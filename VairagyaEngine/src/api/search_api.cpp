#include "api/search_api.hpp"

#include "query/query_engine.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <array>

using namespace std;

namespace api {

    namespace {

        constexpr std::array<signed char, 256> makeHexTable() {
            std::array<signed char, 256> table{};

            for (auto& v : table)
                v = -1;

            for (int i = 0; i <= 9; ++i)
                table['0' + i] = i;

            for (int i = 0; i < 6; ++i) {
                table['a' + i] = 10 + i;
                table['A' + i] = 10 + i;
            }

            return table;
        }

        constexpr auto hexTable = makeHexTable();

        uint32_t parsePositiveUInt(const char* value, uint32_t fallback) {
            if (!value) {
                return fallback;
            }

            try {
                int parsed = stoi(value);
                return parsed > 0 ? static_cast<uint32_t>(parsed) : fallback;
            } catch (...) {
                return fallback;
            }
        }

        uint64_t parsePositiveUInt64(const char* value, uint64_t fallback) {
            if (!value) {
                return fallback;
            }

            try {
                unsigned long long parsed = stoull(value);
                return parsed > 0 ? static_cast<uint64_t>(parsed) : fallback;
            } catch (...) {
                return fallback;
            }
        }



        int fromHex(char ch) {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        }

		// This function decodes percent - encoded characters in a query parameter value, but preserves '+' characters as literal plus signs instead of converting them to spaces.
        string percentDecodePreservingPlus(const string& value) {
            string decoded;
			uint64_t value_size = value.size();
            decoded.reserve(value_size);

            // 
            for (size_t i = 0; i < value_size; ++i) {
                if (value[i] == '%' && i + 2 < value_size) {
                    const int high = hexTable[static_cast<unsigned char>(value[i + 1])];
                    const int low = hexTable[static_cast<unsigned char>(value[i + 2])];
                    if (high >= 0 && low >= 0) {
						decoded.push_back(static_cast<char>((high << 4) | low)); // Convert the two hex digits to a single character and append it to the decoded string.
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(value[i]);
            }

            return decoded;
        }

		// This function extracts the raw query parameter value from the request's raw URL, without any decoding or processing. It looks for the parameter in the raw query string and returns its value as-is, preserving any percent-encoded characters or '+' signs.
        string rawQueryParam(const crow::request& request, const string& name) {
            const auto question = request.raw_url.find('?');
            if (question == string::npos) {
                return "";
            }

            const string query_string = request.raw_url.substr(question + 1);
            size_t start = 0;
            uint64_t query_string_size = query_string.size();
			// Manually parse the query string to find the parameter value without decoding.
            while (start <= query_string_size) {
				const auto end = query_string.find('&', start);
				const string part = query_string.substr(start, end == string::npos ? string::npos : end - start); // Extract the current key-value pair from the query string.

                const auto equals = part.find('=');
                const string key = percentDecodePreservingPlus(part.substr(0, equals));
                if (key == name) {
                    return equals == string::npos ? "" : percentDecodePreservingPlus(part.substr(equals + 1));
                }

                if (end == string::npos) {
                    break;
                }
                start = end + 1;
            }

            return "";
        }

        nlohmann::json toJson(const search::SearchResponse& response) {
            nlohmann::json results = nlohmann::json::array();
            for (const auto& result : response.results) {
                results.push_back({
                    {"doc_id", result.doc_id},
                    {"title", result.title},
                    {"url", result.url},
                    {"display_url", result.display_url},
                    {"favicon_url", result.favicon_url},
                    {"language", result.language},
                    {"snippet", result.snippet},
                    {"score", result.score},
                    {"last_fetched_time", result.last_fetched_time},
                    {"quality_score", result.quality_score}
                });
            }

            return {
                {"query", response.query},
                {"page", response.page},
                {"limit", response.limit},
                {"total", response.total},
                {"results", results}
            };
        }
    }

    void runSearchApi(shared_ptr<storage::RocksDBStore> &db_store, uint16_t& port) {
        if (!db_store) {
            cerr << "[ERROR] Cannot start API without an open database.\n";
            return;
        }

        search::QueryEngine engine;
        engine.load(*db_store);

        cout << "[API] Loaded " << engine.documentCount() << " searchable documents.\n";
        cout << "[API] Listening on http://127.0.0.1:" << port << "/search?q=...\n";

        crow::SimpleApp app;

        CROW_ROUTE(app, "/search").methods(crow::HTTPMethod::GET)(
            [&engine](const crow::request& request) {
                string raw_query = rawQueryParam(request, "q");
                if (raw_query.empty()) {
                    const char* decoded_query = request.url_params.get("q");
                    raw_query = decoded_query ? decoded_query : "";
                }

                if (raw_query.empty()) {
                    auto body = nlohmann::json({
                        {"query", ""},
                        {"page", 1},
                        {"limit", 10},
                        {"total", 0},
                        {"results", nlohmann::json::array()}
                    }).dump();
                    return crow::response(200, "application/json", body);
                }

                const uint32_t page = parsePositiveUInt(request.url_params.get("page"), 1);
                const uint32_t limit = min<uint32_t>(
                    100,
                    parsePositiveUInt(request.url_params.get("limit"), 10)
                );

                auto response = engine.search(raw_query, page, limit);
                return crow::response(200, "application/json", toJson(response).dump());
            }
        );

        CROW_ROUTE(app, "/health").methods(crow::HTTPMethod::GET)(
            [&engine]() {
                return crow::response(200, "application/json", nlohmann::json({
                    {"ok", true},
                    {"searchable_documents", engine.documentCount()}
                }).dump());
            }
        );

        CROW_ROUTE(app, "/stats").methods(crow::HTTPMethod::GET)(
            [&engine]() {
                return crow::response(200, "application/json", nlohmann::json({
                    {"documents", engine.documentCount()},
                    {"cache", "lru"}
                }).dump());
            }
        );

        CROW_ROUTE(app, "/click").methods(crow::HTTPMethod::POST, crow::HTTPMethod::GET)(
            [&engine, db_store](const crow::request& request) {
                const uint64_t doc_id = parsePositiveUInt64(request.url_params.get("doc_id"), 0);
                if (doc_id == 0) {
                    return crow::response(400, "application/json", nlohmann::json({
                        {"ok", false},
                        {"error", "doc_id is required"}
                    }).dump());
                }

                if (!engine.registerClick(doc_id)) {
                    return crow::response(404, "application/json", nlohmann::json({
                        {"ok", false},
                        {"error", "document not found"}
                    }).dump());
                }

                const uint64_t click_count = db_store ? db_store->incrementClickCount(doc_id) : 0;
                return crow::response(200, "application/json", nlohmann::json({
                    {"ok", true},
                    {"doc_id", doc_id},
                    {"click_count", click_count}
                }).dump());
            }
        );

        app.port(port).multithreaded().run();
    }

}

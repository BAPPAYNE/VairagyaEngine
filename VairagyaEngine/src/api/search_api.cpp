#include "api/search_api.h"

#include "query/query_engine.h"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

using namespace std;

namespace api {

    namespace {
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

        string percentDecodePreservingPlus(const string& value) {
            string decoded;
            decoded.reserve(value.size());

            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '%' && i + 2 < value.size()) {
                    const int high = fromHex(value[i + 1]);
                    const int low = fromHex(value[i + 2]);
                    if (high >= 0 && low >= 0) {
                        decoded.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(value[i]);
            }

            return decoded;
        }

        string rawQueryParam(const crow::request& request, const string& name) {
            const auto question = request.raw_url.find('?');
            if (question == string::npos) {
                return "";
            }

            const string query_string = request.raw_url.substr(question + 1);
            size_t start = 0;
            while (start <= query_string.size()) {
                const auto end = query_string.find('&', start);
                const string part = query_string.substr(
                    start,
                    end == string::npos ? string::npos : end - start
                );

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

    void runSearchApi(shared_ptr<storage::RocksDBStore> db_store, uint16_t port) {
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

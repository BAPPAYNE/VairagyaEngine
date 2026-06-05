#include "crawler/engine.h"
#include "utils/log.h"
#include "utils/config.h"
#include "net/fetcher.h"
#include "crawler/scheduler.h"
#include "storage/url_state_store.h"
#include "storage/content_state_store.h"
#include "storage/host_state_store.h"
#include "net/response_classifier.h"
#include "html/html_parser.h"
#include "utils/runtime.h"
#include "utils/utils.h"
#include "host/robots_manager.h"
#include "crawler/frontier.h"
#include "utils/argparse.hpp"
#include "pipeline/doc_core_builder.h"
#include "pipeline/fetch_meta_builder.h"
#include "pipeline/content_meta_builder.h"
#include "pipeline/parsed_content_builder.h"
#include "pipeline/link_data_builder.h"
#include "pipeline/quality_signals_builder.h"
#include "pipeline/presentation_builder.h"
#include "pipeline/control_flags_builder.h"
#include "storage/db_schema.h"

#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <boost/url.hpp>

using namespace std;
using namespace argparse;

namespace crawler {
	Engine::Engine(bool extract_links, shared_ptr<storage::RocksDBStore> db_store)
		: frontier()
		, scheduler(frontier)
		, hostStore()
		, db_store(db_store)
		, robotsManager(hostStore)
		, running(true)
		, extract_links_(extract_links)
	{
	}

	void Engine::addURL(const string& url, int depth, const string& referrer) {
		frontier.push(url, depth, referrer);
	}

	optional<FrontierItem> Engine::nextURL() {
		return scheduler.getNextURL();
	}

	bool Engine::frontierEmpty() const {
		return frontier.empty();
	}

	void Engine::processNextURL() {
		if (!running.load()) return;
		// 1. Get next URL from scheduler/frontier
		auto itemOpt = scheduler.getNextURL();
		if (!itemOpt) {
			return;
		}

		processItem(*itemOpt);
		frontier.completeWork();
	}

	void Engine::processItem(const FrontierItem& item) {
		if (!g_running.load() || !running.load()) {
			return;
		}

		auto stats = frontier.stats();
		cout <<
		    //"[DISCOVERED] : " << stats.discovered<< "\n" <<
			//"[FETCHED] : " << stats.fetched<< "\n" <<
			//"[FAILED] : " << stats.failed<< "\n" <<
			//"[RETRIED] : " << stats.retried<< "\n" <<
			//"[DISALLOWED] : " << stats.disallowed << "\n" << 
			//"----------------------\n"
		"" ;

        // 1.5. Check Robots.txt only if NOT ignoring robots
		if (!ignore_robots) {
			if (!robotsManager.hasRulesForUrl(item.normalized_url)) {
				string robotsUrl = RobotsManager::getRobotsURL(item.normalized_url);
				if (!robotsUrl.empty()) {
					// We don't have rules for this host yet. Fetch robots.txt first.
					// Note: synchronous fetch here for simplicity.
					auto result = net::fetch(robotsUrl);
					RobotsRules rules;
					if (result.http_code >= 200 && result.http_code < 300) {
						rules = robotsManager.extractRobotsDirectives(result.content, "VairagyaEngine");
					} else {
						// fetching failed or 404, assume allow all (default empty rules)
					}
					string host = RobotsManager::extractHost(item.normalized_url);
					robotsManager.updateRobots(host, rules);
					cout << "[ROBOTS] Fetched " << robotsUrl << " Status: " << result.http_code;
						//<< " => Allowed: " << rules.allow.size() << ", Disallowed: " << rules.disallow.size() << " (Ignored: No)" << endl;
				}
			}
			if (!robotsManager.canFetch(item.normalized_url)) {
				markDisallowed(item.normalized_url);
				return;
			}
		}

		// 2. Fetch
		auto result = net::fetch(item.normalized_url);

		// 3. Classify response
		auto cls = net::classify(result);

		cout << "Fetched: " << item.normalized_url
			<< " HTTP: " << result.http_code << " Size: " << result.content.size()
			<< endl;

		// 4. Act based on classification
		switch (cls) {

		case net::ResponseClass::OK: {
			if (result.http_code == 200) {
				log_utils::log_url(item.normalized_url);

				// Identify if content is likely parseable (HTML/Text)
                bool parseable = false;
                string ct = result.content_type;
                transform(ct.begin(), ct.end(), ct.begin(), ::tolower);
                
                if (ct.find("text/html") != string::npos || ct.find("text/plain") != string::npos || 
                    ct.find("application/rss+xml") != string::npos || ct.find("application/xml") != string::npos ||
                    ct.find("application/xhtml+xml") != string::npos ||
                    ct.empty()) { // If empty, fallback to extension check
                    parseable = true;
                    
                    string lower_url = item.normalized_url;
                    transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
                    if (lower_url.find(".woff") != string::npos || lower_url.find(".ttf") != string::npos ||
                    	lower_url.find(".gif") != string::npos || lower_url.find(".ico") != string::npos ||
                        lower_url.find(".zip") != string::npos || lower_url.find(".gz") != string::npos ||
                        lower_url.find(".bin") != string::npos || lower_url.find(".woff2") != string::npos ||
						lower_url.find(".js") != string::npos  || lower_url.find(".css") != string::npos ) {
                        parseable = false;
                    }
                }

				// 1. Build Document Core
				storage::DocCore doc = DocCoreBuilder::build(item.normalized_url, parseable ? result.content : "");
				
				// 2. Assign and persistent increment Doc ID
				uint64_t current_id = 0;
				bool is_new_url = true;
				if (db_store) {
					auto existing_doc_json = db_store->get(storage::CF_DOC_CORE, doc.url_hash);
					if (existing_doc_json) {
						try {
							auto existing_doc = nlohmann::json::parse(*existing_doc_json).get<storage::DocCore>();
							if (existing_doc.normalized_url == doc.normalized_url && existing_doc.doc_id != 0) {
								doc.doc_id = existing_doc.doc_id;
								doc.first_seen_time = existing_doc.first_seen_time;
								is_new_url = false;
							}
						} catch (...) {
							// Malformed existing records are overwritten with a fresh id below.
						}
					}

					if (is_new_url) {
						lock_guard<mutex> lock(doc_id_mutex_);
						current_id = db_store->getNextDocId();
						doc.doc_id = current_id;
						db_store->setNextDocId(current_id + 1);
					}
				}
				
				// 3. Run Pipeline Builders
				ParsedContent parsed;
                if (parseable) {
                    parsed = ParsedContentBuilder::build(result.content);
                }

				FetchMeta fetch = FetchMetaBuilder::build(result.http_code, (int)result.fetch_time_ms, 
                                                               result.content.size(), "", "", 
                                                               item.depth, item.referrer_url);
				ContentMeta content = ContentMetaBuilder::build(parsed.clean_text);
				string previous_content_hash;
				if (db_store) {
					auto existing_content_json = db_store->get(storage::CF_CONTENT_META, doc.url_hash);
					if (existing_content_json) {
						auto parsed_content_meta = nlohmann::json::parse(*existing_content_json, nullptr, false);
						if (!parsed_content_meta.is_discarded()) {
							try {
								previous_content_hash = parsed_content_meta.get<storage::ContentMeta>().content_hash;
							} catch (...) {
							}
						}
					}
				}
				
                vector<string> rawLinks;
                if (extract_links_ && parseable) {
                    rawLinks = extractLinks(result.content);
                }
                
                LinkData links = LinkDataBuilder::build((uint32_t)rawLinks.size());
				QualitySignals quality = QualitySignalsBuilder::build(parsed.clean_text, time(nullptr));
				Presentation pres = PresentationBuilder::build(parsed.clean_text, parseable ? result.content : "", "", 
                                                                 item.normalized_url, "");
				ControlFlags ctrl = ControlFlagsBuilder::build(parseable ? result.content : "", true);

                auto sanitizeField = [](string& value) {
                    value = sanitizeUtf8Lossy(value);
                };
                sanitizeField(doc.normalized_url);
                sanitizeField(doc.url_hash);
                sanitizeField(doc.canonical_url);
                sanitizeField(doc.language_code);
                sanitizeField(doc.charset);
                sanitizeField(doc.content_type);
                sanitizeField(fetch.etag);
                sanitizeField(fetch.last_modified);
                sanitizeField(fetch.referrer_url);
                sanitizeField(content.content_hash);
                sanitizeField(parsed.title);
                sanitizeField(parsed.meta_description);
                sanitizeField(parsed.clean_text);
                sanitizeField(pres.snippet);
                sanitizeField(pres.favicon_url);
                sanitizeField(pres.site_name);
                sanitizeField(pres.breadcrumb);
                sanitizeField(pres.display_url);
                sanitizeField(ctrl.index_status);
                sanitizeField(ctrl.error_reason);

				// 4. Persistence into RocksDB
				if (db_store) {
					using json = nlohmann::json;
					db_store->put(storage::CF_DOC_CORE, doc.url_hash, json(doc).dump());
					db_store->put(storage::CF_FETCH_META, doc.url_hash, json(fetch).dump());
					db_store->put(storage::CF_CONTENT_META, doc.url_hash, json(content).dump());
					db_store->put(storage::CF_PARSED_CONTENT, doc.url_hash, json(parsed).dump());
					db_store->put(storage::CF_LINK_GRAPH, doc.url_hash, json(links).dump());
					db_store->put(storage::CF_QUALITY, doc.url_hash, json(quality).dump());
					db_store->put(storage::CF_PRESENTATION, doc.url_hash, json(pres).dump());
					db_store->put(storage::CF_CONTROL, doc.url_hash, json(ctrl).dump());
					db_store->recordCrawlResult(
						doc.url_hash,
						doc.normalized_url,
						result.http_code,
						content.content_hash,
						!previous_content_hash.empty() && previous_content_hash != content.content_hash
					);
					
					if (is_new_url) {
						// Domain Indexing
						string rev_host = reverseHost(item.normalized_url);
						string domain_key = storage::RocksDBStore::buildDomainKey(rev_host, "/", doc.doc_id);
						db_store->put(storage::CF_DOMAIN_INDEX, domain_key, doc.url_hash);
					}
				}

				cout << "[INDEXED] " << item.normalized_url << " (ID: " << doc.doc_id << ", Lang: " << doc.language_code << ")\n";
                
                if (extract_links_ && parseable) {
                    cout << "[LINKS] Found: " << rawLinks.size() << endl;

                    // Resolve + process + enqueue
                    for (const auto& raw : rawLinks) {
						if (!g_running.load() || !running.load()) {
							break;
						}
                        auto resolved = resolveRelativeURL(raw, item.normalized_url);
                        if (!resolved) continue;

                        auto processed = processURL(*resolved);
                        if (processed.status == URLStatus::ACCEPTED_URL) {
                            bool domain_allowed = true;
                            if (same_domain && !allowed_domains.empty()) {
                                try {
                                    auto parsed_url = boost::urls::parse_uri(processed.normalized);
                                    if (parsed_url) {
                                        string domain = string(parsed_url->host());
                                        domain_allowed = false;
                                        for (const auto& allowed : allowed_domains) {
                                            if (domain == allowed || (domain.size() > allowed.size() && domain.ends_with("." + allowed))) {
                                                domain_allowed = true;
                                                break;
                                            }
                                        }
                                    } else {
                                        domain_allowed = false;
                                    }
                                } catch (...) {
                                    domain_allowed = false;
                                }
                            }

                            if (domain_allowed && (ignore_robots || robotsManager.canFetch(processed.normalized))) {
                                
								if (domain_allowed && isHtmlPageUrl(processed.normalized) && (ignore_robots || robotsManager.canFetch(processed.normalized))) {
									addURL(processed.normalized, item.depth + 1, item.normalized_url);
								}
                            }
                        }
                    }
                }
			}
			markFetched(item.normalized_url, result.http_code);
			break;
		}

		case net::ResponseClass::REDIRECT: {
			// Redirect handled as terminal fetch for now
			if (db_store) {
				db_store->recordCrawlResult(
					DocCoreBuilder::hashUrl(item.normalized_url),
					item.normalized_url,
					result.http_code,
					"",
					false
				);
			}
			markFetched(item.normalized_url, result.http_code);
			break;
		}

		case net::ResponseClass::CLIENT_ERROR: {
			// 4xx → terminal
			if (db_store) {
				db_store->recordCrawlResult(
					DocCoreBuilder::hashUrl(item.normalized_url),
					item.normalized_url,
					result.http_code,
					"",
					false
				);
			}
			markFailed(item.normalized_url, result.http_code);
			break;
		}

		case net::ResponseClass::SERVER_ERROR:
			[[fallthrough]];
		case net::ResponseClass::NETWORK_ERROR: {
			// Retryable
			if (db_store) {
				db_store->recordCrawlResult(
					DocCoreBuilder::hashUrl(item.normalized_url),
					item.normalized_url,
					result.http_code,
					"",
					false
				);
			}
			markRetry(item.normalized_url, result.status, result.http_code);
			break;
		}

		default:
			// Defensive: treat unknown as retryable
			if (db_store) {
				db_store->recordCrawlResult(
					DocCoreBuilder::hashUrl(item.normalized_url),
					item.normalized_url,
					result.http_code,
					"",
					false
				);
			}
			markRetry(item.normalized_url, result.status, result.http_code);
			break;
		}

		
	}

	void Engine::workerLoop(size_t worker_id) {
		while (g_running.load() && running.load()) {
			auto itemOpt = frontier.popWait(g_running);
			if (!itemOpt) {
				break;
			}

			try {
				processItem(*itemOpt);
			} catch (const exception& err) {
				cerr << "[ERROR] Worker " << worker_id << " crashed while processing URL: " << err.what() << "\n";
			} catch (...) {
				cerr << "[ERROR] Worker " << worker_id << " crashed while processing URL with an unknown exception.\n";
			}
			frontier.completeWork();
		}
	}

	void Engine::run(size_t worker_count) {
		if (worker_count == 0) {
			worker_count = 1;
		}

		vector<thread> workers;
		workers.reserve(worker_count);
		for (size_t i = 0; i < worker_count; ++i) {
			workers.emplace_back(&Engine::workerLoop, this, i + 1);
		}

		for (auto& worker : workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}

	bool Engine::shouldContinue() const {
		if (!running.load()) {
			return false;
		}
		if (frontierEmpty()) {
			return false;
		}
		/*
		* Later
			scheduler backoff
			host-level limits
			global stop conditions
		*/
		return true;
	}

	void Engine::shutdown() {
		/*
		Description
			Gracefully stops the engine.
		Responsibilities
			Set running flag to false
			Prevent new URL processing
			Allow current operation to complete
		*/
		running.store(false);
		frontier.shutdown();
	}

	void Engine::markFetched(const string& url, uint16_t http_status) {
		frontier.markFetched(url, http_status);
	}

	void Engine::markFailed(const string& url, uint16_t http_status) {
		frontier.markFailed(url, http_status);
	}

	void Engine::markRetry(const string& url, net::FetchStatus status, uint16_t http_status) {
		frontier.markRetry(url, status, http_status);
	}

	void Engine::markDisallowed(const string &url) {
		frontier.markDisallowed(url);
	}

	vector<string> Engine::get200URLs() const {
		return frontier.getSuccessfulURLs();
	}

	vector<string> Engine::getPendingURLs() const {
		return frontier.getPendingURLs();
	}

};

void crawler::runCrawler(const vector<string>& initialURLs, shared_ptr<storage::RocksDBStore> db_store, size_t worker_count) {
	Engine engine(crawl_links, db_store);

	log_utils::init_output_streams(json_output_path, txt_output_path);

	auto saveCheckpoint = [&]() {
		if (db_store) {
			db_store->savePendingURLs(engine.getPendingURLs());
		}
	};

	// Seed
	int size_initialURLS = initialURLs.size();
	for (int i = 0; i < size_initialURLS; i++) {
		auto seed = processURL(initialURLs[i]);
		if (seed.status == URLStatus::ACCEPTED_URL) {
			engine.addURL(seed.normalized, 0, "SEED");
		}
	}
	saveCheckpoint();

	cout << "[INFO] Starting " << worker_count << " crawler worker thread(s).\n";
	engine.run(worker_count);
	engine.shutdown();
	saveCheckpoint();

	if (db_store) {
		auto pendingURLs = engine.getPendingURLs();
		db_store->savePendingURLs(pendingURLs);
		cout << "[RESUME] Saved " << pendingURLs.size() << " pending URLs to database.\n";
	}
	
	log_utils::close_output_streams();
}

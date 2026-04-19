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
		if (!running) return;
		cout <<
		    "[DISCOVERED] : " << frontier.crawl_stats.discovered<< "\n" <<
			"[FETCHED] : " << frontier.crawl_stats.fetched<< "\n" <<
			"[FAILED] : " << frontier.crawl_stats.failed<< "\n" <<
			"[RETRIED] : " << frontier.crawl_stats.retried<< "\n" <<
			"[DISALLOWED] : " << frontier.crawl_stats.disallowed << "\n" << 
			"----------------------\n"
		"" ;
		// 1. Get next URL from scheduler/frontier
		auto itemOpt = scheduler.getNextURL();
		if (!itemOpt) {
			return;
		}

		const FrontierItem& item = *itemOpt;

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
					cout << "[ROBOTS] Fetched " << robotsUrl << " Status: " << result.http_code 
						<< " => Allowed: " << rules.allow.size() << ", Disallowed: " << rules.disallow.size() << " (Ignored: No)" << endl;
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
                        lower_url.find(".png") != string::npos || lower_url.find(".jpg") != string::npos ||
                        lower_url.find(".jpeg") != string::npos || lower_url.find(".gif") != string::npos ||
                        lower_url.find(".ico") != string::npos || lower_url.find(".pdf") != string::npos ||
                        lower_url.find(".zip") != string::npos || lower_url.find(".gz") != string::npos ||
                        lower_url.find(".bin") != string::npos || lower_url.find(".woff2") != string::npos) {
                        parseable = false;
                    }
                }

				// 1. Build Document Core
				storage::DocCore doc = DocCoreBuilder::build(item.normalized_url, parseable ? result.content : "");
				
				// 2. Assign and persistent increment Doc ID
				uint64_t current_id = 0;
				if (db_store) {
					current_id = db_store->getNextDocId();
					doc.doc_id = current_id;
					db_store->setNextDocId(current_id + 1);
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
				
                vector<string> rawLinks;
                if (extract_links_ && parseable) {
                    rawLinks = extractLinks(result.content);
                }
                
                LinkData links = LinkDataBuilder::build((uint32_t)rawLinks.size());
				QualitySignals quality = QualitySignalsBuilder::build(parsed.clean_text, time(nullptr));
				Presentation pres = PresentationBuilder::build(parsed.clean_text, parseable ? result.content : "", "", 
                                                                 item.normalized_url, "");
				ControlFlags ctrl = ControlFlagsBuilder::build(parseable ? result.content : "", true);

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
					
					// Domain Indexing
					string rev_host = reverseHost(item.normalized_url);
					string domain_key = storage::RocksDBStore::buildDomainKey(rev_host, "/", doc.doc_id);
					db_store->put(storage::CF_DOMAIN_INDEX, domain_key, doc.url_hash);
				}

				cout << "[INDEXED] " << item.normalized_url << " (ID: " << doc.doc_id << ", Lang: " << doc.language_code << ")\n";
                
                if (extract_links_ && parseable) {
                    cout << "[LINKS] Found: " << rawLinks.size() << endl;

                    // Resolve + process + enqueue
                    for (const auto& raw : rawLinks) {
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
			markFetched(item.normalized_url, result.http_code);
			break;
		}

		case net::ResponseClass::CLIENT_ERROR: {
			// 4xx → terminal
			markFailed(item.normalized_url, result.http_code);
			break;
		}

		case net::ResponseClass::SERVER_ERROR:
			[[fallthrough]];
		case net::ResponseClass::NETWORK_ERROR: {
			// Retryable
			markRetry(item.normalized_url, result.status, result.http_code);
			break;
		}

		default:
			// Defensive: treat unknown as retryable
			markRetry(item.normalized_url, result.status, result.http_code);
			break;
		}

		
	}

	bool Engine::shouldContinue() const {
		if (!running) {
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
		running = false;
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

void crawler::runCrawler(const vector<string>& initialURLs, shared_ptr<storage::RocksDBStore> db_store) {
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

	while (g_running && engine.shouldContinue()) {
		saveCheckpoint();
		try {
			engine.processNextURL();
		} catch (const exception& err) {
			cerr << "[ERROR] URL processing crashed: " << err.what() << "\n";
			saveCheckpoint();
			continue;
		} catch (...) {
			cerr << "[ERROR] URL processing crashed with an unknown exception.\n";
			saveCheckpoint();
			continue;
		}
		saveCheckpoint();
	}

	if (db_store) {
		auto pendingURLs = engine.getPendingURLs();
		db_store->savePendingURLs(pendingURLs);
		cout << "[RESUME] Saved " << pendingURLs.size() << " pending URLs to database.\n";
	}
	
	engine.shutdown();
	log_utils::close_output_streams();
}

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
#include "host/robots_manager.h"
#include "crawler/frontier.h"
#include "utils/argparse.hpp"
#include "pipeline/doc_core_builder.h"

#include <iostream>
#include <boost/url.hpp>

using namespace std;
using namespace argparse;

namespace crawler {
	Engine::Engine(bool extract_links)
		: frontier()
		, scheduler(frontier)
		, hostStore()
		, robotsManager(hostStore)
		, running(true)
		, extract_links_(extract_links)
	{
	}

	void Engine::addURL(const string& url) {
		frontier.push(url);
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
		    //"[DISCOVERED] : " << frontier.crawl_stats.discovered<< "\n" <<
			"[FATCHED] : " << frontier.crawl_stats.fetched<< "\n" <<
		//	"[FAILED] : " << frontier.crawl_stats.failed<< "\n" <<
		//	"[RETRIED] : " << frontier.crawl_stats.retried<< "\n" <<
		//	"[DISALLOWED] : " << frontier.crawl_stats.disallowed << "\n" << 
		//	"----------------------\n"
		"" ;
		// 1. Get next URL from scheduler/frontier
		auto itemOpt = scheduler.getNextURL();
		if (!itemOpt) {
			return;
		}

		const FrontierItem& item = *itemOpt;

		// 1.5. Check Robots.txt
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
					<< " => Allowed: " << rules.allow.size() << ", Disallowed: " << rules.disallow.size() << endl;
				
			}
		}

		if (!robotsManager.canFetch(item.normalized_url)) {
			markDisallowed(item.normalized_url);
			return;
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
			// Mark success
			markFetched(item.normalized_url, result.http_code);
			
			if (result.http_code == 200) {
				log_utils::log_url(item.normalized_url);
				// Build DocCore (for now, we just log it, but this is where we'd normally store it or pass it to the next pipeline stage)
				DocCore doc = DocCoreBuilder::build(item.normalized_url, result.content);
				cout << "  - Language: " << doc.language_code << endl;
				cout << "  - Charset: " << doc.charset << endl;
				cout << "  - Content-Type: " << doc.content_type << endl;
				cout << "  - Canonical: " << doc.canonical_url << endl;
				cout << "  - Hash: " << doc.url_hash << endl;
			}

			// Extract raw links
			if (extract_links_) {
				auto rawLinks = extractLinks(result.content);

				// Resolve + process + enqueue
				for (const auto& raw : rawLinks) {
					auto resolved = resolveRelativeURL(raw, item.normalized_url);
					if (!resolved) continue;

					auto processed = processURL(*resolved);
					if (processed.status == URLStatus::ACCEPTED_URL) {
						// Check same-domain restriction if enabled
						bool domain_allowed = true;
						if (same_domain && !allowed_domains.empty()) {
							try {
								auto parsed = boost::urls::parse_uri(processed.normalized);
								if (parsed) {
									string domain = string(parsed->host());
									domain_allowed = allowed_domains.find(domain) != allowed_domains.end();
								} else {
									domain_allowed = false;
								}
							} catch (...) {
								domain_allowed = false;
							}
						}

						if (domain_allowed && robotsManager.canFetch(processed.normalized)) {
							addURL(processed.normalized);
						}
					}
				}
			}
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

};

void crawler::runCrawler(const vector<string>& initialURLs) {
	Engine engine(crawl_links);

	log_utils::init_output_streams(json_output_path, txt_output_path);

	// Seed
	int size_initialURLS = initialURLs.size();
	for (int i = 0; i < size_initialURLS; i++) {
		auto seed = processURL(initialURLs[i]);
		if (seed.status == URLStatus::ACCEPTED_URL) {
			engine.addURL(seed.normalized);
		}
	}

	while (g_running && engine.shouldContinue()) {
		engine.processNextURL();
	}
	
	engine.shutdown();
	log_utils::close_output_streams();
}
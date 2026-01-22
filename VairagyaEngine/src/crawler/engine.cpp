#include "crawler/engine.h"
#include "net/fetcher.h"
#include "crawler/scheduler.h"
#include "storage/url_state_store.h"
#include "storage/content_state_store.h"
#include "storage/host_state_store.h"
#include "net/response_classifier.h"
#include "html/html_parser.h"
#include "utils/runtime.h"

#include <iostream>

using namespace std;

namespace crawler {
	Engine::Engine()
		: frontier()
		, scheduler(frontier)
		, running(true)
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

		// 1. Get next URL from scheduler/frontier
		auto itemOpt = scheduler.getNextURL();
		if (!itemOpt) {
			return;
		}

		const FrontierItem& item = *itemOpt;

		// 2. Fetch
		auto result = net::fetch(item.normalized_url);

		// 3. Classify response
		auto cls = net::classify(result);

		cout << "Fetched: " << item.normalized_url
			<< " HTTP:" << result.http_code
			<< " Class:" << static_cast<int>(cls)
			<< endl;

		// 4. Act based on classification
		switch (cls) {

		case net::ResponseClass::OK: {
			// Mark success
			markFetched(item.normalized_url, result.http_code);

			// Extract raw links
			auto rawLinks = extractLinks(result.content);

			// Resolve + process + enqueue
			for (const auto& raw : rawLinks) {
				auto resolved = resolveRelativeURL(raw, item.normalized_url);
				if (!resolved) continue;

				auto processed = processURL(*resolved);
				if (processed.status == URLStatus::ACCEPTED_URL) {
					addURL(processed.normalized);
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

	void Engine::markRetry(const string& url,
		net::FetchStatus status,
		uint16_t http_status)
	{
		frontier.markRetry(url, status, http_status);
	}


};

void crawler::runCrawler() {
	Engine engine;

	// Seed
	auto seed = processURL("https://www.google.com");
	if (seed.status == URLStatus::ACCEPTED_URL) {
		engine.addURL(seed.normalized);
	}

	while (g_running && engine.shouldContinue()) {
		engine.processNextURL();
	}

	engine.shutdown();
}
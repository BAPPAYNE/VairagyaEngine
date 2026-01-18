#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <optional>

#include "crawler/frontier.h"
#include "url/process.h"
#include "crawler/scheduler.h"

namespace crawler {

	class Engine {
	public:
		Engine();

		// Intake AFTER processing
		void addURL(const string& url);

		// Frontier access
		std::optional<FrontierItem> nextURL();
		bool frontierEmpty() const;
		void processNextURL();
		bool shouldContinue() const;
		void shutdown();

		void markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_code);
		void markFailed(const string& url, uint16_t http_code);
		void markFetched(const string& url, uint16_t http_code);

	private:
		Frontier frontier; // Manages URLs to be crawled
		Scheduler scheduler; // Manages URL scheduling
		bool running; // Indicates if the engine is active
	};

	void runCrawler();
};

#endif // ENGINE_H
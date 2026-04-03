#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <optional>

#include "crawler/frontier.h"
#include "url/process.h"
#include "crawler/scheduler.h"
#include "host/robots_manager.h"
#include "storage/rocksdb_store.h"
#include "storage/memory_host_state_store.h"
#include <memory>

namespace crawler {

	class Engine {
	public:
		Engine(bool extract_links = false, std::shared_ptr<storage::RocksDBStore> db_store = nullptr);

		// Intake AFTER processing
		void addURL(const string& url, int depth = 0, const string& referrer = "");

		// Frontier access
		std::optional<FrontierItem> nextURL();
		bool frontierEmpty() const;
		void processNextURL();
		bool shouldContinue() const;
		void shutdown();

		void markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_code);
		void markFailed(const string& url, uint16_t http_code);
		void markFetched(const string& url, uint16_t http_code);
		void markDisallowed(const string& url);

		std::vector<std::string> get200URLs() const;

	private:
		Frontier frontier; // Manages URLs to be crawled
		Scheduler scheduler; // Manages URL scheduling
		storage::MemoryHostStateStore hostStore;
		std::shared_ptr<storage::RocksDBStore> db_store;
		RobotsManager robotsManager;
		bool running; // Indicates if the engine is active
		bool extract_links_;
	};

	void runCrawler(const std::vector<std::string>& initialURLs, std::shared_ptr<storage::RocksDBStore> db_store = nullptr);
};

#endif // ENGINE_H
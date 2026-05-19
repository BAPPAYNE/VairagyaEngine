#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <cstdint>
#include <optional>
#include <mutex>
#include <atomic>

#include "crawler/frontier.h"
#include "url/process.h"
#include "crawler/scheduler.h"
#include "host/robots_manager.h"
#include "storage/rocksdb_store.h"
#include "storage/memory_host_state_store.h"
#include <memory>

using namespace std;

namespace crawler {

	class Engine {
	public:
		Engine(bool extract_links = false, shared_ptr<storage::RocksDBStore> db_store = nullptr);

		// Intake AFTER processing
		void addURL(const string& url, int depth = 0, const string& referrer = "");

		// Frontier access
		optional<FrontierItem> nextURL();
		bool frontierEmpty() const;
		void processNextURL();
		void run(size_t worker_count);
		bool shouldContinue() const;
		void shutdown();

		void markRetry(const string& url, net::FetchStatus fetch_status, uint16_t http_code);
		void markFailed(const string& url, uint16_t http_code);
		void markFetched(const string& url, uint16_t http_code);
		void markDisallowed(const string& url);

		vector<string> get200URLs() const;
		vector<string> getPendingURLs() const;

	private:
		Frontier frontier; // Manages URLs to be crawled
		Scheduler scheduler; // Manages URL scheduling
		storage::MemoryHostStateStore hostStore;
		shared_ptr<storage::RocksDBStore> db_store;
		RobotsManager robotsManager;
		atomic<bool> running; // Indicates if the engine is active
		bool extract_links_;
		mutex doc_id_mutex_;

		void processItem(const FrontierItem& item);
		void workerLoop(size_t worker_id);
	};

	void runCrawler(const vector<string>& initialURLs, shared_ptr<storage::RocksDBStore> db_store = nullptr, size_t worker_count = 1);
};

#endif // ENGINE_H

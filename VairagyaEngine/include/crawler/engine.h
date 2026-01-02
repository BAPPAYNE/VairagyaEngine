#ifndef ENGINE_H
#define ENGINE_H

#include <string>
#include <optional>

#include "crawler/frontier.h"
#include "url/process.h"

namespace crawler {

	class Engine {
	public:
		Engine();

		// Intake AFTER processing
		void addURL(const string& url);

		// Frontier access
		std::optional<FrontierItem> nextItem();
		bool frontierEmpty() const;
	private:
		Frontier frontier;
	};

	void runCrawler();
};

#endif // ENGINE_H
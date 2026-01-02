#include "crawler/engine.h"
#include "net/fetcher.h"

#include <iostream>

using namespace std;

namespace crawler {
	Engine::Engine() : frontier() {}

	void Engine::addURL(const string& url) {
		frontier.push(url);
	}

	optional<FrontierItem> Engine::nextItem() {
		return frontier.pop();
	}

	bool Engine::frontierEmpty() const {
		return frontier.empty();
	}
};

void crawler::runCrawler() {
	Engine engine;

	auto seed = processURL("https://www.example.com");
	if (seed.status == URLStatus::ACCEPTED) {
		engine.addURL(seed.normalized); // still OK for now
	}

	while (!engine.frontierEmpty()) {
		auto itemOpt = engine.nextItem();
		if (!itemOpt) continue;

		const FrontierItem& item = *itemOpt;

		auto result = net::fetch(item.url);

		cout << "Fetched: " << item.url
			<< " status=" << (int)result.http_code
			<< endl;
	}

}
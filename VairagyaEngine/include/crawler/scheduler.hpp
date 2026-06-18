#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <optional>
#include <string>

#include "crawler/frontier.hpp"

namespace crawler {
	class Scheduler {
	public:
		explicit Scheduler(Frontier& frontier);
		optional<FrontierItem> getNextURL();

	private:
		Frontier& frontier_;
	};
};


#endif // SCHEDULER_H
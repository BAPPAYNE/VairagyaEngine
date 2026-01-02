#ifndef SCHENDULER_H
#define SCHENDULER_H

#include <optional>
#include "crawler/frontier.h"

namespace Crawler {
	class Scheduler {
	public:
		explicit Scheduler(Frontier& frontier);
		optional<string> getNextURL();

	private:
		Frontier& frontier_;
	};
}


#endif // SCHENDULER_H
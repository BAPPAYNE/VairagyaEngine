#include "crawler/scheduler.h"

using namespace std;

namespace crawler {
	Scheduler::Scheduler(Frontier& frontier)
		: frontier_(frontier)
	{
	}

	optional<FrontierItem> Scheduler::getNextURL() {
		
	/*
	@brief Retrieve the next URL to crawl.
	
	Responsibility:
	- Query the associated `Frontier` for the next available `FrontierItem`.
	- Return the result as `std::optional<FrontierItem>`. A disengaged optional
	  indicates that no item is currently available.
	
	Thread-safety:
	- Assumes `Frontier::pop()` handles necessary synchronization if the frontier
	  is accessed concurrently by multiple schedulers or workers.
	
	@return optional<FrontierItem> The next frontier item, or std::nullopt if none.
	 */
		return frontier_.pop();
	}
}
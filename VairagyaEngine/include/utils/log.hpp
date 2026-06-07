#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

using namespace std;

// If application runs in DEBUG mode, this function will print debug information to the console.
void debug(const char* format, ...);

namespace log_utils {
	void init_output_streams(const string& json_path, const string& txt_path); // Initializes output streams for both JSON and TXT logging. If files already exist, it loads existing URLs to prevent duplicates. JSON file is opened and closed on each write, while TXT file is kept open for appending until close_output_streams is called.
	void close_output_streams(); // Closes any open file streams. JSON file is closed after every write in log_url, so this mainly ensures the TXT file is properly closed.
	void log_url(const string& url); // Logs a URL to both TXT and JSON outputs, ensuring no duplicates are logged.
}

#endif // LOG_H
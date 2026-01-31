#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

// If application runs in DEBUG mode, this function will print debug information to the console.
void debug(const char* format, ...);

namespace log_utils {
    void init_output_streams(const std::string& json_path, const std::string& txt_path);
    void close_output_streams();
    void log_url(const std::string& url);
}

#endif // LOG_H
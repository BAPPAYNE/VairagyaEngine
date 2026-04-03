#ifndef RUNTIME_H
#define RUNTIME_H

#include <atomic>

extern std::atomic<bool> g_running; // atomic boolean to check if the application is running

#endif // RUNTIME_H
#ifndef RUNTIME_H
#define RUNTIME_H

#include <atomic>
#include <mutex>

using namespace std;

extern atomic<bool> g_running; // atomic boolean to check if the application is running
extern mutex g_io_mtx;         // serializes console output across worker threads

#endif // RUNTIME_H 
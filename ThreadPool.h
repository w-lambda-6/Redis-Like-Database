#pragma once

#include <cstddef>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <functional>

class Task {
public:
    std::function<void(*void)> f = nullptr;
    void *arg = nullptr;

    Task(std::function<void(void*)> func, void* argument) : f(func), arg(argument) {}
};

class ThreadPool {
public:
    std::vector<std::thread> threads;
    std::deque<Task> queue;
    std::mutex mu;
    std::condition_variable not_empty;

    void init(size_t num_threads);
    void produce(std::function<void(void*)> f, void* arg);
private:
    void worker();
};
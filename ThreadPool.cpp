#include "ThreadPool.h"
#include <iostream>
#include <functional>
#include <thread>
#include <cassert>
#include <mutex>

void ThreadPool::worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(mu);
        
        // Wait until there's something in the queue
        not_empty.wait(lock, [this]{ return !queue.empty(); });

        // Get the next task
        Task task = queue.front();
        queue.pop_front();

        lock.unlock();  // Unlock mutex while processing the task

        // Execute the task
        task.f(task.arg);
    }
}

void ThreadPool::init(size_t num_threads) {
    assert(num_threads > 0);

    threads.resize(num_threads);
    
    for (size_t i = 0; i < num_threads; ++i) {
        threads[i] = std::thread(&ThreadPool::worker, this);
    }
}

void ThreadPool::produce(std::function<void(void*)> f, void* arg) {
    // Lock the mutex to safely modify the queue
    std::lock_guard<std::mutex> lock(mu);

    // Create a task and add it to the queue
    queue.push_back(Task(f, arg));

    // Notify one waiting worker thread that a new task is available
    not_empty.notify_one();
}
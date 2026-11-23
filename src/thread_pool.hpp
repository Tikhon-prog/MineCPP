#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace minecpp {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count) {
        if (thread_count == 0) thread_count = 1;
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(mutex_);
                        ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                        if (stopping_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                        ++active_;
                    }
                    try { task(); } catch (...) { /* Individual jobs report errors through their result queues. */ }
                    {
                        std::lock_guard lock(mutex_);
                        --active_;
                        if (tasks_.empty() && active_ == 0) idle_.notify_all();
                    }
                }
            });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() { shutdown(); }

    void submit(std::function<void()> task) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            tasks_.push(std::move(task));
        }
        ready_.notify_one();
    }

    void wait_idle() {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
    }

    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        workers_.clear();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable idle_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::size_t active_ = 0;
    bool stopping_ = false;
};

} // namespace minecpp

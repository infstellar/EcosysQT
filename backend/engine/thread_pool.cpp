#include "thread_pool.h"

#include <cassert>

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        if (num_threads == 0) num_threads = 1; // 确保至少一个线程
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            // 拒绝在停止后提交任务
            return;
        }
        tasks_.push(std::move(task));
        ++outstanding_;
    }
    cv_task_.notify_one();
}

void ThreadPool::wait_for_completion() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_completed_.wait(lock, [this] {
        return tasks_.empty() && outstanding_.load() == 0;
    });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_task_.notify_all();

    for (auto &t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_task_.wait(lock, [this] {
                return stopping_.load() || !tasks_.empty();
            });

            if (stopping_.load() && tasks_.empty()) {
                break; // 退出线程
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        // 执行任务（不持锁）
        try {
            if (task) task();
        } catch (...) {
            // 生产环境可接入日志系统；此处静默失败以不影响线程池
        }

        // 标记完成并进行完成条件通知
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto remaining = --outstanding_;
            if (tasks_.empty() && remaining == 0) {
                cv_completed_.notify_all();
            }
        }
    }
}
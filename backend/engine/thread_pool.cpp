#include "thread_pool.h"

#include <cassert>
#include <algorithm>
#include <limits>

namespace {
// 线程局部存储（TLS）变量，用于存储每个工作线程的唯一索引。
// 这使得在任务执行期间，可以识别出当前是哪个线程在工作。
// 初始值设为最大值，表示无效索引。
thread_local std::size_t tls_worker_index = std::numeric_limits<std::size_t>::max();
}

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        if (num_threads == 0) num_threads = 1; // 确保至少一个线程
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        // 在创建线程时，捕获其索引 `i`，并传递给工作循环。
        workers_.emplace_back([this, i] { worker_loop(i); });
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

// 返回线程池中的工作线程数量。
// 至少返回1，以避免在没有线程的情况下出现除以零的错误。
std::size_t ThreadPool::worker_count() const {
    return std::max<std::size_t>(1, workers_.size());
}

// 获取当前线程的工作索引。
// 如果当前线程不是线程池的工作线程，则返回一个无效索引。
std::size_t ThreadPool::current_worker_index() {
    return tls_worker_index;
}

// 每个工作线程的主循环。
// 参数 `worker_index` 是此线程的唯一标识符。
void ThreadPool::worker_loop(std::size_t worker_index) {
    // 在线程开始时，设置其TLS索引。
    tls_worker_index = worker_index;
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
    // 在线程退出前，重置其TLS索引为无效值。
    tls_worker_index = std::numeric_limits<std::size_t>::max();
}
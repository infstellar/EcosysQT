#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

// 轻量级线程池，用于并发提交任务并等待完成
class ThreadPool {
public:
    // 构造函数：启动指定数量的工作线程
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    // 提交一个无参数任务到队列
    void submit(std::function<void()> task);

    // 阻塞直到所有已提交的任务执行完毕
    void wait_for_completion();

    // 请求关闭线程池：唤醒所有线程并等待退出
    void shutdown();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_task_;
    std::condition_variable cv_completed_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> outstanding_{0};
};

#endif // THREAD_POOL_H
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <deque>
#include <atomic>
#include <cstddef>

// 轻量级线程池，用于并发提交任务并等待完成
class ThreadPool {
public:
    // 构造函数：启动指定数量的工作线程
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    // 提交一个无参数任务到队列
    void submit(std::function<void()> task);

    // 轻重任务分别提交（单队列优先级：heavy 使用 push_front，light 使用 push_back）
    void submit_light(std::function<void()> task);
    void submit_heavy(std::function<void()> task);

    /**
     * @brief 批量提交一组任务到队列，减少互斥锁竞争。
     *
     * 该接口一次性将多个任务放入内部队列，只需获取一次锁，显著降低
     * 高频小任务提交时的调度开销。
     */
    void submit_bulk(std::vector<std::function<void()>> tasks);
    void submit_bulk_light(std::vector<std::function<void()>> tasks);
    void submit_bulk_heavy(std::vector<std::function<void()>> tasks);

    // 阻塞直到所有已提交的任务执行完毕
    void wait_for_completion();

    // 请求关闭线程池：唤醒所有线程并等待退出
    void shutdown();

    /**
     * @brief 获取线程池中的工作线程数量。
     *
     * @return std::size_t 工作线程的数量。如果线程池未初始化或已关闭，则返回1，以确保安全（例如，避免除以零）。
     */
    std::size_t worker_count() const;

    /**
     * @brief 获取当前线程的工作索引。
     *
     * @return std::size_t 如果当前线程是线程池的一个工作线程，则返回其索引（从0开始）。
     *         如果当前线程不是线程池的工作线程（例如，主线程），则返回 `std::numeric_limits<std::size_t>::max()`。
     */
    static std::size_t current_worker_index();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 工作线程的循环函数，每个线程执行此函数直到线程池关闭。
    void worker_loop(std::size_t worker_index);

    std::vector<std::thread> workers_;
    enum class TaskPriority { Light, Heavy };
    struct Task {
        std::function<void()> fn;
        TaskPriority priority{TaskPriority::Light};
    };
    std::deque<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_task_;
    std::condition_variable cv_completed_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::size_t> outstanding_{0};
};

#endif // THREAD_POOL_H
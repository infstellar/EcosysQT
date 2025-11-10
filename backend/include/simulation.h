#ifndef SIMULATION_H
#define SIMULATION_H

#include "ecosystem.h"
#include "utils.h" // 包含 EcosystemStateData 的定义
#include "thread_pool.h" // 线程池并发工具
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <map>


// --- SimulationEngine Class ---
class SimulationEngine {
public:
    SimulationEngine(const EcosystemConfig& config);
    ~SimulationEngine();

    void start();
    void pause();
    void resume();
    void stop();
    void reset(const EcosystemConfig& new_config);
    void step();

    void set_speed(double speed);
    EcosystemStateData get_data() const;
    void update_config(const EcosystemConfig& new_config);

    bool is_running() const;
    bool is_paused() const;

private:
    void simulation_loop();
    void update_ecosystem();

    EcosystemConfig config;
    std::unique_ptr<EcosystemState> ecosystem;
    // 并发线程池（阶段 0：基础设施）
    std::unique_ptr<ThreadPool> thread_pool;

    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::atomic<double> simulation_speed;
    int target_fps;

    /**
     * @brief 双缓冲核心：使用 std::atomic_load/store 操作共享指针快照。
     * 模拟线程发布最新帧，GUI 线程以无锁方式读取稳定的可见数据。
     */
    std::shared_ptr<EcosystemStateData> m_visible_data;

    std::unique_ptr<std::thread> simulation_thread;
    std::atomic<bool> stop_event;
};

// --- SimulationController Class ---
class SimulationController {
public:
    SimulationController(const EcosystemConfig& config);

    void start();
    void pause();
    void resume();
    void stop();
    void reset(const EcosystemConfig& config);
    void step();

    void set_speed(double speed);
    EcosystemStateData get_data() const;
    void update_config(const EcosystemConfig& config);

    bool is_running() const;
    bool is_paused() const;

private:
    std::unique_ptr<SimulationEngine> engine;
};

#endif // SIMULATION_H
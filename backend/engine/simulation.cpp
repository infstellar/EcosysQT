#include "simulation.h"
#include "tracy/Tracy.hpp"
#include "high_resolution_timer.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <spdlog/spdlog.h>
// --- SimulationEngine Implementation ---

SimulationEngine::SimulationEngine(const EcosystemConfig& config)
        : config(config),
            ecosystem(std::make_unique<EcosystemState>(config)),
            thread_pool(std::make_unique<ThreadPool>(0)), // 初始化线程池，0代表自动根据硬件选择合适的线程数
            running(false),
            paused(false),
            target_fps(30),
            stop_event(false),
            // 初始化 TPS 统计成员
        m_current_tps(0.0),
        m_tps_frame_counter(0),
        m_tps_last_update_time(std::chrono::steady_clock::now()) {
    // 创建一个初始快照，确保 GUI 在线程启动前也能安全读取数据。
    auto initial_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
    initial_snapshot->current_tps = m_current_tps.load(std::memory_order_relaxed);
    std::atomic_store(&m_visible_data, initial_snapshot);
}

SimulationEngine::~SimulationEngine() {
    stop();
}

void SimulationEngine::start() {
    if (running) {
        return;
    }
    running = true;
    paused = false;
    stop_event = false;

    simulation_thread = std::make_unique<std::thread>(&SimulationEngine::simulation_loop, this); // 开始模拟循环
}

void SimulationEngine::pause() {
    paused = true;
}

void SimulationEngine::resume() {
    paused = false;
}

void SimulationEngine::stop() {
    if (!running) {
        return;
    }
    stop_event = true;
    if (simulation_thread && simulation_thread->joinable()) {
        simulation_thread->join();
    }
    running = false;
    paused = false;
}

void SimulationEngine::reset(const EcosystemConfig& new_config) {
    bool was_running = is_running();
    stop();
    {
    std::lock_guard<std::mutex> lock(m_ecosystem_mutex);
    config = new_config;
    ecosystem->reset(new_config);
    auto new_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
    new_snapshot->current_tps = m_current_tps.load(std::memory_order_relaxed);
    std::atomic_store(&m_visible_data, new_snapshot);
    }
    if (was_running) {
        start();
    }
}

void SimulationEngine::step() {
    if (running) {
        return; // Cannot step while simulation is running automatically
    }
    std::shared_ptr<EcosystemStateData> new_snapshot;
    {
        std::lock_guard<std::mutex> lock(m_ecosystem_mutex);
        update_ecosystem();
        new_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
        new_snapshot->current_tps = m_current_tps.load(std::memory_order_relaxed);
    }
    std::atomic_store(&m_visible_data, new_snapshot);
}

std::shared_ptr<EcosystemStateData> SimulationEngine::get_data() const {
    return std::atomic_load(&m_visible_data);
}

void SimulationEngine::update_config(const EcosystemConfig& new_config) {
    std::lock_guard<std::mutex> lock(m_ecosystem_mutex);
    config = new_config;
    // Note: This matches Python behavior, only updating the config object.
    // The ecosystem itself is not reset here.
}
// --- 新增：实现设置目标FPS的函数 ---
void SimulationEngine::set_target_fps(int fps) {
    // 限制FPS在合理范围内，例如 1 到 200
    this->target_fps = std::clamp(fps, 1, 2000);
}
bool SimulationEngine::is_running() const {
    return running;
}

bool SimulationEngine::is_paused() const {
    return paused;
}

void SimulationEngine::simulation_loop() {
    // Raise timer resolution for the duration of the simulation loop on Windows.
    #ifdef _WIN32
    HighResolutionTimer _hrt(1);
    #endif

    try {
        while (!stop_event) {
            ZoneScoped;
            const auto frame_start = std::chrono::steady_clock::now();
            if (!paused) {
                std::shared_ptr<EcosystemStateData> new_snapshot;
                {
                    ZoneScopedN("Update Frame");
                    std::lock_guard<std::mutex> lock(m_ecosystem_mutex);
                    update_ecosystem();
                }
                {
                    ZoneScopedN("Create Snapshot");
                    const int ts = ecosystem->clock().time_step();
                    if (ts % 30 == 0) {
                        new_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
                        new_snapshot->current_tps = m_current_tps.load(std::memory_order_relaxed);
                        std::atomic_store(&m_visible_data, new_snapshot);
                    }
                }
            }

            {
                ZoneScopedN("Sleep");
                const auto frame_end = std::chrono::steady_clock::now();
                const auto target_frame_duration = std::chrono::duration<double, std::milli>(1000.0 / target_fps);
                const auto frame_elapsed = std::chrono::duration<double, std::milli>(frame_end - frame_start);
                const auto sleep_duration = target_frame_duration - frame_elapsed;
                if (sleep_duration.count() > 15.6) {
                    const auto sleep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_duration);
                    std::this_thread::sleep_for(sleep_ns);
                }
            }

            // TPS 计算
            {
                ZoneScopedN("TPS Calculation");
                m_tps_frame_counter++;
                const auto tps_now = std::chrono::steady_clock::now();
                const auto tps_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tps_now - m_tps_last_update_time).count();
                if (tps_elapsed_ms >= 1000) {
                    const double tps = static_cast<double>(m_tps_frame_counter) / (static_cast<double>(tps_elapsed_ms) / 1000.0);
                    m_current_tps.store(tps, std::memory_order_relaxed);
                    m_tps_last_update_time = tps_now;
                    m_tps_frame_counter = 0;
                }
            }

            FrameMark;
        }
    } catch (const std::exception& e) {
        spdlog::error("[SimulationEngine] Uncaught exception in simulation_loop: {}", e.what());
        // 将 stop_event 标志设为 true，确保线程能安全退出
        stop_event = true;
    } catch (...) {
        spdlog::error("[SimulationEngine] Unknown exception in simulation_loop");
        stop_event = true;
    }

    // 不能直接将 std::atomic<bool> 传给 fmt/spdlog，先读取值
    spdlog::info("[SimulationEngine] simulation_loop exiting (stop_event={})", stop_event.load());
}

void SimulationEngine::update_ecosystem() {
    ZoneScoped;
    // The logic from Python's _update_ecosystem is now encapsulated
    // within the C++ EcosystemState methods.
    
    // 1. Update time (tick-based)
    ecosystem->update_one_tick();

    // 2. 分阶段并发更新
    // 使用线程池来并发处理物种的决策和应用阶段，以提高性能。
    // 如果线程池可用，则将更新任务（决策、应用）分派给线程池中的多个线程。
    // 在每个阶段之间，需要等待所有任务完成，以确保数据一致性。
    // `resolve_interactions` 是一个同步点，它处理所有物种决策后的交互，例如捕食。
    if (thread_pool) {
        {
            ZoneScopedN("Prepare Update");
            ecosystem->prepare_for_update();
        }
        {
            ZoneScopedN("Dispatch Decision");
            ecosystem->dispatch_decision_tasks(*thread_pool);
        }
        {
            ZoneScopedN("Wait Decision");
            thread_pool->wait_for_completion();
        }
        {
            ZoneScopedN("Resolve Interactions");
            ecosystem->resolve_interactions();
        }
        {
            ZoneScopedN("Dispatch Apply");
            ecosystem->dispatch_apply_tasks(*thread_pool);
        }
        {
            ZoneScopedN("Wait Apply");
            thread_pool->wait_for_completion();
        }
    } else {
        // 如果没有可用的线程池，则回退到单线程执行。
        // 这确保了即使在不支持多线程的环境下，模拟也能正确运行。
        ThreadPool fallback_pool(1);
        {
            ZoneScopedN("Prepare Update");
            ecosystem->prepare_for_update();
        }
        {
            ZoneScopedN("Dispatch Decision");
            ecosystem->dispatch_decision_tasks(fallback_pool);
        }
        {
            ZoneScopedN("Wait Decision");
            fallback_pool.wait_for_completion();
        }
        {
            ZoneScopedN("Resolve Interactions");
            ecosystem->resolve_interactions();
        }
        {
            ZoneScopedN("Dispatch Apply");
            ecosystem->dispatch_apply_tasks(fallback_pool);
        }
        {
            ZoneScopedN("Wait Apply");
            fallback_pool.wait_for_completion();
        }
    }

    // 3. 应用注册表变更
    // 在所有物种更新完成后，统一处理出生和死亡等注册表变更。
    // 这可以避免在迭代过程中修改集合，从而简化并发控制。
    {
        ZoneScopedN("Apply Registry Changes");
        ecosystem->apply_registry_changes();
    }

    // 5. Update statistics
    {
        ZoneScopedN("Update Statistics");
        ecosystem->update_statistics();
    }

}

void SimulationEngine::loadSnapshot(const std::shared_ptr<EcosystemStateData>& data) {
    std::lock_guard<std::mutex> lock(m_ecosystem_mutex);
    try {
        spdlog::info("[SimulationEngine] loadSnapshot: begin");

        if (!data) {
            spdlog::warn("[SimulationEngine] loadSnapshot: provided data is null");
            return;
        }

        // 停止模拟循环（外部通常已 stop()，这里再次保证）
        if (running && !stop_event) {
            spdlog::info("[SimulationEngine] loadSnapshot: stopping engine before load");
            stop_event = true;
            // 如果有线程，join 在 stop() 内已经处理；这里只确保状态一致
        }

        // 把数据加载到生态系统状态（会重建 registry / world grid / spatial grid）
        ecosystem->loadFromData(data);

        // 强制一次准备/重建，确保内部索引一致（如果实现了 prepare_for_update，会做清理与 rebuild）
        ecosystem->prepare_for_update();

        // 更新并发布可见快照给 GUI（不可带锁地 publish 指针）
        auto new_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
        new_snapshot->current_tps = m_current_tps.load(std::memory_order_relaxed);
        std::atomic_store(&m_visible_data, new_snapshot);

        spdlog::info("[SimulationEngine] loadSnapshot: published visible snapshot");

        // 注意：这里不自动重启线程。上层（SimulationController / UI）应决定何时 start()
    } catch (const std::exception& e) {
        spdlog::error("[SimulationEngine] loadSnapshot failed: {}", e.what());
    } catch (...) {
        spdlog::error("[SimulationEngine] loadSnapshot failed: unknown exception");
    }
}

// --- SimulationController Implementation ---

SimulationController::SimulationController(const EcosystemConfig& config)
    : engine(std::make_unique<SimulationEngine>(config)) {}

void SimulationController::start() {
    engine->start();
}

void SimulationController::pause() {
    engine->pause();
}

void SimulationController::resume() {
    engine->resume();
}

void SimulationController::stop() {
    engine->stop();
}

void SimulationController::reset(const EcosystemConfig& config) {
    engine->reset(config);
}

void SimulationController::step() {
    engine->step();
}

std::shared_ptr<EcosystemStateData> SimulationController::get_data() {
    return engine->get_data();
}

void SimulationController::update_config(const EcosystemConfig& config) {
    engine->update_config(config);
}

// --- 新增：实现控制器对外的接口 ---
void SimulationController::set_target_fps(int fps) {
    engine->set_target_fps(fps);
}

bool SimulationController::is_running() const {
    return engine->is_running();
}

bool SimulationController::is_paused() const {
    return engine->is_paused();
}

void SimulationController::loadFromSnapshot(const std::shared_ptr<EcosystemStateData>& data) {
    engine->stop();
    // 你需要在 SimulationEngine 里实现 loadSnapshot
    engine->loadSnapshot(data);
}
#include "simulation.h"
#include "tracy/Tracy.hpp"
#include "high_resolution_timer.h"
#include <chrono>
#include <iostream>

// --- SimulationEngine Implementation ---

SimulationEngine::SimulationEngine(const EcosystemConfig& config)
        : config(config),
            ecosystem(std::make_unique<EcosystemState>(config)),
            thread_pool(std::make_unique<ThreadPool>(0)), // 初始化线程池，0代表自动根据硬件选择合适的线程数
            running(false),
            paused(false),
            target_fps(40),
            stop_event(false) {
    // 创建一个初始快照，确保 GUI 在线程启动前也能安全读取数据。
    std::atomic_store(&m_visible_data, std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state()));
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
    config = new_config;
    ecosystem->reset(new_config);
    // 发布重置后的快照，让前端立即看到初始状态。
    std::atomic_store(&m_visible_data, std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state()));
    if (was_running) {
        start();
    }
}

void SimulationEngine::step() {
    if (running) {
        return; // Cannot step while simulation is running automatically
    }
    update_ecosystem();
    // 单步模式下也需要发布最新数据。
    std::atomic_store(&m_visible_data, std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state()));
}

EcosystemStateData SimulationEngine::get_data() const {
    // 原子地获取可见快照指针，确保跨线程读取安全。
    std::shared_ptr<EcosystemStateData> data_ptr = std::atomic_load(&m_visible_data);
    if (!data_ptr) {
        return EcosystemStateData{};
    }
    return *data_ptr;
}

void SimulationEngine::update_config(const EcosystemConfig& new_config) {
    config = new_config;
    // Note: This matches Python behavior, only updating the config object.
    // The ecosystem itself is not reset here.
}

bool SimulationEngine::is_running() const {
    return running;
}

bool SimulationEngine::is_paused() const {
    return paused;
}

void SimulationEngine::simulation_loop() {
    // Raise timer resolution for the duration of the simulation loop on Windows.
    // This improves precision of short sleeps used for frame pacing.
    #ifdef _WIN32
    HighResolutionTimer _hrt(1);
    #endif
    while (!stop_event) {
        ZoneScoped;
        const auto frame_start = std::chrono::steady_clock::now();
        if (!paused) {
            {
                ZoneScopedN("Update Frame");
                update_ecosystem();
            }
            {
                ZoneScopedN("Sent frame to ui");
                // 发布新的模拟帧数据供 GUI 线程读取。
                auto new_data_snapshot = std::make_shared<EcosystemStateData>(ecosystem->get_ecosystem_state());
                std::atomic_store(&m_visible_data, new_data_snapshot);
            }
        }
        {
            ZoneScopedN("Sleep");
            const auto frame_end = std::chrono::steady_clock::now();
            const auto target_frame_duration = std::chrono::duration<double, std::milli>(1000.0 / target_fps);
            const auto frame_elapsed = std::chrono::duration<double, std::milli>(frame_end - frame_start);
            // 打印 frame_elapsed 时间，单位是毫秒

            // Adjust sleep time by subtracting the work duration to keep frame pacing accurate.
            
            const auto sleep_duration = target_frame_duration - frame_elapsed;
            if (sleep_duration.count() > 10) {
                const auto sleep_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sleep_duration);
                // 打印 sleep 时间，单位是毫秒

                std::this_thread::sleep_for(sleep_ns);
            }
        }
        

        FrameMark;
    }
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

EcosystemStateData SimulationController::get_data() const {
    return engine->get_data();
}

void SimulationController::update_config(const EcosystemConfig& config) {
    engine->update_config(config);
}

bool SimulationController::is_running() const {
    return engine->is_running();
}

bool SimulationController::is_paused() const {
    return engine->is_paused();
}
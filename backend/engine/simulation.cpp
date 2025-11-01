#include "simulation.h"
#include <chrono>
#include <iostream>

// --- SimulationEngine Implementation ---

SimulationEngine::SimulationEngine(const EcosystemConfig& config)
    : config(config),
      ecosystem(std::make_unique<EcosystemState>(config)),
      running(false),
      paused(false),
      simulation_speed(1.0),
      target_fps(30),
      stop_event(false) {}

SimulationEngine::~SimulationEngine() {
    stop();
}

void SimulationEngine::set_update_callback(UpdateCallback callback) {
    update_callback = callback;
}

void SimulationEngine::set_extinction_callback(ExtinctionCallback callback) {
    extinction_callback = callback;
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
    if (was_running) {
        start();
    }
}

void SimulationEngine::step() {
    if (running) {
        return; // Cannot step while simulation is running automatically
    }
    update_ecosystem();
}

void SimulationEngine::set_speed(double speed) {
    simulation_speed = std::max(0.1, std::min(5.0, speed));
}

EcosystemStateData SimulationEngine::get_data() const {
    // In C++, we return the structured data directly, not a map.
    // The caller can then access its members.
    return ecosystem->get_ecosystem_state();
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
    while (!stop_event) {
        if (!paused) {
            update_ecosystem();

            if (update_callback) {
                update_callback(get_data());
            }

            auto extinct_species = ecosystem->check_extinction();
            if (!extinct_species.empty() && extinction_callback) {
                extinction_callback(extinct_species);
            }
        }

        // Control frame rate
        double sleep_duration_ms = (1000.0 / target_fps) / simulation_speed;
        //TODO 改成基于运行时间+延迟时间的精准控制。
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(sleep_duration_ms)));

    }
}

void SimulationEngine::update_ecosystem() {
    // The logic from Python's _update_ecosystem is now encapsulated
    // within the C++ EcosystemState methods.
    
    // 1. Update all species (includes movement, energy loss, etc.)
    ecosystem->update_species();

    // 2. Handle reproduction
    ecosystem->handle_reproduction();

    // 3. Clean up dead individuals
    ecosystem->cleanup_dead();

    // 4. Update statistics
    ecosystem->update_statistics();

    // 5. Increment time step
    ecosystem->time_step++;
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

void SimulationController::set_speed(double speed) {
    engine->set_speed(speed);
}

EcosystemStateData SimulationController::get_data() const {
    return engine->get_data();
}

void SimulationController::update_config(const EcosystemConfig& config) {
    engine->update_config(config);
}

void SimulationController::set_callbacks(UpdateCallback update_cb, ExtinctionCallback extinction_cb) {
    engine->set_update_callback(update_cb);
    engine->set_extinction_callback(extinction_cb);
}

bool SimulationController::is_running() const {
    return engine->is_running();
}

bool SimulationController::is_paused() const {
    return engine->is_paused();
}
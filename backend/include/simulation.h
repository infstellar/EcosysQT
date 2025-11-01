#ifndef SIMULATION_H
#define SIMULATION_H

#include "ecosystem.h"
#include "utils.h" // 包含 EcosystemStateData 的定义
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <map>



// --- Callback Function Types ---
using UpdateCallback = std::function<void(const EcosystemStateData&)>;
using ExtinctionCallback = std::function<void(const std::vector<std::string>&)>;

// --- SimulationEngine Class ---
class SimulationEngine {
public:
    SimulationEngine(const EcosystemConfig& config);
    ~SimulationEngine();

    void set_update_callback(UpdateCallback callback);
    void set_extinction_callback(ExtinctionCallback callback);

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

    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::atomic<double> simulation_speed;
    int target_fps;

    UpdateCallback update_callback;
    ExtinctionCallback extinction_callback;

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

    void set_callbacks(UpdateCallback update_cb, ExtinctionCallback extinction_cb);

    bool is_running() const;
    bool is_paused() const;

private:
    std::unique_ptr<SimulationEngine> engine;
};

#endif // SIMULATION_H
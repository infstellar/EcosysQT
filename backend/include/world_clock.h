#pragma once

#include <string>

struct EcosystemConfig;

class WorldClock {
public:
    WorldClock() = default;
    explicit WorldClock(const EcosystemConfig* config);

    void attach_config(const EcosystemConfig* config) noexcept;

    void advance_tick() noexcept;
    void reset() noexcept;
    void set_time_step(int time_step) noexcept;

    int time_step() const noexcept { return m_time_step; }

    int current_day() const;
    int current_year() const;
    int current_quadrum() const;
    int current_hour() const;
    int current_minute() const;
    std::string current_quadrum_name() const;

    int days_in_year() const noexcept;

private:
    const EcosystemConfig* m_config{nullptr};
    int m_time_step{0};

    int ticks_per_day() const noexcept;
    int ticks_per_hour() const noexcept;
    int days_per_year() const noexcept;
    int days_per_quadrum() const noexcept;
    int quadrums_per_year() const noexcept;
};

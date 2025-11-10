#include "world_clock.h"

#include "ecosystem.h"

#include <algorithm>
#include <spdlog/spdlog.h>

WorldClock::WorldClock(const EcosystemConfig* config) : m_config(config) {}

void WorldClock::attach_config(const EcosystemConfig* config) noexcept {
    m_config = config;
}

void WorldClock::advance_tick() noexcept {
    ++m_time_step;
}

void WorldClock::reset() noexcept {
    m_time_step = 0;
}

void WorldClock::set_time_step(int time_step) noexcept {
    m_time_step = time_step;
}

int WorldClock::current_day() const {
    const int day_divisor = std::max(1, ticks_per_day());
    return (m_time_step / day_divisor) + 1;
}

int WorldClock::current_year() const {
    const int total_days = current_day() - 1;
    const int days_per_year_value = std::max(1, days_per_year());
    return (total_days / days_per_year_value) + 1;
}

int WorldClock::current_quadrum() const {
    const int day_of_year = ((current_day() - 1) % std::max(1, days_per_year())) + 1;
    const int quadrum_length = std::max(1, days_per_quadrum());
    return ((day_of_year - 1) / quadrum_length) + 1;
}

int WorldClock::current_hour() const {
    const int ticks_in_day = m_time_step % std::max(1, ticks_per_day());
    const int hour_divisor = std::max(1, ticks_per_hour());
    return ticks_in_day / hour_divisor;
}

int WorldClock::current_minute() const {
    const int ticks_in_day = m_time_step % std::max(1, ticks_per_day());
    const int hour_divisor = std::max(1, ticks_per_hour());
    const int ticks_in_hour = ticks_in_day % hour_divisor;
    if (hour_divisor <= 0) {
        return 0;
    }
    return static_cast<int>((static_cast<double>(ticks_in_hour) / static_cast<double>(hour_divisor)) * 60.0);
}

std::string WorldClock::current_quadrum_name() const {
    const int quadrum_index = current_quadrum() - 1;
    if (quadrum_index < 0) {
        if (auto logger = spdlog::get("ecosim")) {
            logger->warn("WorldClock::current_quadrum_name encountered negative index");
        }
        return "Unknown";
    }

    if (quadrums_per_year() == 4) {
        static const char* kQuadrumNames[] = {"Aprimay", "Jugust", "Septober", "Decembery"};
        if (quadrum_index < 4) {
            return kQuadrumNames[quadrum_index];
        }
    }

    return std::string("Q") + std::to_string(quadrum_index + 1);
}

int WorldClock::ticks_per_day() const noexcept {
    return m_config ? m_config->ticks_per_day : 0;
}

int WorldClock::ticks_per_hour() const noexcept {
    return m_config ? m_config->ticks_per_hour : 0;
}

int WorldClock::days_per_year() const noexcept {
    return m_config ? m_config->days_per_year : 0;
}

int WorldClock::days_per_quadrum() const noexcept {
    return m_config ? m_config->days_per_quadrum : 0;
}

int WorldClock::quadrums_per_year() const noexcept {
    return m_config ? m_config->quadrums_per_year : 0;
}

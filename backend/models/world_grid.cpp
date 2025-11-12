#include "world_grid.h"

#include "thing_base.h"
#include "thread_pool.h"
#include "world_clock.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

WorldGrid::WorldGrid(int width, int height) {
    resize(width, height);
}

void WorldGrid::resize(int width, int height) {
    const int new_width = std::max(0, width);
    const int new_height = std::max(0, height);
    const std::size_t expected_size = static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height);
    if (m_width != new_width || m_height != new_height || m_tiles.size() != expected_size) {
        m_tiles.assign(expected_size, Tile{});
    }
    m_width = new_width;
    m_height = new_height;
}

void WorldGrid::clear_things() {
    for (auto& tile : m_tiles) {
        tile.things.clear();
    }
}

bool WorldGrid::is_valid_coord(int x, int y) const {
    return x >= 0 && x < m_width && y >= 0 && y < m_height;
}

std::size_t WorldGrid::get_index(int x, int y) const {
    if (!is_valid_coord(x, y)) {
        throw std::out_of_range("Grid coordinate out of range");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
}

Tile& WorldGrid::get_tile(int x, int y) {
    return m_tiles[get_index(x, y)];
}

const Tile& WorldGrid::get_tile(int x, int y) const {
    return m_tiles[get_index(x, y)];
}

void WorldGrid::add_thing_to_tile(ThingBase* thing) {
    if (!thing) {
        return;
    }
    if (!is_valid_coord(thing->m_grid_x, thing->m_grid_y)) {
        throw std::out_of_range("Thing grid coordinate out of range");
    }
    Tile& tile = get_tile(thing->m_grid_x, thing->m_grid_y);
    tile.things.push_back(thing);
}

void WorldGrid::remove_thing_from_tile(ThingBase& thing) {
    if (!is_valid_coord(thing.m_grid_x, thing.m_grid_y)) {
        return;
    }
    Tile& tile = get_tile(thing.m_grid_x, thing.m_grid_y);
    auto it = std::remove(tile.things.begin(), tile.things.end(), &thing);
    if (it != tile.things.end()) {
        tile.things.erase(it, tile.things.end());
    }
}

std::vector<std::shared_ptr<ThingBase>> WorldGrid::get_nearby_things_broad(const Position& center, double radius) const {
    std::vector<std::shared_ptr<ThingBase>> nearby;
    if (radius < 0.0 || m_width <= 0 || m_height <= 0) {
        return nearby;
    }

    const double radius_sq = radius * radius;
    const int min_x = std::clamp(static_cast<int>(std::floor(center.x - radius)), 0, m_width - 1);
    const int max_x = std::clamp(static_cast<int>(std::floor(center.x + radius)), 0, m_width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor(center.y - radius)), 0, m_height - 1);
    const int max_y = std::clamp(static_cast<int>(std::floor(center.y + radius)), 0, m_height - 1);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Tile& tile = get_tile(x, y);
            for (ThingBase* thing_ptr : tile.things) {
                if (!thing_ptr || !thing_ptr->alive) {
                    continue;
                }

                const double dx = thing_ptr->position.x - center.x;
                const double dy = thing_ptr->position.y - center.y;
                if ((dx * dx + dy * dy) <= radius_sq) {
                    nearby.push_back(thing_ptr->shared_from_this());
                }
            }
        }
    }

    return nearby;
}

void WorldGrid::initialize_all_tile_states(const WorldClock& clock) {
    if (m_tiles.empty()) {
        return;
    }

    for (auto& tile : m_tiles) {
        update_tile_state(tile, clock);
    }
}

void WorldGrid::update_tile_local_time(Tile& tile, const WorldClock& clock) {
    const int global_hour = clock.current_hour();
    const double raw_offset = tile.longitude / 15.0;
    const int hour_offset = static_cast<int>(std::lround(raw_offset));

    int local_hour = global_hour + hour_offset;
    local_hour %= 24;
    if (local_hour < 0) {
        local_hour += 24;
    }

    tile.local_hour = local_hour;
}

void WorldGrid::update_tile_weather(Tile& tile, const WorldClock& clock) {
    double base_temperature = 15.0;
    double seasonal_amplitude = 10.0;
    double diurnal_amplitude = 6.0;

    switch (tile.biome) {
    case BiomeType::Temperate:
        base_temperature = 15.0;
        seasonal_amplitude = 12.0;
        diurnal_amplitude = 6.0;
        break;
    case BiomeType::Tropical:
        base_temperature = 28.0;
        seasonal_amplitude = 4.0;
        diurnal_amplitude = 4.0;
        break;
    case BiomeType::Frigid:
        base_temperature = -5.0;
        seasonal_amplitude = 16.0;
        diurnal_amplitude = 5.0;
        break;
    case BiomeType::Polar:
        base_temperature = -18.0;
        seasonal_amplitude = 20.0;
        diurnal_amplitude = 3.0;
        break;
    }

    const int days_per_year = std::max(1, clock.days_in_year());
    const int day_of_year = ((clock.current_day() - 1) % days_per_year);
    constexpr double two_pi = 6.28318530717958647692;
    const double seasonal_phase = static_cast<double>(day_of_year) / static_cast<double>(days_per_year);
    const double seasonal_offset = std::cos(seasonal_phase * two_pi);

    const double diurnal_phase = static_cast<double>(tile.local_hour) / 24.0;
    const double diurnal_offset = std::cos((diurnal_phase - 0.5) * two_pi);

    tile.temperature = base_temperature + seasonal_amplitude * seasonal_offset + diurnal_amplitude * diurnal_offset;
}

void WorldGrid::update_tile_state(Tile& tile, const WorldClock& clock) {
    update_tile_local_time(tile, clock);
    update_tile_weather(tile, clock);
    update_tile_brightness(tile, clock);
}

void WorldGrid::update_tile_brightness(Tile& tile, const WorldClock& clock) {
    constexpr double kMinBrightness = 0.1;
    constexpr double kMaxBrightness = 1.0;
    constexpr double kTwilightStartDeg = 0.0;
    constexpr double kTwilightEndDeg = -18.0;
    constexpr double kPi = 3.14159265358979323846;

    const double axial_tilt_rad = m_axial_tilt_deg * (kPi / 180.0);
    const double latitude_rad = tile.latitude * (kPi / 180.0);

    const int days_per_year = std::max(1, clock.days_in_year());
    const int day_of_year = (clock.current_day() - 1) % days_per_year;

    const double global_time_hours = static_cast<double>(clock.current_hour()) +
                                    static_cast<double>(clock.current_minute()) / 60.0;
    double local_time_hours = global_time_hours + (tile.longitude / 15.0);
    local_time_hours = std::fmod(local_time_hours, 24.0);
    if (local_time_hours < 0.0) {
        local_time_hours += 24.0;
    }

    const double seasonal_angle = (2.0 * kPi * (static_cast<double>(day_of_year) + 10.0)) /
                                  static_cast<double>(days_per_year);
    const double declination_rad = std::asin(-std::sin(axial_tilt_rad) * std::cos(seasonal_angle));

    const double hour_angle_rad = (local_time_hours - 12.0) * 15.0 * (kPi / 180.0);

    const double sin_elevation = std::sin(latitude_rad) * std::sin(declination_rad) +
                                 std::cos(latitude_rad) * std::cos(declination_rad) * std::cos(hour_angle_rad);
    const double elevation_rad = std::asin(std::clamp(sin_elevation, -1.0, 1.0));
    const double elevation_deg = elevation_rad * (180.0 / kPi);

    double brightness = kMinBrightness;

    if (elevation_deg >= kTwilightStartDeg) {
        brightness = kMaxBrightness;
    } else if (elevation_deg > kTwilightEndDeg) {
        const double twilight_range = kTwilightStartDeg - kTwilightEndDeg;
        double progress = (elevation_deg - kTwilightEndDeg) / twilight_range;
        progress = std::clamp(progress, 0.0, 1.0);
        brightness = kMinBrightness + (kMaxBrightness - kMinBrightness) * progress;
    }

    tile.brightness = std::clamp(brightness, kMinBrightness, kMaxBrightness);
}

void WorldGrid::dispatch_map_update_tasks(ThreadPool& pool, const WorldClock& clock) {
    const std::size_t total_tiles = m_tiles.size();
    if (total_tiles == 0) {
        return;
    }

    const int amortization_ticks = std::max(1, m_map_update_amortization_ticks);
    const int current_tick = clock.time_step();
    int chunk_index = current_tick % amortization_ticks;
    if (chunk_index < 0) {
        chunk_index += amortization_ticks;
    }
    const std::size_t chunk_size = (total_tiles + static_cast<std::size_t>(amortization_ticks) - 1) / static_cast<std::size_t>(amortization_ticks);

    if (chunk_size == 0) {
        return;
    }

    const std::size_t start_index = static_cast<std::size_t>(chunk_index) * chunk_size;
    if (start_index >= total_tiles) {
        return;
    }

    const std::size_t end_index = std::min(total_tiles, start_index + chunk_size);
    if (end_index <= start_index) {
        return;
    }

    constexpr std::size_t task_chunk_size = 4096;
    const std::size_t range_length = end_index - start_index;
    const std::size_t estimated_task_count = (range_length + task_chunk_size - 1) / task_chunk_size;

    std::vector<std::function<void()>> tasks;
    tasks.reserve(estimated_task_count);

    for (std::size_t task_start = start_index; task_start < end_index; task_start += task_chunk_size) {
        const std::size_t task_end = std::min(end_index, task_start + task_chunk_size);
        tasks.push_back([this, &clock, task_start, task_end] {
            for (std::size_t j = task_start; j < task_end; ++j) {
                update_tile_state(m_tiles[j], clock);
            }
        });
    }

    if (!tasks.empty()) {
        pool.submit_bulk_light(std::move(tasks));
    }
}

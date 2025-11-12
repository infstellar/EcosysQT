/*
 * Logging.cpp
 * 封装 spdlog 的集中初始化和关闭
 */

#include "logging.h"

#include <exception>
#include <iostream>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/common.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Logging {

// --- 把上面定义的宏放在这里 ---
#define SPDLOG_ONCE(logger, level, ...) \
    do { \
        static bool s_logged_once = false; \
        if (!s_logged_once) { \
            s_logged_once = true; \
            logger->log(level, __VA_ARGS__); \
        } \
    } while (0)

#define SPDLOG_WARN_ONCE(logger, ...) SPDLOG_ONCE(logger, spdlog::level::warn, __VA_ARGS__)

void init(const std::string& log_file_path, bool log_to_console) {
    if (spdlog::get(MAIN_LOGGER_NAME)) {
        spdlog::set_default_logger(spdlog::get(MAIN_LOGGER_NAME));
        return;
    }

    try {
        spdlog::init_thread_pool(8192, 1);

        std::vector<spdlog::sink_ptr> sinks;
        auto rotating_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file_path, 1024 * 1024 * 5, 3);

        if (log_to_console) {
            auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            sinks.push_back(stdout_sink);
#ifdef NDEBUG
            stdout_sink->set_level(spdlog::level::warn);
#else
            stdout_sink->set_level(spdlog::level::trace);
#endif
        }

        sinks.push_back(rotating_file_sink);

        auto logger = std::make_shared<spdlog::async_logger>(
            MAIN_LOGGER_NAME,
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);

        spdlog::register_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v [@%s:%#]");

#ifdef NDEBUG
        spdlog::set_level(spdlog::level::info);
        rotating_file_sink->set_level(spdlog::level::info);
#else
        spdlog::set_level(spdlog::level::trace);
        rotating_file_sink->set_level(spdlog::level::trace);
#endif

        spdlog::set_default_logger(logger);
    } catch (const std::exception& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

void shutdown() {
    if (auto logger = spdlog::get(MAIN_LOGGER_NAME)) {
        logger->info("Logger shutting down...");
    }
    spdlog::shutdown();
}

} // namespace Logging

#pragma once // 确保头文件只被包含一次

#include "spdlog/spdlog.h" // 宏需要 spdlog 的定义

// --- 辅助宏，用于拼接唯一的变量名 ---
// 不要直接使用它们
#define SPDLOG_DETAIL_PASTE_IMPL(a, b) a##b
#define SPDLOG_DETAIL_PASTE(a, b) SPDLOG_DETAIL_PASTE_IMPL(a, b)
#define SPDLOG_DETAIL_UNIQUE_VAR_NAME SPDLOG_DETAIL_PASTE(s_logged_once_, __LINE__)

// --- 定义通用的 LOG_ONCE 宏 ---
/**
 * @brief 记录一次日志。
 * * 利用静态局部变量和 __LINE__ 宏确保日志在程序生命周期中，
 * 每一个调用点只被执行一次。
 * * @param logger spdlog的logger实例 (e.g., my_logger)
 * @param level 日志级别 (e.g., spdlog::level::warn)
 * @param ... 格式化字符串和参数 (e.g., "Error: {}", error_code)
 */
#define SPDLOG_ONCE(logger, level, ...) \
    do { \
        static bool SPDLOG_DETAIL_UNIQUE_VAR_NAME = false; \
        if (!SPDLOG_DETAIL_UNIQUE_VAR_NAME) { \
            SPDLOG_DETAIL_UNIQUE_VAR_NAME = true; \
            logger->log(level, __VA_ARGS__); \
        } \
    } while (0)

// --- 为常用级别定义的便捷宏 ---
#define SPDLOG_INFO_ONCE(logger, ...)  SPDLOG_ONCE(logger, spdlog::level::info, __VA_ARGS__)
#define SPDLOG_WARN_ONCE(logger, ...)  SPDLOG_ONCE(logger, spdlog::level::warn, __VA_ARGS__)
#define SPDLOG_ERROR_ONCE(logger, ...) SPDLOG_ONCE(logger, spdlog::level::err, __VA_ARGS__)
#define SPDLOG_DEBUG_ONCE(logger, ...) SPDLOG_ONCE(logger, spdlog::level::debug, __VA_ARGS__)
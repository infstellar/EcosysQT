/*
 * Logging.h
 * 封装 spdlog 的集中初始化和关闭
 */

#pragma once

#include <string>

namespace Logging {

// 主 logger 名称供项目复用
const std::string MAIN_LOGGER_NAME = "ecosim";

/**
 * @brief 初始化全局日志系统。
 *
 * 在应用程序启动时调用一次，配置异步日志、控制台 sink 和旋转文件 sink。
 * @param log_file_path 日志文件路径，例如 "logs/ecosim.log"
 * @param log_to_console 是否同时输出到控制台
 */
void init(const std::string& log_file_path = "logs/ecosim.log", bool log_to_console = true);

/**
 * @brief 关闭日志系统，确保所有异步日志刷新到磁盘。
 */
void shutdown();

} // namespace Logging

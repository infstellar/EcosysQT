#pragma once

#include "ecosystem.h"
#include <string>

// 从给定路径加载配置；失败时返回安全默认值。
EcosystemConfig load_map_config_from_yaml(const std::string& yaml_path);
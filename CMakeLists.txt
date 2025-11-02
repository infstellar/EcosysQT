# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置 vcpkg 工具链文件
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

# 项目名称和语言
project(MyQtApp LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 核心 Qt 配置 ---
# 开启 Qt 的自动化工具，它会自动处理 MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找我们需要的库 (通过vcpkg管理)
find_package(Qt5 COMPONENTS 
    Core 
    Gui 
    Widgets 
    REQUIRED
)
find_package(Eigen3 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# 添加头文件搜索路径（后端与前端 UI 接口）
include_directories(
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/ui/include
    ${CMAKE_SOURCE_DIR}/src/ui
)# --- 添加源文件 ---
# 定义可执行文件及其源文件
add_executable(MyQtApp
  main.cpp
  mainwindow.cpp
  mainwindow.h
  mainwindow.ui
  backend/engine/simulation.cpp
  backend/models/ecosystem.cpp
  backend/models/species_base.cpp
  backend/models/animal.cpp
  backend/models/species/grass.cpp
  backend/models/species/cow.cpp
  backend/models/species/tiger.cpp
  backend/models/species_factory.cpp
  backend/models/species_config_provider_yaml.cpp
  # 前端 UI 模块
  src/ui/Widget.cpp
  src/ui/Widget.h
  src/ui/DummyBackend.cpp
  src/ui/DummyBackend.h
  # 资源文件（AUTORCC）
  
  resources/app.qrc)

# --- 链接库 ---
# 将你的可执行文件与所需库链接起来
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    yaml-cpp
)
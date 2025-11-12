# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 设置 vcpkg 工具链文件
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

# 项目名称和语言
project(MyQtApp LANGUAGES CXX)

# --- UI 调试快照编译选项 ---
option(ECOSIM_UI_DEBUG "Enable UI debug snapshots for animal state" ON)

if(ECOSIM_UI_DEBUG)
    message(STATUS "UI Debug Snapshots: ENABLED")
else()
    message(STATUS "UI Debug Snapshots: DISABLED")
endif()

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 核心 Qt 配置 ---
# 开启 Qt 的自动化工具，它会自动处理 MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找我们需要的库 (通过vcpkg管理)
# 改为使用本地子模块提供的 TracyClient，避免 vcpkg 上的 MinGW 构建问题
# 通过缓存变量控制 Tracy 的开关（由 CMakePresets 或命令行传入）。
# 未显式设置时，默认关闭。
if(NOT DEFINED TRACY_ENABLE)
    set(TRACY_ENABLE OFF CACHE BOOL "Enable profiling")
endif()
if(NOT DEFINED TRACY_ON_DEMAND)
    set(TRACY_ON_DEMAND OFF CACHE BOOL "On-demand profiling")
endif()
add_subdirectory(external/tracy)

# 针对 TracyClient 目标的 Windows API 修复
# 允许在不同目录修改目标的链接库（CMP0079）
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()
if(TARGET TracyClient)
    # 现代 Windows API 声明，解决 WSAPoll / pollfd 等未声明问题
    if(WIN32)
        target_compile_definitions(TracyClient PRIVATE _WIN32_WINNT=0x0601)
        target_link_libraries(TracyClient PRIVATE ws2_32 dbghelp advapi32 ole32)
    endif()
    # MinGW 需要显式开启 SSE 以修复 '__get_cpuid' 未声明
    if(MINGW)
        target_compile_options(TracyClient PRIVATE -msse)
    endif()
else()
    message(WARNING "TracyClient 目标未找到，请确认 external/tracy 已正确加入并生成该目标")
endif()

find_package(Qt5 COMPONENTS Core Gui Widgets Multimedia REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ---!! 在这里添加新的修复块 !! ---
# 修复 Tracy 在 MinGW 8.1 上的编译错误
# 我们必须在 tracy 子目录被添加 *之前* 设置这些
# 编译选项，以便 'TracyClient' 目标能继承它们。
if(MINGW)
    # 1. 修复 '__get_cpuid' was not declared (需要 SSE 指令集)
    add_compile_options(-msse) 
    # 2. 修复 'RelationProcessorDie', 'WSAPoll', 'pollfd' was not declared (需要 Windows 7+ API)
    add_compile_definitions(-D_WIN32_WINNT=0x0601)
endif()
# ---!! 修复块结束 !! ---

#add_subdirectory(external/tracy)

# ---!! 在这里添加新的修复块 !! ---
#
# 修复 Tracy 在 MinGW 8.1 上的编译错误
# 我们使用 set_property 在 'TracyClient' 目标上附加所需的
# 编译定义和链接库，以解决作用域问题。
#
if(MINGW)
    # 1. 修复 pollfd, WSAPoll, RelationProcessorDie, TraceSetInformation 等
    #    通过定义 _WIN32_WINNT=0x0601 (Windows 7) 来解决未声明的错误
    #set_property(TARGET TracyClient APPEND PROPERTY
        ##)

    # 2. 修复 '__get_cpuid' 未声明的错误 (MinGW 8.1 需要手动开启 -msse)
    ##

    # 3. 链接 TracyClient 目标自身需要的 Windows 库 (ws2_32, dbghelp 等)
    #set_property(TARGET TracyClient APPEND PROPERTY
        #INTERFACE_LINK_LIBRARIES "$<$<BOOL:${MINGW}>:ws2_32;dbghelp;advapi32;ole32>"
    #)#
endif()
# ---!! 修复块结束 !! ---

# 添加头文件搜索路径（后端与前端 UI 接口）
include_directories(
    ${CMAKE_SOURCE_DIR}/backend/include
    ${CMAKE_SOURCE_DIR}/src/ui
)# --- 添加源文件 ---
# 定义可执行文件及其源文件
add_executable(MyQtApp 
    main.cpp
    backend/engine/simulation.cpp
    backend/engine/thread_pool.cpp
    backend/models/ecosystem.cpp
    backend/models/population_manager.cpp
    backend/models/world_grid.cpp
    backend/models/world_clock.cpp
    backend/models/interaction_resolver.cpp
    backend/models/spatial_grid.cpp
    backend/models/races_registry.cpp
    backend/models/species_statistics.cpp
    backend/models/race_base.cpp
    backend/models/thing_base.cpp
    backend/models/producer.cpp
    backend/models/animal.cpp
    backend/models/animal_behavior.cpp
    backend/models/bt_actions.cpp
    backend/models/race_factory.cpp
    backend/models/thing_factory.cpp
    backend/models/species_config_provider_yaml.cpp
    backend/models/map_config_loader.cpp
    backend/engine/logging.cpp
    # 前端 UI 模块
    src/ui/Widget.cpp
    src/ui/Widget.h
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/StartScreenWidget.h
    src/ui/StartScreenWidget.cpp
    src/ui/CameraController.h
    src/ui/CameraController.cpp
    src/ui/SimulationRenderer.h
    src/ui/SimulationRenderer.cpp
    # 资源文件（AUTORCC）

    resources/app.qrc)

# --- 链接库 ---
# 将你的可执行文件与所需库链接起来
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Multimedia
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    TracyClient # <--- 链接本地 tracy 客户端
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    $<$<BOOL:${TRACY_ENABLE}>:TRACY_ENABLE> # 通过变量控制启用
    $<$<BOOL:${TRACY_ON_DEMAND}>:TRACY_ON_DEMAND>
    NOMINMAX
    $<$<BOOL:${ECOSIM_UI_DEBUG}>:ECOSIM_ENABLE_UI_DEBUG> # 
)

# 在 Windows 上为高精度定时器链接 winmm
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
    if (MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
    endif()
endif()
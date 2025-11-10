# EcosysQT - 使用 vcpkg 构建指南

本项目现在使用 vcpkg 来管理第三方库依赖。

## 快速开始

### 自动安装 (推荐)

运行提供的安装脚本之一：

**PowerShell (推荐):**
```powershell
.\setup_vcpkg.ps1
```

**批处理文件:**
```cmd
setup_vcpkg.bat
```

这些脚本会：
1. 检查现有的 vcpkg 安装
2. 如果需要，提供多种安装位置选择：
   - **C:\vcpkg** - 系统级安装（需要管理员权限）
   - **D:\vcpkg** - D盘安装（如果存在）
   - **用户目录** - `%USERPROFILE%\vcpkg` （推荐，无需管理员权限）
   - **自定义路径** - 您指定的任何位置
3. 设置 VCPKG_ROOT 环境变量
4. 安装项目所需的所有依赖

> **推荐**: 选择用户目录安装，这样不需要管理员权限，也不会占用C盘空间。

### 手动安装

如果您更喜欢手动设置：

1. **安装 vcpkg**
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **设置环境变量**
   ```bash
   # 设置 VCPKG_ROOT 环境变量指向 vcpkg 安装目录
   set VCPKG_ROOT=C:\vcpkg
   ```

3. **安装 CMake 和 Ninja**
   - CMake 3.16 或更高版本
   - Ninja 构建系统

## 构建项目

### 方法 1: 使用 CMake Presets (推荐)

```bash
# 配置项目 (Debug)
cmake --preset default

# 构建项目
cmake --build --preset default

# 或者配置和构建 Release 版本
cmake --preset release
cmake --build --preset release
```

### 方法 2: 手动配置

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

# 构建项目
cmake --build .
```

## vcpkg 安装位置说明

vcpkg **不需要**安装到C盘根目录！您可以选择以下任何位置：

### 推荐安装位置

1. **用户目录** (推荐)
   - 路径: `%USERPROFILE%\vcpkg` (例如: `C:\Users\YourName\vcpkg`)
   - 优点: 无需管理员权限，用户独享，不影响系统
   - 缺点: 每个用户需要单独安装

2. **其他盘符**
   - 路径: `D:\vcpkg`, `E:\tools\vcpkg` 等
   - 优点: 节省C盘空间，易于管理
   - 缺点: 需要确保盘符存在

3. **项目相对路径**
   - 路径: `.\vcpkg` (项目目录下)
   - 优点: 项目自包含
   - 缺点: 每个项目都需要单独的vcpkg副本

### 不推荐的位置

- **C盘根目录** (`C:\vcpkg`)
  - 需要管理员权限
  - 占用系统盘空间
  - 可能与系统文件冲突

### 环境变量设置

无论安装在哪里，都需要设置 `VCPKG_ROOT` 环境变量指向vcpkg安装目录。我们的安装脚本会自动处理这个设置。

## 依赖库

项目使用以下第三方库（通过 vcpkg 管理）：

- **Eigen3** - 线性代数库

这些依赖在 `vcpkg.json` 文件中定义，vcpkg 会自动下载和构建。

## 项目结构

```
EcosysQT/
├── vcpkg.json              # vcpkg 依赖清单
├── CMakePresets.json       # CMake 预设配置
├── CMakeLists.txt          # CMake 构建配置
├── backend/                # 后端代码
│   ├── include/           # 头文件
│   └── models/            # 模型实现
├── main.cpp               # 主程序入口
├── mainwindow.cpp         # 主窗口实现
├── mainwindow.h           # 主窗口头文件
└── mainwindow.ui          # UI 设计文件
```

## 故障排除

1. **找不到 Eigen3**: 确保 vcpkg 已正确安装 Eigen3 包

## 清理构建

```bash
# 删除构建目录
rmdir /s build
rmdir /s build-release

# 重新配置和构建
cmake --preset default
cmake --build --preset default
```
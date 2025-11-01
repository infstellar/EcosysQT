@echo off
echo 正在构建 EcosysQT 项目...

REM 检查 VCPKG_ROOT 是否设置
if not defined VCPKG_ROOT (
    echo 错误: VCPKG_ROOT 环境变量未设置
    echo 请先运行 setup_vcpkg.bat 或 setup_vcpkg.ps1
    pause
    exit /b 1
)

REM 检查 vcpkg.exe 是否存在
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo 错误: 在 %VCPKG_ROOT% 找不到 vcpkg.exe
    pause
    exit /b 1
)

echo 使用 vcpkg 路径: %VCPKG_ROOT%

REM 创建构建目录
if not exist build mkdir build

REM 配置项目
echo 正在配置项目...
cmake --preset default
if %ERRORLEVEL% neq 0 (
    echo 配置失败！
    pause
    exit /b 1
)

REM 构建项目
echo 正在构建项目...
cmake --build --preset default
if %ERRORLEVEL% neq 0 (
    echo 构建失败！
    pause
    exit /b 1
)

echo.
echo 构建成功！
echo 可执行文件位于: build\Debug\MyQtApp.exe
echo.
pause
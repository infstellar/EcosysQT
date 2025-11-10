@echo off
echo 正在设置 vcpkg 环境...

REM 检查是否已经设置了 VCPKG_ROOT
if defined VCPKG_ROOT (
    echo VCPKG_ROOT 已设置为: %VCPKG_ROOT%
    goto :install_deps
)

REM 检查常见的 vcpkg 安装位置
set "POSSIBLE_PATHS=C:\vcpkg C:\tools\vcpkg C:\dev\vcpkg D:\vcpkg D:\tools\vcpkg D:\dev\vcpkg %USERPROFILE%\vcpkg %USERPROFILE%\tools\vcpkg"

for %%p in (%POSSIBLE_PATHS%) do (
    if exist "%%p\vcpkg.exe" (
        echo 找到 vcpkg 安装在: %%p
        set "VCPKG_ROOT=%%p"
        goto :install_deps
    )
)

echo 未找到 vcpkg 安装。
echo 请选择一个安装选项:
echo 1. 安装到 C:\vcpkg (需要管理员权限)
echo 2. 安装到 D:\vcpkg (如果D盘存在)
echo 3. 安装到用户目录 %USERPROFILE%\vcpkg (推荐)
echo 4. 自定义安装路径
echo 5. 手动指定现有 vcpkg 路径
echo 6. 退出
set /p choice="请输入选择 (1-6): "

if "%choice%"=="1" goto :install_c
if "%choice%"=="2" goto :install_d
if "%choice%"=="3" goto :install_user
if "%choice%"=="4" goto :install_custom
if "%choice%"=="5" goto :manual_path
if "%choice%"=="6" goto :end

:install_c
echo 正在下载并安装 vcpkg 到 C:\vcpkg...
if not exist "C:\" (
    echo 错误: 无法访问C盘根目录，可能需要管理员权限
    goto :end
)
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
call bootstrap-vcpkg.bat
set "VCPKG_ROOT=C:\vcpkg"
goto :install_deps

:install_d
echo 正在下载并安装 vcpkg 到 D:\vcpkg...
if not exist "D:\" (
    echo 错误: D盘不存在或无法访问
    goto :end
)
cd D:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
call bootstrap-vcpkg.bat
set "VCPKG_ROOT=D:\vcpkg"
goto :install_deps

:install_user
echo 正在下载并安装 vcpkg 到 %USERPROFILE%\vcpkg...
cd %USERPROFILE%
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
call bootstrap-vcpkg.bat
set "VCPKG_ROOT=%USERPROFILE%\vcpkg"
goto :install_deps

:install_custom
set /p custom_path="请输入安装路径 (例如: E:\tools\vcpkg): "
for %%i in ("%custom_path%") do set "parent_dir=%%~dpi"
if not exist "%parent_dir%" (
    echo 错误: 父目录 %parent_dir% 不存在
    goto :end
)
echo 正在下载并安装 vcpkg 到 %custom_path%...
cd "%parent_dir%"
git clone https://github.com/Microsoft/vcpkg.git vcpkg
cd vcpkg
call bootstrap-vcpkg.bat
set "VCPKG_ROOT=%custom_path%"
goto :install_deps

:manual_path
set /p VCPKG_ROOT="请输入 vcpkg 安装路径: "
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo 错误: 在指定路径找不到 vcpkg.exe
    goto :end
)

:install_deps
echo 设置环境变量...
setx VCPKG_ROOT "%VCPKG_ROOT%"

echo 正在安装项目依赖...
"%VCPKG_ROOT%\vcpkg.exe" install qt5-base:x64-windows
"%VCPKG_ROOT%\vcpkg.exe" install qt5-widgets:x64-windows
"%VCPKG_ROOT%\vcpkg.exe" install eigen3:x64-windows

echo.
echo vcpkg 设置完成！
echo VCPKG_ROOT 已设置为: %VCPKG_ROOT%
echo.
echo 现在您可以使用以下命令构建项目:
echo cmake --preset default
echo cmake --build --preset default
echo.

:end
pause
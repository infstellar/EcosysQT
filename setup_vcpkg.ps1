# vcpkg 设置脚本
Write-Host "正在设置 vcpkg 环境..." -ForegroundColor Green

# 检查是否已经设置了 VCPKG_ROOT
$vcpkgRoot = $env:VCPKG_ROOT
if ($vcpkgRoot -and (Test-Path "$vcpkgRoot\vcpkg.exe")) {
    Write-Host "VCPKG_ROOT 已设置为: $vcpkgRoot" -ForegroundColor Yellow
    $useExisting = Read-Host "是否使用现有的 vcpkg 安装? (y/n)"
    if ($useExisting -eq 'y' -or $useExisting -eq 'Y') {
        & "$vcpkgRoot\vcpkg.exe" install --triplet x64-windows
        exit 0
    }
}

# 检查常见的 vcpkg 安装位置
$possiblePaths = @(
    "C:\vcpkg", 
    "C:\tools\vcpkg", 
    "C:\dev\vcpkg",
    "D:\vcpkg",
    "D:\tools\vcpkg",
    "D:\dev\vcpkg",
    "$env:USERPROFILE\vcpkg",
    "$env:USERPROFILE\tools\vcpkg"
)

foreach ($path in $possiblePaths) {
    if (Test-Path "$path\vcpkg.exe") {
        Write-Host "找到 vcpkg 安装在: $path" -ForegroundColor Green
        $vcpkgRoot = $path
        break
    }
}

if (-not $vcpkgRoot) {
    Write-Host "未找到 vcpkg 安装。" -ForegroundColor Red
    Write-Host "请选择一个安装选项:"
    Write-Host "1. 安装到 C:\vcpkg (需要管理员权限)"
    Write-Host "2. 安装到 D:\vcpkg (如果D盘存在)"
    Write-Host "3. 安装到用户目录 $env:USERPROFILE\vcpkg (推荐)"
    Write-Host "4. 自定义安装路径"
    Write-Host "5. 手动指定现有 vcpkg 路径"
    Write-Host "6. 退出"
    
    $choice = Read-Host "请输入选择 (1-6)"
    
    switch ($choice) {
        "1" {
            $installPath = "C:\vcpkg"
            Write-Host "正在下载并安装 vcpkg 到 $installPath..." -ForegroundColor Yellow
            if (-not (Test-Path "C:\")) {
                Write-Host "错误: 无法访问C盘根目录，可能需要管理员权限" -ForegroundColor Red
                exit 1
            }
            Set-Location C:\
            git clone https://github.com/Microsoft/vcpkg.git
            Set-Location vcpkg
            .\bootstrap-vcpkg.bat
            $vcpkgRoot = $installPath
        }
        "2" {
            $installPath = "D:\vcpkg"
            if (-not (Test-Path "D:\")) {
                Write-Host "错误: D盘不存在或无法访问" -ForegroundColor Red
                exit 1
            }
            Write-Host "正在下载并安装 vcpkg 到 $installPath..." -ForegroundColor Yellow
            Set-Location D:\
            git clone https://github.com/Microsoft/vcpkg.git
            Set-Location vcpkg
            .\bootstrap-vcpkg.bat
            $vcpkgRoot = $installPath
        }
        "3" {
            $installPath = "$env:USERPROFILE\vcpkg"
            Write-Host "正在下载并安装 vcpkg 到 $installPath..." -ForegroundColor Yellow
            Set-Location $env:USERPROFILE
            git clone https://github.com/Microsoft/vcpkg.git
            Set-Location vcpkg
            .\bootstrap-vcpkg.bat
            $vcpkgRoot = $installPath
        }
        "4" {
            $installPath = Read-Host "请输入安装路径 (例如: E:\tools\vcpkg)"
            $parentDir = Split-Path $installPath -Parent
            if (-not (Test-Path $parentDir)) {
                Write-Host "错误: 父目录 $parentDir 不存在" -ForegroundColor Red
                exit 1
            }
            Write-Host "正在下载并安装 vcpkg 到 $installPath..." -ForegroundColor Yellow
            Set-Location $parentDir
            git clone https://github.com/Microsoft/vcpkg.git vcpkg
            Set-Location vcpkg
            .\bootstrap-vcpkg.bat
            $vcpkgRoot = $installPath
        }
        "5" {
            $vcpkgRoot = Read-Host "请输入现有 vcpkg 安装路径"
            if (-not (Test-Path "$vcpkgRoot\vcpkg.exe")) {
                Write-Host "错误: 在指定路径找不到 vcpkg.exe" -ForegroundColor Red
                exit 1
            }
        }
        "6" {
            exit 0
        }
        default {
            Write-Host "无效选择" -ForegroundColor Red
            exit 1
        }
    }
}

# 设置环境变量
Write-Host "设置环境变量..." -ForegroundColor Yellow
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, "User")
$env:VCPKG_ROOT = $vcpkgRoot

# 安装项目依赖
Write-Host "正在安装项目依赖..." -ForegroundColor Yellow
& "$vcpkgRoot\vcpkg.exe" install --triplet x64-windows

Write-Host ""
Write-Host "vcpkg 设置完成！" -ForegroundColor Green
Write-Host "VCPKG_ROOT 已设置为: $vcpkgRoot" -ForegroundColor Green
Write-Host ""
Write-Host "现在您可以使用以下命令构建项目:" -ForegroundColor Cyan
Write-Host "cmake --preset default" -ForegroundColor White
Write-Host "cmake --build --preset default" -ForegroundColor White
Write-Host ""

Read-Host "按任意键继续..."
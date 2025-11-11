Param(
    [string]$Preset,
    [string]$Target = "MyQtApp",
    [switch]$ConfigureOnly,
    [switch]$BuildOnly,
    [switch]$Clean,
    [switch]$PromptClean,
    [ValidateSet("skip","selected")]
    [string]$CleanMode,
    [int]$Parallel = [Environment]::ProcessorCount,
    [switch]$Verbose
)

# Detect active Conda environment and exit early to avoid toolchain conflicts
$condaActive = $false
if ($env:CONDA_PREFIX -or $env:CONDA_DEFAULT_ENV -or (($env:CONDA_SHLVL -as [int]) -gt 0)) {
    $condaActive = $true
}
if ($condaActive) {
    Write-Host "检测到 Conda 环境。请先执行 'conda deactivate' 后再运行。" -ForegroundColor Red
    exit 1
}

$ErrorActionPreference = 'Stop'

function Get-ConfigurePresets {
    param([string]$PresetsFilePath)
    if (-not (Test-Path $PresetsFilePath)) {
        throw "CMakePresets.json not found at $PresetsFilePath"
    }
    $json = Get-Content -Raw -LiteralPath $PresetsFilePath | ConvertFrom-Json
    return $json.configurePresets
}

# Read VS Code settings.json and return parsed object (supports // comments)
function Get-VSCodeSettings {
    param([string]$SettingsFilePath)
    if (-not (Test-Path $SettingsFilePath)) { return $null }
    $raw = Get-Content -Raw -LiteralPath $SettingsFilePath
    # Strip line comments safely: lines starting with // or inline // not preceded by ':' (avoid http://)
    $stripped = [regex]::Replace($raw, '(?m)^\s*//.*$', '')
    $stripped = [regex]::Replace($stripped, '(?m)(?<!:)//.*$', '')
    try { return ($stripped | ConvertFrom-Json) } catch { return $null }
}

# Expand ${env:VAR} placeholders inside strings
function Expand-EnvPlaceholders {
    param([string]$Text)
    if (-not $Text) { return $Text }
    return ([regex]::Replace($Text, '\$\{env:([A-Za-z0-9_]+)\}', { param($m) [Environment]::GetEnvironmentVariable($m.Groups[1].Value) }))
}

function Select-PresetInteractively {
    param([array]$Presets)
    Write-Host "Available CMake configure presets:" -ForegroundColor Cyan
    for ($i = 0; $i -lt $Presets.Count; $i++) {
        $p = $Presets[$i]
        $name = $p.name
        $disp = $p.displayName
        Write-Host "  [$($i+1)] $name`t($disp)"
    }
    $choice = Read-Host "Select preset by number (default: 'default')"
    if ([string]::IsNullOrWhiteSpace($choice)) {
        return 'default'
    }
    if (-not ($choice -as [int])) {
        throw "Invalid selection: $choice"
    }
    $idx = [int]$choice - 1
    if ($idx -lt 0 -or $idx -ge $Presets.Count) {
        throw "Selection out of range"
    }
    return $Presets[$idx].name
}

function Get-BinaryDirForPreset {
    param([array]$Presets, [string]$PresetName)
    $p = $Presets | Where-Object { $_.name -eq $PresetName }
    if (-not $p) { throw "Preset '$PresetName' not found" }
    return $p.binaryDir
}

# Expand CMakePresets-style placeholders in paths (e.g., ${sourceDir})
function Expand-CMakePresetPlaceholders {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$SourceDir
    )

    $expanded = $Path
    # ${sourceDir} -> actual source directory
    $expanded = $expanded -replace '\$\{sourceDir\}', [Regex]::Escape($SourceDir)
    # Normalize separators for Windows and resolve to full path if rooted
    $expanded = $expanded -replace '/', '\\'
    return $expanded
}

# Create a link named 'workspace' inside build directory pointing to project root.
function New-WorkspaceLink {
    param(
        [Parameter(Mandatory=$true)][string]$LinkDir,
        [Parameter(Mandatory=$true)][string]$TargetDir,
        [string]$LinkName = 'workspace'
    )

    $linkPath = Join-Path $LinkDir $LinkName
    # If a link exists, remove it; if a non-link exists, warn and skip
    if (Test-Path -LiteralPath $linkPath) {
        try {
            $item = Get-Item -LiteralPath $linkPath -ErrorAction Stop
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                Remove-Item -LiteralPath $linkPath -Force -ErrorAction Stop
            } else {
                Write-Host "Path exists and is not a link: $linkPath (skip)" -ForegroundColor Yellow
                return
            }
        } catch {
            Write-Host "Unable to inspect existing path: $linkPath (skip)" -ForegroundColor Yellow
            return
        }
    }

    # Try symbolic link first, then junction as fallback on Windows
    try {
        New-Item -ItemType SymbolicLink -Path $linkPath -Target $TargetDir -ErrorAction Stop | Out-Null
        Write-Host "Created symbolic link '$LinkName' -> $TargetDir" -ForegroundColor Green
    } catch {
        try {
            New-Item -ItemType Junction -Path $linkPath -Target $TargetDir -ErrorAction Stop | Out-Null
            Write-Host "Created junction '$LinkName' -> $TargetDir" -ForegroundColor Green
        } catch {
            Write-Host ("Failed to create link: {0}" -f $_.Exception.Message) -ForegroundColor Red
        }
    }
}

function Ensure-CMakeInstalled {
    try {
        & cmake --version | Out-Null
    } catch {
        throw "CMake is not available on PATH. Please install CMake or open a Developer PowerShell."
    }
}

function Check-Vcpkg {
    if (-not $env:VCPKG_ROOT) {
        throw "VCPKG_ROOT environment variable is not set. Run setup_vcpkg.ps1 first."
    }
    if (-not (Test-Path (Join-Path $env:VCPKG_ROOT 'vcpkg.exe'))) {
        throw "vcpkg.exe not found in $env:VCPKG_ROOT"
    }
    Write-Host "Using vcpkg path: $($env:VCPKG_ROOT)" -ForegroundColor Green
}

# Interactive selector for cleaning behavior (similar to preset selection)
function Select-CleanModeInteractively {
    param(
        [Parameter(Mandatory=$true)][string]$AbsBinDir,
        [string]$Default = 'skip'
    )

    Write-Host "请选择清理选项:" -ForegroundColor Cyan
    Write-Host "[0] 不清理" -ForegroundColor Gray
    Write-Host ("[1] 清理所选预设的构建目录: {0}" -f $AbsBinDir) -ForegroundColor Yellow
    if ($Default -eq 'selected') {
        Write-Host "默认: 清理" -ForegroundColor Gray
    } else {
        Write-Host "默认: 不清理" -ForegroundColor Gray
    }

    while ($true) {
        $choice = Read-Host "输入序号 (0/1，直接回车选择默认)"
        if ($choice -eq '') { return $Default }
        switch ($choice) {
            '0' { return 'skip' }
            '1' { return 'selected' }
            default { Write-Host "无效选择，请输入 0 或 1。" -ForegroundColor Red }
        }
    }
}

# Main
Ensure-CMakeInstalled
Check-Vcpkg

$presetsPath = Join-Path $PSScriptRoot 'CMakePresets.json'
$presets = Get-ConfigurePresets -PresetsFilePath $presetsPath

# VSCode settings as defaults for preset and PATH
$vsSettingsPath = Join-Path $PSScriptRoot '.vscode/settings.json'
$vsSettings = Get-VSCodeSettings -SettingsFilePath $vsSettingsPath
$presetFromVS = $null
$pathConfigure = $null
$pathBuild = $null
$configureSettings = $null
if ($vsSettings) {
    if ($vsSettings.'cmake.configurePreset') { $presetFromVS = [string]$vsSettings.'cmake.configurePreset' }
    if ($vsSettings.'cmake.configureEnvironment' -and $vsSettings.'cmake.configureEnvironment'.PATH) {
        $pathConfigure = Expand-EnvPlaceholders ([string]$vsSettings.'cmake.configureEnvironment'.PATH)
    }
    if ($vsSettings.'cmake.buildEnvironment' -and $vsSettings.'cmake.buildEnvironment'.PATH) {
        $pathBuild = Expand-EnvPlaceholders ([string]$vsSettings.'cmake.buildEnvironment'.PATH)
    }
    if ($vsSettings.'cmake.configureSettings') {
        $configureSettings = $vsSettings.'cmake.configureSettings'
    }
}

if (-not $Preset) {
    if ($presetFromVS) {
        Write-Host ("Using preset from .vscode/settings.json: {0}" -f $presetFromVS) -ForegroundColor Cyan
        $Preset = $presetFromVS
    } else {
        $Preset = Select-PresetInteractively -Presets $presets
    }
}

Write-Host "Selected preset: $Preset" -ForegroundColor Cyan

# Resolve binary directory for the selected preset (relative paths are rooted to repo)
$binDir = Get-BinaryDirForPreset -Presets $presets -PresetName $Preset
# Expand ${sourceDir} and normalize
$expandedBinDir = Expand-CMakePresetPlaceholders -Path $binDir -SourceDir $PSScriptRoot
$absBinDir = $expandedBinDir
if (-not [System.IO.Path]::IsPathRooted($absBinDir)) {
    $absBinDir = Join-Path $PSScriptRoot $absBinDir
}
$absBinDir = [System.IO.Path]::GetFullPath($absBinDir)

# Cleaning behavior: always interactive selection
$defaultCleanMode = $CleanMode
if (-not $defaultCleanMode) { $defaultCleanMode = 'skip' }
$effectiveCleanMode = Select-CleanModeInteractively -AbsBinDir $absBinDir -Default $defaultCleanMode

switch ($effectiveCleanMode) {
    'selected' {
        if (Test-Path $absBinDir) {
            Write-Host "Cleaning build directory: $absBinDir" -ForegroundColor Yellow
            Remove-Item -LiteralPath $absBinDir -Recurse -Force
        } else {
            Write-Host "No build directory to clean: $absBinDir" -ForegroundColor Yellow
        }
    }
    default {
        Write-Host "Skip cleaning." -ForegroundColor Yellow
    }
}

if (-not $BuildOnly) {
    Write-Host "Configuring project..." -ForegroundColor Cyan
    $originalPATH = $env:Path
    if ($pathConfigure) {
        Write-Host "Injecting PATH from .vscode for configure." -ForegroundColor Green
        $env:Path = $pathConfigure
    }
    $configureArgs = @('--preset', $Preset)
    # Inject cmake.configureSettings as -D variables
    if ($configureSettings) {
        Write-Host "Injecting cmake.configureSettings from .vscode." -ForegroundColor Green
        foreach ($key in $configureSettings.PSObject.Properties.Name) {
            $val = $configureSettings.$key
            if ($null -ne $val) {
                if ($val -is [bool]) {
                    if ($val) { $valStr = 'ON' } else { $valStr = 'OFF' }
                } else {
                    $valStr = [string](Expand-EnvPlaceholders ([string]$val))
                }
                $configureArgs += @('-D', "$key=$valStr")
            }
        }
    }
    if ($Verbose) { $configureArgs += @('--log-level=VERBOSE') }
    & cmake @configureArgs
}

if (-not $ConfigureOnly) {
    Write-Host "Building project..." -ForegroundColor Cyan
    if ($pathBuild) {
        Write-Host "Injecting PATH from .vscode for build." -ForegroundColor Green
        $env:Path = $pathBuild
    } else {
        # Restore original PATH if we changed it for configure
        if ($originalPATH) { $env:Path = $originalPATH }
    }
    $buildArgs = @('--build', '--preset', $Preset, '--parallel', $Parallel)
    if ($Verbose) { $buildArgs += @('--verbose') }
    if ($Target) { $buildArgs += @('--target', $Target) }
    & cmake @buildArgs
}

# After successful build, create a convenience link to the project workspace inside the build directory
if (-not $ConfigureOnly) {
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Build succeeded. Creating workspace link in: $absBinDir" -ForegroundColor Cyan
        New-WorkspaceLink -LinkDir $absBinDir -TargetDir $PSScriptRoot -LinkName 'workspace'
    } else {
        Write-Host "Build failed; skip creating workspace link." -ForegroundColor Yellow
    }
}

Write-Host "Done." -ForegroundColor Green
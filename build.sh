#!/bin/bash
#
# Linux/macOS 版本的 build.ps1
#
# 功能:
# - 解析 CMakePresets_linux.json
# - 读取 .vscode/settings.json 以获取默认预设、PATH 注入和 configureSettings
# - 运行 cmake configure 和 build
# - 支持 headless 模式、清理、交互式预设选择等
#
# 依赖: jq, perl (用于环境变量展开)

# --- 参数默认值 ---
PRESET=""
TARGET="MyQtApp"
CONFIGURE_ONLY=false
BUILD_ONLY=false
CLEAN_MODE="" # 将在后面变为交互式
PARALLEL=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
VERBOSE=false
HEADLESS=false

# --- 脚本设置 ---
set -e # 出错时立即退出
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
PRESETS_PATH="$SCRIPT_DIR/CMakePresets_linux.json"
VSCODE_SETTINGS_PATH="$SCRIPT_DIR/.vscode/settings.json"

# --- 帮助信息 ---
show_help() {
    echo "用法: ./build.sh [选项]"
    echo ""
    echo "选项:"
    echo "  -p <preset>     指定要使用的 CMake 预设 (默认: 从 .vscode/settings.json 读取或交互式选择)"
    echo "  -t <target>     要构建的目标 (默认: $TARGET)"
    echo "  -c              仅运行 CMake configure"
    echo "  -b              仅运行 CMake build (跳过 configure)"
    echo "  --clean         [已弃用] 请使用交互式提示"
    echo "  --parallel <N>  设置并行构建的作业数 (默认: $PARALLEL)"
    echo "  -v, --verbose   启用 CMake 详细日志"
    echo "  -h, --headless  构建 headless 模式 (注入 -DECOSIM_HEADLESS=ON)"
    echo "  --help          显示此帮助信息"
}

# --- 解析命令行参数 ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        -p)
            PRESET="$2"
            shift 2
            ;;
        -t)
            TARGET="$2"
            shift 2
            ;;
        -c)
            CONFIGURE_ONLY=true
            shift
            ;;
        -b)
            BUILD_ONLY=true
            shift
            ;;
        --clean)
            # 忽略，总会提示
            shift
            ;;
        --parallel)
            PARALLEL="$2"
            shift 2
            ;;
        -v | --verbose)
            VERBOSE=true
            shift
            ;;
        -h | --headless)
            HEADLESS=true
            shift
            ;;
        --help)
            show_help
            exit 0
            ;;
        *)
            echo "未知选项: $1"
            show_help
            exit 1
            ;;
    esac
done

# --- 辅助函数 ---

# 检查依赖
ensure_deps() {
    command -v cmake >/dev/null 2>&1 || { echo >&2 "错误: 未找到 'cmake'。请安装 CMake。"; exit 1; }
    command -v jq >/dev/null 2>&1 || { echo >&2 "错误: 未找到 'jq'。请安装 jq (用于解析 JSON)。"; exit 1; }
    command -v perl >/dev/null 2>&1 || { echo >&2 "错误: 未找到 'perl'。请安装 perl (用于展开环境变量)。"; exit 1; }
}

# 检查 Conda
check_conda() {
    if [ -n "$CONDA_PREFIX" ] || [ -n "$CONDA_DEFAULT_ENV" ] || [ "${CONDA_SHLVL:-0}" -gt 0 ]; then
        echo >&2 "检测到 Conda 环境。请先执行 'conda deactivate' 后再运行。"
        exit 1
    fi
}

# 检查 Vcpkg
check_vcpkg() {
    if [ -z "$VCPKG_ROOT" ]; then
        echo >&2 "错误: VCPKG_ROOT 环境变量未设置。请先设置 vcpkg。"
        exit 1
    fi
    if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
        echo >&2 "错误: 在 $VCPKG_ROOT 中未找到 vcpkg 可执行文件"
        exit 1
    fi
    echo -e "\033[0;32m使用 vcpkg 路径: $VCPKG_ROOT\033[0m"
}

# 读取 VSCode 设置 (移除 // 注释)
get_vscode_settings() {
    if [ ! -f "$1" ]; then
        echo "{}"
        return
    fi
    
    # 修复: 增加预处理来清理 JSONC 特性 和 无效空白字符
    # 1. 替换 U+00A0 (non-breaking space) 为- ASCII space
    # 2. 移除 // 注释 (简单的 sed)
    # 3. 移除 /* ... */ 注释 (简单的 sed, 不支持跨行)
    # 4. 移除行尾多余的逗号 (JSONC -> JSON), 例如 "key": "val", } -> "key": "val" }
    # 5. 将清理后的流交给 jq
    <"$1" perl -pe 's/\x{00A0}/ /g' | \
        sed -e 's|//.*||' -e 's|/\*.*\*/||' | \
        sed -e 's/,\s*\]/\]/' -e 's/,\s*\}/}/' | \
        jq -c '.'
}

# 展开 ${env:VAR} 占位符
expand_env_placeholders() {
    local text="$1"
    if [ -z "$text" ]; then
        echo ""
        return
    fi
    # 使用 perl 来模拟 PowerShell 的展开
    echo "$text" | perl -pe 's/\$\{env:([A-Za-z0-9_]+)\}/$ENV{$1}/g'
}

# 交互式选择预设
select_preset_interactively() {
    local presets_json="$1"
    # *** 修复: 将菜单标题重定向到 stderr (>&2) ***
    echo -e "\033[0;36mAvailable CMake configure presets:\033[0m" >&2
    local names=()
    local display_names=()
    while IFS= read -r line; do
        names+=("$(echo "$line" | jq -r '.name')")
        display_names+=("$(echo "$line" | jq -r '.displayName')")
    done < <(echo "$presets_json" | jq -c '.[]')

    for i in "${!names[@]}"; do
        # *** 修复: 将菜单项重定向到 stderr (>&2) ***
        printf "  [%d] %s\t(%s)\n" $((i+1)) "${names[$i]}" "${display_names[$i]}" >&2
    done

    local default_preset="default"
    if [ "$HEADLESS" = true ]; then
        default_preset="headless-debug"
    fi
    
    # *** 修复: 从 /dev/tty 读取，确保在 stdout 被捕获时也能工作 ***
    read -p "Select preset by number (default: '$default_preset'): " choice < /dev/tty
    if [ -z "$choice" ]; then
        choice="$default_preset"
    fi

    if [[ "$choice" =~ ^[0-9]+$ ]]; then
        local idx=$((choice - 1))
        if [ $idx -ge 0 ] && [ $idx -lt ${#names[@]} ]; then
            # *** 成功: 只有最终选择被打印到 stdout ***
            echo "${names[$idx]}"
        else
            echo >&2 "选择超出范围"
            exit 1
        fi
    else
        # 按名称返回
        echo "$choice"
    fi
}

# 获取预设的 binaryDir
get_binary_dir_for_preset() {
    local presets_json="$1"
    local preset_name="$2"
    local bin_dir
    bin_dir=$(echo "$presets_json" | jq -r --arg PRESET_NAME "$preset_name" '.[] | select(.name == $PRESET_NAME) | .binaryDir')
    if [ -z "$bin_dir" ] || [ "$bin_dir" = "null" ]; then
        echo >&2 "错误: 未找到预设 '$preset_name'"
        exit 1
    fi
    echo "$bin_dir"
}

# 展开 CMake 占位符
expand_cmake_preset_placeholders() {
    local path="$1"
    local source_dir="$2"
    local expanded="$path"
    expanded=$(echo "$expanded" | sed "s|\${sourceDir}|$source_dir|g")
    echo "$expanded"
}

# 创建工作区链接
new_workspace_link() {
    local link_dir="$1"
    local target_dir="$2"
    local link_name="${3:-workspace}"
    local link_path="$link_dir/$link_name"

    # -s: 符号链接, -f: 强制 (覆盖), -n: 不要解引用 (如果 $link_path 已是符号链接)
    if ln -sfn "$target_dir" "$link_path"; then
        echo -e "\033[0;32mCreated symbolic link '$link_name' -> $target_dir\033[0m"
    else
        echo -e "\033[0;31mFailed to create link: $link_path\033[0m"
    fi
}

# 交互式选择清理模式
select_clean_mode_interactively() {
    local abs_bin_dir="$1"
    local default_mode="$2"

    echo -e "\033[0;36m请选择清理选项:\033[0m" >&2
    echo -e "\033[0;90m[0] 不清理\033[0m" >&2
    echo -e "\033[0;33m[1] 清理所选预设的构建目录: $abs_bin_dir\033[0m" >&2

    if [ "$default_mode" = "selected" ]; then
        echo -e "\033[0;90m默认: 清理\033[0m" >&2
    else
        echo -e "\033[0;90m默认: 不清理\033[0m" >&2
    fi

    while true; do
        read -p "输入序号 (0/1，直接回车选择默认): " choice < /dev/tty
        if [ -z "$choice" ]; then
            REPLY="$default_mode"
            break
        fi
        case "$choice" in
            0) REPLY="skip"; break ;;
            1) REPLY="selected"; break ;;
            *) echo -e "\033[0;31m无效选择，请输入 0 或 1。\033[0m" >&2 ;;
        esac
    done
    echo "$REPLY"
}

# 确认是否运行 configure
confirm_run_configure() {
    local default_yes=$1
    local preset_name=$2
    local default_hint="Y"
    if [ "$default_yes" = false ]; then
        default_hint="N"
    fi
    
    local question="是否执行 cmake 配置步骤 (--preset $preset_name)? [Y/n] 默认: $default_hint "

    while true; do
        read -p "$question" reply < /dev/tty
        if [ -z "$reply" ]; then
            REPLY=$default_yes
            break
        fi
        case "$reply" in
            [yY] | [yY][eE][sS]) REPLY=true; break ;;
            [nN] | [nN][oO]) REPLY=false; break ;;
            *) echo -e "\033[0;31m无效输入，请输入 Y 或 N。\033[0m" >&2 ;;
        esac
    done
    echo "$REPLY"
}

# --- 主逻辑 ---
ensure_deps
check_conda
check_vcpkg

if [ ! -f "$PRESETS_PATH" ]; then
    echo >&2 "错误: CMakePresets_linux.json 未找到于 $PRESETS_PATH"
    exit 1
fi
ALL_PRESETS_JSON=$(jq -c '.configurePresets' "$PRESETS_PATH")

# 从 .vscode/settings.json 获取默认值
VS_SETTINGS_JSON=$(get_vscode_settings "$VSCODE_SETTINGS_PATH")
PRESET_FROM_VS=$(echo "$VS_SETTINGS_JSON" | jq -r '.["cmake.configurePreset"] // empty')
PATH_CONFIGURE=$(echo "$VS_SETTINGS_JSON" | jq -r '.["cmake.configureEnvironment"].PATH // empty')
PATH_BUILD=$(echo "$VS_SETTINGS_JSON" | jq -r '.["cmake.buildEnvironment"].PATH // empty')
CONFIGURE_SETTINGS_JSON=$(echo "$VS_SETTINGS_JSON" | jq -c '.["cmake.configureSettings"] // empty')

PATH_CONFIGURE_EXPANDED=$(expand_env_placeholders "$PATH_CONFIGURE")
PATH_BUILD_EXPANDED=$(expand_env_placeholders "$PATH_BUILD")

# 确定预设
if [ -z "$PRESET" ]; then
    if [ -n "$PRESET_FROM_VS" ]; then
        echo -e "\033[0;36mUsing preset from .vscode/settings.json: $PRESET_FROM_VS\033[0m"
        PRESET="$PRESET_FROM_VS"
    else
        PRESET=$(select_preset_interactively "$ALL_PRESETS_JSON")
    fi
fi
echo -e "\033[0;36mSelected preset: $PRESET\033[0m"

# 解析构建目录
BIN_DIR=$(get_binary_dir_for_preset "$ALL_PRESETS_JSON" "$PRESET")
EXPANDED_BIN_DIR=$(expand_cmake_preset_placeholders "$BIN_DIR" "$SCRIPT_DIR")
ABS_BIN_DIR="$EXPANDED_BIN_DIR"
if [[ ! "$ABS_BIN_DIR" = /* ]]; then
    ABS_BIN_DIR="$SCRIPT_DIR/$ABS_BIN_DIR"
fi
# 解析真实路径
mkdir -p "$ABS_BIN_DIR"
ABS_BIN_DIR=$(cd "$ABS_BIN_DIR" && pwd)

# 清理
DEFAULT_CLEAN_MODE=${CLEAN_MODE:-"skip"}
EFFECTIVE_CLEAN_MODE=$(select_clean_mode_interactively "$ABS_BIN_DIR" "$DEFAULT_CLEAN_MODE")

if [ "$EFFECTIVE_CLEAN_MODE" = "selected" ]; then
    if [ -d "$ABS_BIN_DIR" ]; then
        echo -e "\033[0;33mCleaning build directory: $ABS_BIN_DIR\033[0m"
        rm -rf "$ABS_BIN_DIR"
    else
        echo -e "\033[0;33mNo build directory to clean: $ABS_BIN_DIR\033[0m"
    fi
else
    echo -e "\033[0;33mSkip cleaning.\033[0m"
fi

# Configure
ORIGINAL_PATH=$PATH
if [ "$BUILD_ONLY" = false ]; then
    DEFAULT_CONFIGURE_YES=false
    if [ "$EFFECTIVE_CLEAN_MODE" = "selected" ]; then
        DEFAULT_CONFIGURE_YES=true
    fi
    
    RUN_CONFIGURE=$(confirm_run_configure $DEFAULT_CONFIGURE_YES "$PRESET")
    
    if [ "$RUN_CONFIGURE" = true ]; then
        echo -e "\033[0;36mConfiguring project...\033[0m"
        if [ -n "$PATH_CONFIGURE_EXPANDED" ]; then
            echo -e "\033[0;32mInjecting PATH from .vscode for configure.\033[0m"
            export PATH="$PATH_CONFIGURE_EXPANDED:$PATH"
        fi

        configureArgs=("--preset" "$PRESET")

        # 注入 Headless 标记
        if [ "$HEADLESS" = true ]; then
            configureArgs+=("-DECOSIM_HEADLESS=ON")
        fi

        # 注入 .vscode/settings.json 中的 cmake.configureSettings
        if [ -n "$CONFIGURE_SETTINGS_JSON" ] && [ "$CONFIGURE_SETTINGS_JSON" != "null" ] && [ "$CONFIGURE_SETTINGS_JSON" != "{}" ]; then
            echo -e "\033[0;32mInjecting cmake.configureSettings from .vscode.\033[0m"
            # 使用 jq 迭代 JSON 对象的键值对，但避免将原始字符串再次交给 jq 解析
            # 说明：val_json 是原始字符串或布尔文本（true/false），不是合法的 JSON 输入；
            # 直接传给 jq 会触发解析错误，因此在 shell 中完成类型转换。
            while IFS='=' read -r key val_json; do
                # 将布尔值转换为 CMake 开关，其余保持原样
                case "$val_json" in
                    true)  val_str="ON" ;;
                    false) val_str="OFF" ;;
                    *)     val_str="$val_json" ;;
                esac
                val_str_expanded=$(expand_env_placeholders "$val_str")
                configureArgs+=("-D$key=$val_str_expanded")
            done < <(echo "$CONFIGURE_SETTINGS_JSON" | jq -r 'to_entries[] | "\(.key)=\(.value)"')
        fi

        if [ "$VERBOSE" = true ]; then
            configureArgs+=("--log-level=VERBOSE")
        fi

        # 切换到源目录运行 configure
        (cd "$SCRIPT_DIR" && cmake "${configureArgs[@]}")
    else
        echo -e "\033[0;33m已跳过 cmake 配置步骤。\033[0m"
    fi
fi

# Build
if [ "$CONFIGURE_ONLY" = false ]; then
    echo -e "\033[0;36mBuilding project...\033[0m"
    if [ -n "$PATH_BUILD_EXPANDED" ]; then
        echo -e "\033[0;32mInjecting PATH from .vscode for build.\033[0m"
        export PATH="$PATH_BUILD_EXPANDED:$PATH"
    else
        # 恢复原始 PATH
        export PATH="$ORIGINAL_PATH"
    fi

    buildArgs=("--build" "--preset" "$PRESET" "--parallel" "$PARALLEL")
    if [ "$VERBOSE" = true ]; then
        buildArgs+=("--verbose")
    fi
    if [ -n "$TARGET" ]; then
        buildArgs+=("--target" "$TARGET")
    fi

    # 切换到源目录运行 build
    (cd "$SCRIPT_DIR" && cmake "${buildArgs[@]}")

    BUILD_SUCCESS=$?

    if [ $BUILD_SUCCESS -eq 0 ]; then
        echo -e "\033[0;36mBuild succeeded. Creating workspace link in: $ABS_BIN_DIR\033[0m"
        new_workspace_link "$ABS_BIN_DIR" "$SCRIPT_DIR" "workspace"
    else
        echo -e "\033[0;33mBuild failed; skip creating workspace link.\033[0m"
    fi
fi

echo -e "\033[0;32mDone.\033[0m"
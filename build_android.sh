#!/bin/bash

# 设置字符编码
export LANG=en_US.UTF-8
export ANDROID_HOME=${ANDROID_HOME:-/mnt/e/WSL_Data/AndroidSDK}
export ndk_version=r25b

# 配置参数
ANDROID_NDK_API_LEVEL=21
ANDROID_NDK_PATH=${ANDROID_HOME}/ndk/android-ndk-${ndk_version} # 修改为你的 NDK 路径
ANDROID_ABI="arm64-v8a"                               # 可选: arm64-v8a, armeabi-v7a, x86_64, x86
BUILD_TYPE="Release"

# 脚本目录和路径设置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_PATH="$SCRIPT_DIR"
BUILD_DIR="$SOURCE_PATH/build/android"
INSTALL_DIR="$SOURCE_PATH/install"
BUILD_OR_NOT="y"

# 颜色输出函数
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查 NDK 路径
check_ndk_path() {
    if [ -z "$ANDROID_NDK_PATH" ] || [ ! -d "$ANDROID_NDK_PATH" ]; then
        print_error "Android NDK not found at: $ANDROID_NDK_PATH"
        print_info "Please set the correct ANDROID_NDK_PATH in the script"
        print_info "You can download NDK from: https://developer.android.com/ndk/downloads"
        exit 1
    fi
    
    print_info "Using Android NDK: $ANDROID_NDK_PATH"
}

# 设置 Android 工具链
setup_android_toolchain() {
    print_info "Setting up Android toolchain..."
    
    # 检查是否安装了 CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found. Please install CMake"
        print_info "You can install with: sudo apt-get install cmake"
        exit 1
    fi
    
    # 设置工具链文件路径
    TOOLCHAIN_FILE="$ANDROID_NDK_PATH/build/cmake/android.toolchain.cmake"
    
    if [ ! -f "$TOOLCHAIN_FILE" ]; then
        print_error "Android toolchain file not found: $TOOLCHAIN_FILE"
        print_error "Please check your NDK installation"
        exit 1
    fi
    
    print_info "Using toolchain: $TOOLCHAIN_FILE"
    print_info "Target ABI: $ANDROID_ABI"
}

# 处理构建目录
handle_build_dir() {
    if [ -d "$BUILD_DIR" ]; then
        read -p "Build folder exists, rebuild? y/n [n] " BB
        if [[ $BB =~ ^[Yy]$ ]]; then
            print_info "Rebuilding..."
            rm -rf "$BUILD_DIR"
        else
            print_info "Skipping build directory creation"
            BUILD_OR_NOT="n"
        fi
    else
        BUILD_OR_NOT="y"
    fi
}

# CMake 生成
cmake_generate() {
    if [ "$BUILD_OR_NOT" = "y" ]; then
        mkdir -p "$BUILD_DIR"
        
        print_info "CMake generating..."
        
        cmake -S "$SOURCE_PATH" -B "$BUILD_DIR" \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
            -DANDROID_ABI="$ANDROID_ABI" \
            -DANDROID_PLATFORM=android-21 \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
            -DANDROID_STL=c++_static \
            -DANDROID_CPP_FEATURES="rtti exceptions"
        
        if [ $? -ne 0 ]; then
            print_error "CMake generation failed!"
            exit 1
        fi
    fi
}

# 构建和安装
build_and_install() {
    print_info "Starting build..."
    
    # 构建
    cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"
    if [ $? -ne 0 ]; then
        print_error "Build failed!"
        exit 1
    fi
    
    # 安装
    print_info "Installing..."
    cmake --install "$BUILD_DIR" --config "$BUILD_TYPE" --prefix "$INSTALL_DIR"
    if [ $? -ne 0 ]; then
        print_error "Install failed!"
        exit 1
    fi
    
    print_success "Build completed successfully!"
    print_info "Install directory: $INSTALL_DIR"
    
    # 显示安装内容
    if [ -d "$INSTALL_DIR" ]; then
        print_info "Installation contents:"
        find "$INSTALL_DIR" -type f -name "*.so" -o -name "*.a" | while read file; do
            echo "  $file"
            readelf -d $file | grep NEEDED
        done
    fi
}

# 主函数
main() {
    print_info "Android Build Script"
    print_info "===================="
    
    # 显示配置
    print_info "Configuration:"
    print_info "  Source Path: $SOURCE_PATH"
    print_info "  Build Dir: $BUILD_DIR"
    print_info "  Install Dir: $INSTALL_DIR"
    print_info "  ABI: $ANDROID_ABI"
    print_info "  Build Type: $BUILD_TYPE"
    
    check_ndk_path
    setup_android_toolchain
    handle_build_dir
    cmake_generate
    build_and_install
}

# 参数处理
while [[ $# -gt 0 ]]; do
    case $1 in
        --ndk-path)
            ANDROID_NDK_PATH="$2"
            shift 2
            ;;
        --abi)
            ANDROID_ABI="$2"
            shift 2
            ;;
        --build-type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --install-dir)
            INSTALL_DIR="$2"
            shift 2
            ;;
        -h|--help)
            echo "Android Build Script"
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --ndk-path PATH    Set Android NDK path"
            echo "  --abi ABI          Set target ABI (arm64-v8a, armeabi-v7a, x86_64, x86)"
            echo "  --build-type TYPE  Set build type (Release, Debug)"
            echo "  --build-dir DIR    Set build directory"
            echo "  --install-dir DIR  Set install directory"
            echo "  -h, --help         Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# 运行主函数
main

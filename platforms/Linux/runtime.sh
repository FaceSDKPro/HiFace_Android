#!/bin/sh
# Sourced by launchers; supports an extracted package and the dual-architecture Demo.
# 由启动脚本加载，兼容解压后的独立包和双架构 Demo。
resolve_hiface_runtime() {
    if [ "$(uname -s)" != Linux ]; then
        echo "Run this Linux package on Ubuntu 22.04+ (x86_64 or ARM64)." >&2
        return 1
    fi
    case "$(uname -m)" in
        x86_64|amd64) hiface_arch=x86_64 ;;
        aarch64|arm64) hiface_arch=arm64 ;;
        *) echo "Unsupported architecture: $(uname -m)" >&2; return 1 ;;
    esac
    hiface_runtime=$demo_dir
    if [ ! -x "$hiface_runtime/bin/hiface_desktop" ] && [ ! -x "$hiface_runtime/bin/hiface_demo" ]; then
        hiface_runtime=$demo_dir/runtimes/$hiface_arch
    fi
    if [ "${1:-}" = gui ]; then
        if [ -x "$hiface_runtime/bin/hiface_startup" ] && [ -x "$hiface_runtime/bin/hiface_desktop" ]; then return 0; fi
        echo "Incomplete Demo package. FaceAISDK.Service@gmail.com" >&2
        return 1
    fi
    if [ ! -x "$hiface_runtime/bin/hiface_demo" ] || [ ! -f "$hiface_runtime/sdk/lib/libhiface.so.1" ]; then
        echo "Missing $hiface_arch runtime. Keep the complete Demo and SDK directories together." >&2
        return 1
    fi
}

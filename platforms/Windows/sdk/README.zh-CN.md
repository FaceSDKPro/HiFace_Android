# HiFace Windows SDK · 0.8.3 Beta

[English](README.md) | 简体中文

支持 Windows 10/11 x64、CPU，C/C++ 均调用公共 C ABI。
`bin/` 为 DLL，`lib/` 为导入库和 CMake 配置，`include/` 为头文件，`../examples/` 为集成示例。
模型已内置于 `hiface.dll`。

## 构建与部署

安装 Visual Studio 2022 的 C++ 桌面工具与 CMake 3.22.1+，在本 SDK 目录执行：

```powershell
cmake -S ../examples -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$PWD"
cmake --build build --config Release
./build/bin/Release/hiface_c_consumer.exe
./build/bin/Release/hiface_cpp_consumer.exe
./build/bin/Release/hiface_model_consumer.exe
```

自己的 CMake 工程：

```cmake
find_package(HiFace 0.8.3 CONFIG REQUIRED COMPONENTS C)
target_link_libraries(your_app PRIVATE HiFace::HiFace)
```

不使用 CMake 时，添加 `include/` 并链接 `lib/hiface.lib`。
将 `bin/` 下全部 DLL 复制到应用 EXE 同目录，并按宿主配置部署 CRT。
头文件、导入库和 DLL 须使用同一版本的 x64 文件，不要定义 `HIFACE_C_API_STATIC` 或 `HIFACE_C_API_BUILD`。
分发时保留 `LICENSE`、`AI_USAGE_NOTICE.md` 和 `licenses/`。

## 调用流程

完整示例见 [model_consumer.cpp](../examples/model_consumer.cpp)：
配置 CPU → 创建 Session → 用 `hiface_windows_session_load_model_v1` 加载所需能力
→ `hiface_session_prepare_loaded_models_v1` → 处理图片 → 释放结果与 Session。
结构体先设置 `struct_size`，逐次检查返回状态。

| 功能 | 接口 |
| --- | --- |
| 图片检测 | `hiface_session_process_rgba_v1` |
| 单脸 / 多脸采集 | `hiface_session_capture_rgba_v1` / `hiface_session_capture_faces_rgba_v1` |
| 相机采集、活体、1:1 | YUV420 送帧、face-flow 系列 |
| 人脸库、1:N/M:N | feature / search 系列；HF1 和数据库操作无需初始化模型 |

参数和所有权以公共头文件为准。宿主负责图片解码与方向处理；图片采集使用 2..4096 内的偶数 RGBA 宽高。
相机输入需正确设置平面 stride 和旋转。单个 Session / Database 串行调用，耗时工作放到 UI 线程之外。
等待在途调用结束后销毁句柄；SDK 分配的结果用对应接口释放。HF1 和数据库未加密。

应用层用法见独立 Demo 的 `source/` 与 `source/integration/`。
找不到 DLL / `0xc000007b`：检查完整的 x64 DLL；初始化失败：检查状态和日志。
相机由应用打开和送帧。当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。
[反馈问题](https://github.com/FaceSDKPro/HelloFace/issues)时附版本、状态码和脱敏日志。

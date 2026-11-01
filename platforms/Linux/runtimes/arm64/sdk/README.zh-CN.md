# HiFace Linux SDK

[English](README.md) | 简体中文

**Beta** · x86_64 / ARM64 · CPU · C ABI v1。
运行基线为 Ubuntu 22.04 / glibc 2.35，兼容目标为 Ubuntu 24.04。
SDK正在Beta 迭代公测中，请及时升级SDK版本。当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。目标设备验收待完成。

## 接入

使用与目标架构一致的 SDK。`include/hiface/` 是公共声明，`lib/` 是共享库，
模型资源受保护地封装在共享库中，不提供独立模型文件。SDK 本身无需桌面或摄像头。

```cmake
cmake_minimum_required(VERSION 3.22.1)
project(MyApp LANGUAGES CXX)
find_package(HiFace CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HiFace::HiFace)
```

配置时传 `-DCMAKE_PREFIX_PATH=/absolute/path/to/sdk`。
随应用部署 `libhiface.so.1`、它指向的实际库，设置 RPATH 或库搜索路径，保留共享库链接。

## 运行 C 示例

在 SDK 目录执行，需要 CMake 3.22.1+ 和 C 编译器：

```bash
sha256sum -c FILES.sha256
cmake -S ../../../examples/image-search -B build -DCMAKE_PREFIX_PATH="$PWD"
cmake --build build --parallel 2
./build/hiface_sdk_image_search --builtin :memory: --blank 256 256
```

[image_search.c](../../../examples/image-search/image_search.c) 演示 SDK 初始化、最多十张脸采集、查库及资源释放。
将 `:memory: --blank 256 256` 替换为 `/path/to/hiface-search.db image.rgba WIDTH HEIGHT` 可搜索自己的图片像素。
输入需已处理方向，紧密排列的 RGBA8888，宽高为 2..4096 的偶数，文件长度恰好为 `WIDTH*HEIGHT*4`。
JPEG/PNG 解码和摄像头适配见独立 Demo。

## 接口要点

- 填写每个结构体的 `struct_size` 和存在时的 `api_version`，检查返回状态及结果有效标记。
- Release 构建不包含内部 Debug/Info 诊断，保留 Warning/Error 日志回调和原始状态码。
- 配置启用 feature/landmark。配置运行时 → 创建 Session → `hiface_linux_load_builtin_models_v1` → 处理 → 销毁。
  Linux 初始化接口声明位于 `include/hiface/hiface_linux.h`。
  接口同步调用；同一 Session 串行使用，销毁前等待在途调用结束，输入缓冲保留到调用返回。
- 采集输出 512 维特征和可选对齐图；静态图片不做活体检测，多人采集需逐脸检查状态。
  借用的结果数据应在下一次调用或结果销毁前复制。
- 实时帧使用 `process_camera_yuv420`，填写真实平面步长、1920×1080 范围内的偶数尺寸、支持的旋转角度及同源单调毫秒时间。
  启动流程后持续送帧，读取业务终态后 stop。
- 人脸库支持 CRUD 和 Top-K，校验 `HIFACE_FEATURE_SPACE_ID`。HF1 和 SQLite 未加密。
  相似度不是身份概率；活体拒绝时不能仅凭相似度放行。

完整签名与契约见 `include/hiface/hiface_c_api.h`。
保留许可和依赖声明；使用、再分发授权及支持请[联系作者](https://github.com/FaceSDKPro/HelloFace/issues)。


## 红外单模态活体

创建 CPU Session，特征和关键点开关可为零，先调用
`hiface_linux_load_infrared_model_v1(session, NULL)`，再调用
`hiface_session_evaluate_infrared_liveness_v1`。输入归正、未镜像的真实近红外 GRAY8 数据及行步长，
支持奇数尺寸，每边最大 4096；宿主提供该 IR 图像内的人脸框（左上角加宽高，至少 10×10）和
目标摄像头标定的真人阈值。成功返回后，`real_score >= real_threshold` 才通过；失败不能放行。

完整 C 消费示例为 `examples/infrared_liveness.c`。SDK 不管理 IR 摄像头采集、IR 人脸检测或补光，
此入口不改变 RGB 相机活体和搜索流程。

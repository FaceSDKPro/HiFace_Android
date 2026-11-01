# HiFace Linux SDK

English | [简体中文](README.zh-CN.md)

**Beta** · x86_64 / ARM64 · CPU · C ABI v1.
Ubuntu 22.04 / glibc 2.35 baseline; Ubuntu 24.04 is a compatibility target.
The SDK is in iterative public Beta testing. Please keep the SDK version up to date. The current model does not support face recognition for people under 12 and must not be used in financial scenarios. Target-device acceptance is pending.

## Integrate

Use the SDK matching your architecture. `include/hiface/` contains public declarations, `lib/` the shared SDK,
and model resources are protected inside the shared library. No standalone model files are distributed.

```cmake
cmake_minimum_required(VERSION 3.22.1)
project(MyApp LANGUAGES CXX)
find_package(HiFace CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HiFace::HiFace)
```

Configure with `-DCMAKE_PREFIX_PATH=/absolute/path/to/sdk`. Deploy `libhiface.so.1` and its target library with your application; configure RPATH/library search paths. Keep library symlinks intact.

## Run the C example

From the SDK directory, with CMake 3.22.1+ and a C compiler:

```bash
sha256sum -c FILES.sha256
cmake -S ../../../examples/image-search -B build -DCMAKE_PREFIX_PATH="$PWD"
cmake --build build --parallel 2
./build/hiface_sdk_image_search --builtin :memory: --blank 256 256
```

[image_search.c](../../../examples/image-search/image_search.c) initializes the SDK, captures up to ten faces, queries the database and releases resources.
Replace `:memory: --blank 256 256` with `/path/to/hiface-search.db image.rgba WIDTH HEIGHT` to search your own pixels.
Input must be orientation-normalized, tightly packed RGBA8888: even dimensions 2..4096, exactly `WIDTH*HEIGHT*4` bytes.
JPEG/PNG decoding and camera adapters are demonstrated in the separate Demo.

## API essentials

- Set every `struct_size` and, where present, `api_version`. Check returned status and result-validity flags.
- Release builds omit internal Debug/Info diagnostics. Warning/Error log callbacks and original status codes remain available.
- Enable feature/landmark in the config. Configure runtime → create Session → `hiface_linux_load_builtin_models_v1` → process → destroy.
  The Linux initialization declaration is in `include/hiface/hiface_linux.h`.
  Calls are synchronous; serialize Session work and wait for in-flight calls before destruction. Input buffers remain valid until return.
- Capture returns 512-float features and optional aligned previews. Static images do not perform liveness checks.
  Multi-face capture has a separate status per face. Copy borrowed result data before the next call/result destruction.
- Camera input uses `process_camera_yuv420` with real plane strides, even dimensions up to 1920×1080,
  supported rotation and one monotonic millisecond clock. Feed frames until a started flow reaches a terminal outcome, then stop it.
- The search database supports CRUD and Top-K; check `HIFACE_FEATURE_SPACE_ID`. HF1 and SQLite are unencrypted.
  Similarity is not identity probability; a spoof-rejected result must not authorize access.

Full signatures and contracts: `include/hiface/hiface_c_api.h`.
Keep the license and dependency notices. For usage/redistribution authorization or support, [contact the author](https://github.com/FaceSDKPro/HelloFace/issues).

IR-only FLIR: create a CPU session with feature/landmarks disabled, call
`hiface_linux_load_infrared_model_v1(session, NULL)`, then
`hiface_session_evaluate_infrared_liveness_v1`. Supply upright unmirrored GRAY8 IR
pixels, row stride, an IR-coordinate face box (x/y + width/height, at least 10x10)
and a device-calibrated real threshold. `real_score >= real_threshold` passes;
check status before reading the result. Dimensions may be odd, up to 4096 per side.
See `examples/infrared_liveness.c` for a complete consumer. This does not capture
IR cameras, detect a face, or change the RGB liveness/search flows. Validate actual IR hardware and attacks separately.

# HiFace Windows SDK · 0.8.3 Beta

English | [简体中文](README.zh-CN.md)

Windows 10/11 x64, CPU. C and C++ use the public C ABI.
`bin/` contains the DLLs, `lib/` the import library and CMake package,
`include/` the headers, and `../examples/` runnable integration examples.
Models are built into `hiface.dll`.

## Build and deploy

With Visual Studio 2022 Desktop C++ tools and CMake 3.22.1+, run from this SDK directory:

```powershell
cmake -S ../examples -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$PWD"
cmake --build build --config Release
./build/bin/Release/hiface_c_consumer.exe
./build/bin/Release/hiface_cpp_consumer.exe
./build/bin/Release/hiface_model_consumer.exe
```

In your CMake project:

```cmake
find_package(HiFace 0.8.3 CONFIG REQUIRED COMPONENTS C)
target_link_libraries(your_app PRIVATE HiFace::HiFace)
```

Without CMake, add `include/` and link `lib/hiface.lib`.
Copy all `bin/*.dll` beside your executable and deploy your application's CRT.
Use matching x64 headers, library and DLLs; do not define `HIFACE_C_API_STATIC` or `HIFACE_C_API_BUILD`.
Keep `LICENSE`, `AI_USAGE_NOTICE.md` and `licenses/` when redistributing.

## Call the SDK

See [model_consumer.cpp](../examples/model_consumer.cpp) for the complete lifecycle:
configure CPU → create Session → load capabilities with `hiface_windows_session_load_model_v1`
→ `hiface_session_prepare_loaded_models_v1` → process images → release results and Session.
Initialize each structure's `struct_size` and check return codes.

- Detection: `hiface_session_process_rgba_v1`.
- Capture: `hiface_session_capture_rgba_v1`; multiple faces: `hiface_session_capture_faces_rgba_v1`.
- Camera capture, liveness and verification: YUV420 and face-flow APIs.
- Gallery/search: feature and search APIs; HF1/database operations need no model initialization.

The headers define parameters and ownership. Decode images and handle orientation in your application;
photo capture accepts even RGBA dimensions from 2 to 4096. Supply camera planes with correct strides and rotation.
Serialize calls per Session/Database off the UI thread. Wait for active calls before destruction
and use SDK functions to release SDK-owned results. HF1 and databases are unencrypted.

For application examples, see the separate Demo's `source/` and `source/integration/`.
Missing DLL / `0xc000007b`: check the complete x64 DLL set. Initialization failure: check status and logs.
The SDK does not open cameras. The current model does not support face recognition for people under 12 and must not be used in financial scenarios.
[Report issues](https://github.com/FaceSDKPro/HelloFace/issues) with version, status code and sanitized logs.

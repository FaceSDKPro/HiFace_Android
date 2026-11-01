# HiFace Windows · 0.8.5 Beta

English | [简体中文](README.zh-CN.md)

Windows 10/11 x64, Intel/AMD CPU. The SDK is in iterative public Beta testing. Please keep the SDK version up to date.

- [SDK integration](sdk/README.md): public C API, DLLs, CMake and C/C++ examples.
- [Demo](demo/README.md): executable, application source and AI-generated sample data.

## Run the Demo

Open `demo/`, run `self_test.bat`, then `run_demo.bat` after `RESULT: PASS`.
Keep the complete directory. No development tools or model downloads are needed.

## Build from source

Install Visual Studio 2022 Desktop C++ tools, CMake 3.22.1+ and Python `cryptography`. Keep your stable Ed25519 PKCS#8 publisher PEM outside this directory, then run here:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./build_demo.ps1 -PublisherKey C:\BuildKeys\publisher.pem
```

Edit `demo/source/`; the rebuilt executable is `build/demo/bin/Release/hiface_demo.exe`.
GUI and camera testing: package file `demo/Windows真机测试清单.txt`.

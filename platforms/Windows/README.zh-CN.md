# HiFace Windows · 0.8.3 Beta

[English](README.md) | 简体中文

支持 Windows 10/11 x64、Intel/AMD CPU。SDK正在Beta 迭代公测中，请及时升级SDK版本。

- [SDK 接入](sdk/README.zh-CN.md)：公共 C API、DLL、CMake 和 C/C++ 示例。
- [Demo 使用](demo/README.md)：可执行程序、应用源码和 AI 合成人脸演示数据。

## 运行 Demo

进入 `demo/`，先运行 `self_test.bat`，看到 `RESULT: PASS` 后运行 `run_demo.bat`。
保留完整目录，无需安装开发工具或下载模型。

## 编译 Demo

安装 Visual Studio 2022 的 C++ 桌面工具、CMake 3.22.1+ 和 Python `cryptography`。将固定使用的 Ed25519 PKCS#8 发布者 PEM 保存在本目录外，然后执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./build_demo.ps1 -PublisherKey C:\BuildKeys\publisher.pem
```

修改 `demo/source/` 后重新编译，程序位于 `build/demo/bin/Release/hiface_demo.exe`。
界面与相机验收见包内 `demo/Windows真机测试清单.txt`。

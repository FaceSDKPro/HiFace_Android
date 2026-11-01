# HelloFace

[English](README.md) | 简体中文

**0.8.2 Beta**

Android、iOS、Harmony、Windows 和 Linux SDK 与 Demo 统一更新至 0.8.2。

### 离线人脸检测、采集、活体、1:1 比对及 1:N/M:N 搜索。

![HelloFace](AboutHelloFace.png)

| 平台 | 环境 | 接入说明 |
|---|---|---|
| Android | Android 6.0+ · ARM64/ARMv7 · JDK 17 · SDK 36 | [Demo](platforms/Android/README.zh-CN.md) · [SDK](platforms/Android/docs/SDK.zh-CN.md) |
| iOS | iOS 15+ · Swift 6 · Xcode | [Demo](platforms/iOS/README.zh-CN.md) · [SDK](platforms/iOS/SDK/API.zh-CN.md) |
| Harmony | HarmonyOS API 20+ · ARM64 · DevEco Studio 26 | [Demo](platforms/Harmony/README.zh-CN.md) · [SDK](platforms/Harmony/SDK.zh-CN.md) |
| Linux | Ubuntu 22.04+ · x86_64/ARM64 · X11/XWayland | [Demo](platforms/Linux/README.zh-CN.md) · [SDK](platforms/Linux/runtimes/x86_64/sdk/README.zh-CN.md) |
| Windows | Windows 10/11 x64 · CPU | [Demo](platforms/Windows/README.zh-CN.md) · [SDK](platforms/Windows/sdk/README.zh-CN.md) |
| Web 人脸录入 | 浏览器 | [在线体验](https://hiface-mdp.pages.dev/face/) |

**更多平台（计划）：** 将支持微信小程序、Chrome、uni-app、Flutter 插件、React Native（RN）插件等。

## 获取代码

本仓库使用 Git LFS 管理 SDK 二进制等大文件，若只需默认分支的最新提交，建议使用浅克隆以跳过历史提交：

```bash
git clone --depth 1 --single-branch https://github.com/FaceSDKPro/HelloFace.git
```

- SDK正在Beta 迭代公测中，请及时升级SDK版本。
- 当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。
- 人脸搜索识别要求摄像头成像清晰；室外环境下，摄像头宽动态范围须大于 100 dB。
- 其他平台在完善支持中，有问题请提issues。

[版本下载](https://github.com/FaceSDKPro/HelloFace/releases) · [Android APK 构建](https://github.com/FaceSDKPro/HelloFace/actions/workflows/android-demo.yml) · [问题反馈](https://github.com/FaceSDKPro/HelloFace/issues)

邮箱联系：[FaceAISDK.Service@gmail.com](mailto:FaceAISDK.Service@gmail.com)

微信联系：

<img src="WeChat.jpg" alt="微信二维码" width="200">

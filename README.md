# HelloFace

English | [中文](README.zh-CN.md)

**0.8.2 Beta**

Android, iOS, Harmony, Windows, and Linux SDKs and Demos are updated to 0.8.2.

### Offline face detection, capture, liveness, 1:1 verification and 1:N/M:N search.

![HelloFace](AboutHelloFace-EN.png)

| Platform | Requirements | Integration |
|---|---|---|
| Android | Android 6.0+ · ARM64/ARMv7 · JDK 17 · SDK 36 | [Demo](platforms/Android/README.md) · [SDK](platforms/Android/docs/SDK.md) |
| iOS | iOS 15+ · Swift 6 · Xcode | [Demo](platforms/iOS/README.md) · [SDK](platforms/iOS/SDK/API.md) |
| Harmony | HarmonyOS API 20+ · ARM64 · DevEco Studio 26 | [Demo](platforms/Harmony/README.md) · [SDK](platforms/Harmony/SDK.md) |
| Linux | Ubuntu 22.04+ · x86_64/ARM64 · X11/XWayland | [Demo](platforms/Linux/README.md) · [SDK](platforms/Linux/runtimes/x86_64/sdk/README.md) |
| Windows | Windows 10/11 x64 · CPU | [Demo](platforms/Windows/README.md) · [SDK](platforms/Windows/sdk/README.md) |
| Web Face Enrollment | Browser | [Try online](https://hiface-mdp.pages.dev/face/) |

**More platforms (planned):** WeChat Mini Programs, Chrome, uni-app, Flutter plugins, React Native (RN) plugins, and more.

## Get the code

This repository uses Git LFS to manage large files such as SDK binaries. If you only need the latest commit from the default branch, use a shallow clone to skip the commit history:

```bash
git clone --depth 1 --single-branch https://github.com/FaceSDKPro/HelloFace.git
```

- The SDK is in iterative public Beta testing. Please keep the SDK version up to date.
- The current model does not support face recognition for people under 12 and must not be used in financial scenarios.
- Face search requires clear camera images; outdoor deployments require a camera with a wide dynamic range greater than 100 dB.
- Support for other platforms is being improved. If you encounter a problem, please open an issue.

[Releases](https://github.com/FaceSDKPro/HelloFace/releases) · [Android APK builds](https://github.com/FaceSDKPro/HelloFace/actions/workflows/android-demo.yml) · [Issues](https://github.com/FaceSDKPro/HelloFace/issues)

Email: [FaceAISDK.Service@gmail.com](mailto:FaceAISDK.Service@gmail.com)

WeChat contact:

<img src="WeChat.jpg" alt="WeChat QR code" width="200">

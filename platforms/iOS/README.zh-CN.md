# HiFace iOS 集成 Demo

[English](README.md) | 简体中文

SDK：**0.8.3 Beta**

支持检测、采集、活体、1:1 比对、1:N/M:N 搜索和人脸库管理，算法在设备端运行。

**模型适用范围：当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。**

## 运行

1. 用 Xcode 打开 `HiFaceDemo.xcodeproj`，选择 `HiFaceDemo`。
2. 在 Signing & Capabilities 配置自己的 Team 和 Bundle ID。
3. 在 iOS 15+ 的 iPhone 运行并允许相机权限。

模拟器和 My Mac 适合界面或编译检查；相机和性能需在 iPhone 验证。

## 工程

| 目录 | 用途 |
|---|---|
| `HiFaceDemo/` | 可修改的 SwiftUI / AVFoundation 示例 |
| `SDK/` | 二进制 SPM；保留 HiFace 和 HiFaceNative 两个目标 |

使用 `HiFaceSDK()` 默认模型配置，在 App Info.plist 添加 `NSCameraUsageDescription`。

接入说明：[SDK](SDK/README.zh-CN.md) · [Demo](DEMO.zh-CN.md)。

SDK正在Beta 迭代公测中，请及时升级SDK版本。[更新说明](https://github.com/FaceSDKPro/)。

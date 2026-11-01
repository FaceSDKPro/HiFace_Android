# HiFace HarmonyOS 集成 Demo

[English](README.md) | 简体中文

SDK：**0.8.3 Beta**

支持检测、采集、活体、1:1 比对、1:N/M:N 搜索和人脸库管理，算法在设备端运行。

**模型适用范围：当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。**

## 运行

1. 用 DevEco Studio 26 打开本目录并同步依赖。
2. 配置自己的 Bundle Name、签名证书和设备 Profile。
3. 在 API 20+ 的 arm64 手机运行 `demo`。未签名 HAP 无法安装到手机。

## 工程

| 目录 | 用途 |
|---|---|
| `demo/` | 可修改的 ArkTS 示例 |
| `libs/hiface-0.8.3.har` | SDK 二进制 |
| `AppScope/`、`hvigor/` | 应用信息和构建配置，需保留 |

模型已内置于 HAR，使用前等待 `whenReady()`。

接入说明：[SDK](SDK.zh-CN.md) · [Demo](demo/README.zh-CN.md)。

SDK正在Beta 迭代公测中，请及时升级SDK版本。[更新说明](https://github.com/FaceSDKPro/)。

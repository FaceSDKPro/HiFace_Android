# HiFace HarmonyOS Integration Demo

English | [简体中文](README.zh-CN.md)

SDK: **0.8.5 Beta**

On-device detection, capture, liveness, 1:1 comparison, 1:N/M:N search and face-library management.

**Model scope: The current model does not support face recognition for people under 12 and must not be used in financial scenarios.**

## Run

1. Open this directory in DevEco Studio 26 and sync dependencies.
2. Configure your bundle name, signing certificate and device profile.
3. Run `demo` on an API 20+ arm64 phone. Phones require a signed HAP.

## Project

| Path | Purpose |
|---|---|
| `demo/` | Editable ArkTS examples |
| `libs/hiface-0.8.5.har` | Binary SDK |
| `AppScope/`, `hvigor/` | Required App and build configuration |

Models are embedded in the HAR. Await `whenReady()` before using the SDK.

Guides: [SDK](SDK.md) · [Demo](demo/README.md).

The SDK is in iterative public Beta testing. Please keep the SDK version up to date. [Updates](https://github.com/FaceSDKPro/).

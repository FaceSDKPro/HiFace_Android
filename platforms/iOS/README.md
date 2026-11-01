# HiFace iOS Integration Demo

English | [简体中文](README.zh-CN.md)

SDK: **0.8.3 Beta**

On-device detection, capture, liveness, 1:1 comparison, 1:N/M:N search and face-library management.

**Model scope: The current model does not support face recognition for people under 12 and must not be used in financial scenarios.**

## Run

1. Open `HiFaceDemo.xcodeproj` in Xcode and select `HiFaceDemo`.
2. Set your Team and bundle ID in Signing & Capabilities.
3. Run on an iOS 15+ iPhone and allow camera access.

Simulators and My Mac support UI/build checks. Validate camera behavior and performance on iPhone.

## Project

| Path | Purpose |
|---|---|
| `HiFaceDemo/` | Editable SwiftUI / AVFoundation examples |
| `SDK/` | Binary SPM; retain both HiFace and HiFaceNative targets |

Use `HiFaceSDK()` with its default model bundle. Add `NSCameraUsageDescription` to your App Info.plist.

Guides: [SDK](SDK/README.md) · [Demo](DEMO.md).

The SDK is in iterative public Beta testing. Please keep the SDK version up to date. [Updates](https://github.com/FaceSDKPro/).

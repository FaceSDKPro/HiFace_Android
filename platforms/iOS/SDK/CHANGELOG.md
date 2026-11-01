# HiFace iOS SDK 版本说明

发布状态以对应 SPM tag 和 GitHub Release 为准；本文件记录准备交付的版本内容。

## [0.8.3]

- 五平台由公共 Core/SQLite 统一管理试用和正式授权；使用 runtime.db，不迁移旧状态。
- 修复授权状态跨功能期限的不一致，以及启动恢复遇到转正/续期时的错误 14；同证重复导入保持幂等。
- 简化人脸搜索双行状态展示与相机生命周期处理；保留完整 faceID 和活体分数。
- 本地候选准备中；构建、验收及发布批准待完成。

## [0.8.2]

- 本地候选准备中；构建、验收及发布批准待完成。

## [0.8.1]

- 统一 Demo 的“SDK参数设置”入口，并在导出前核对本地框架与 Demo 的版本和构建序号。
- 加强版本同步、候选打包和跨平台回归检查；公开 API、特征空间和模型密文保持当前已审查基线。
- 本地候选准备中；二进制、设备验收及本次发布批准待完成。

## 0.8.0 — Preparation, not published

- 将授权状态持久化检查周期统一为 10 秒，同时继续在每次能力调用和输出边界校验时间、代次与能力范围。
- 同步五平台 SDK 与 Demo 版本；二进制、真机和完整公开树验收仍须绑定本次候选。

## 0.7.0 — Preparation, not published

- 同步 SDK 与 Demo 版本，保持精简的直接接入方式。
- 加强独立消费者、最终产物身份和公开资源检查；构建及设备验收仍待完成。

## 0.0.6 — Local candidate, not published

- 更新 SDK 与 Demo 集成，改善资源生命周期和运行状态处理。

## 0.0.5 — Local candidate, not published

- Bound the JDX feature preprocessing configuration to the authenticated BPHFM2 feature container. Existing feature spaces and databases are not automatically relabeled or migrated.
- Selected the reviewed NCNN A profile on iOS; retained the compile-only arm64 Simulator stub and separate device model resources.
- Fixed concurrent clock sampling, retained fail-closed runtime checks, and unified the default search database filename.
- Strengthened final App model integrity, ZIP entry checks and candidate release validation. Target-device acceptance remains separate.

## 0.0.4

- Added a separate GRAY8 infrared liveness API with explicit device-calibrated thresholds.
- Hardened Native runtime checks and release binaries.
- Replaced the Simulator runtime with an arm64 compile-only stub and removed models, inference dependencies, and Intel `x86_64` support from Simulator slices.
- Added bilingual Demo comments and synchronized generated English resources.
- Preserved offline RGB detection, capture, verification and search workflows.

## 0.0.1

首个 iOS SDK 版本，最低 iOS 15，Swift 6，采用 SPM 二进制分发。

- 提供检测、五点/可选 Face Mesh 与头姿、HF1 特征提取、引导采集、动作/静默活体、动态及静态 1:1。
- 提供 SQLite 人脸库、单次特征查询及连续单人/多人搜索；连续搜索使用 `FaceSearchSessionConfiguration`。
- 相机流程共用互斥槽，支持请求取消和幂等关闭；相机回调在 `MainActor` 交付。
- 打包 `HiFace.xcframework` 和 `HiFaceNative.xcframework`，包含 device arm64 与 simulator arm64/x86_64。
- SDK 包内包含 BPHFM1/HFM2 密文模型和隐私清单，保留公开 Swift interface，内部崩溃符号单独存档。
- Demo 搜索相机支持固定采集帧率的回退，通过送帧节流实现 15/5 FPS 分析目标。
- Demo 使用 SDK 自带的 Framework 模型，不再覆盖为 App Bundle 路径；模型文件读取失败统一返回 `modelLoadFailed`。
- Native 打包读取 CMake 当前目标路径，防止目录重组后误选旧 Framework 缓存；Demo 版本统一为 0.0.1。
- 公开 API 已冻结，并验证既有集成示例对更新后二进制 SDK 的编译兼容。

限制：首版 CPU 推理；宿主负责相机、权限和 UI；多人搜索不支持静默活体；人脸库未加密。
当前 0.0.1 Beta 可使用至北京时间 2026-12-25 全天，2026-12-26 零点起停止初始化和工作；
客户绑定和撤销机制尚未提供。真实性、识别精度和设备性能以目标场景的验收记录为准。

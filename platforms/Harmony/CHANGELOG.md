# Changelog

## [0.8.3]

- Unified trial and commercial authorization in the shared Core/SQLite runtime.db; old state is not migrated.
- Fixed inconsistent status at feature expiry and startup error 14 during concurrent activation/renewal; identical certificate import remains idempotent.
- Simplified the two-line face-search status, retaining complete face IDs and liveness scores.
- 本地候选准备中；构建、验收及发布批准待完成。

## [0.8.2]

- 本地候选准备中；构建、验收及发布批准待完成。

## [0.8.1]

- Aligned the Demo SDK Settings entry with the other platforms.
- Check release inputs before building and bind the delivery report and README to the exact packaged HAR bytes.
- Retain the reviewed public API, feature space and encrypted model baseline. Candidate builds, device acceptance and publication approval remain pending.

## [0.8.0] - Preparation, not published

- Changed the authorization-state persistence checkpoint to 10 seconds while retaining per-use and output-boundary validation.
- Aligned the five SDK and Demo identities; binary, device and full public-tree acceptance remain pending for this candidate.

## [0.7.0] - Preparation, not published

- Aligned SDK and Demo release identities while preserving direct SDK ownership and lifecycle cleanup.
- Strengthened candidate and public-resource validation; fresh builds and device acceptance remain pending.

## [0.0.6] - Local candidate, not published

- Updated SDK and Demo integration, runtime state handling and package validation.

## [Unreleased]

## [0.0.5] - Local candidate, not published

- Bound the JDX feature preprocessing configuration to the authenticated BPHFM2 feature container. Existing feature spaces and databases are not automatically relabeled or migrated.
- Selected the reviewed NCNN A profile on HarmonyOS and retained the existing public signature baseline.
- Fixed concurrent clock sampling, retained fail-closed runtime checks, and unified the default search database filename.
- Strengthened candidate validation and require a valid freshly packaged HAR before negative release tests. Device acceptance remains separate.

## [0.0.4]

- Added a separate GRAY8 infrared liveness API with explicit device-calibrated thresholds.
- Hardened Native runtime checks and release binaries.
- Added bilingual Demo comments and synchronized generated English resources.
- Preserved offline RGB detection, capture, verification and search workflows.

## [0.0.1] - Release candidate

- Exposed SDK-owned capabilities as public interfaces returned by HiFaceSdk; removed internal port constructors and unreachable internal HAR declarations.
- Kept EAR/MAR and derived action metrics internal; public detection provides boxes, confidence, landmarks and optional pose/mesh.
- Froze public signatures and verified the previous integration example against the updated Release HAR.
- Preserved original HF1 features with explicit feature-space IDs; added lightweight metadata pages, existence checks and metadata-only updates.
- Moved search-database close and SQLite/cache cleanup to background work, rejecting queued operations after close.
- Rejected integer truncation at the N-API boundary and aligned search thresholds with Core float32 semantics.

- Aligned SDK and Demo versions with the Android/iOS 0.0.1 release train; verified local zero-major HAR build and OHPM prepublish (registry review pending).
- Added customer Demo checksums and commercial license; the SDK remains in iterative public Beta testing and should be upgraded promptly.
- Removed the pre-release fixed build-stage probe; runtime compatibility diagnostics use `getApiVersion()` and readiness uses `whenReady()`.

- Hardened customer Demo export and Release verification so only a fully validated HAR with all five encrypted models embedded exactly once can be delivered.
- Fixed Demo capture/liveness/search startup by passing the shared SDK as a plain ArkUI component input, avoiding `@Prop` copies of live Promises and session state.
- Moved camera-flow/search start, stop and SDK destruction off the ArkUI thread; invalidate stale work immediately and order replacement starts after pending controls.

- Aligned liveness flow defaults and `hasActions()` with the Android SDK contract.
- Hardened camera-flow Native argument validation, Demo retry behavior and result-dialog frame suspension.
- Prepared the initial HarmonyOS release candidate of the HiFace bytecode HAR.
- Added offline face detection, capture, liveness, 1:1 verification and 1:N/M:N search.
- Added an independent SQLite face database, realtime frame backpressure and stable lifecycle errors.
- Added an arm64-v8a N-API library with embedded authenticated HFM2 models.
- Added consumer obfuscation rules and Release artifact hardening checks.

# HiFace 0.8.5 API Guide

English | [简体中文](API.zh-CN.md)

## Installation

**Model scope: The current model does not support face recognition for people under 12 and must not be used in financial scenarios.**

Face search requires clear camera images; outdoor deployments require a camera with a wide dynamic range greater than 100 dB.

The SDK is in iterative public Beta testing. Please keep the SDK version up to date. [Updates](https://github.com/FaceSDKPro/).

| Requirement | Supported |
|---|---|
| iOS | 15 and later |
| Device | `arm64` |
| Simulator | `arm64` on Apple Silicon; Intel Mac is unsupported |
| Entry point | Swift `HiFaceSDK` |

Add the released binary Swift package to Xcode with a fixed version and select the `HiFace` product. Keep both `HiFace` and `HiFaceNative` binary targets. Configure `NSCameraUsageDescription` in the host Info.plist.

```swift
import HiFace
let sdk = HiFaceSDK()
```

The SDK owns its model bundle. Do not override it with the host App bundle.

## Live detection and frame ownership

Use two-plane NV12 `420v` / `420f` buffers with a BT.601 or BT.709 matrix attachment.

```swift
let frame = try HiFaceCameraFrame(
    pixelBuffer: pixelBuffer,
    rotation: .degrees90,
    isMirrored: usingFrontCamera
)
let result = sdk.processFrame(frame, configuration: try FaceDetectionConfiguration(
    maxFaces: 5, includesDetailedLandmarks: true
))
```

`processFrame` is synchronous and borrows/locks the pixel buffer only during the call. Reuse it afterward. Initialization, model switching, and native backpressure may return `.notReady`; discard that frame and continue sending the latest frames.

Dimensions must be even, with long side at most `HiFaceCameraFrame.maximumLongSidePixels` (1920) and short side at most `maximumShortSidePixels` (1080). The host selects compatible camera formats. Coordinates belong to the rotated, unmirrored image; the host maps aspect-fit/fill, crop, scaling, and front-camera mirroring.

## Static images and HF1

```swift
let capture = await sdk.capture(cgImage)
let feature = await sdk.extractFeature(from: cgImage)
let similarity = await sdk.compare(feature: leftHF1, with: rightHF1)
let imageSimilarity = await sdk.compare(leftCGImage, with: rightCGImage)
```

Static entry points select the largest face. `capture` returns HF1 and an optional 333×333 aligned preview; extraction alone avoids generating a preview. Compare existing HF1 features to avoid repeated detection and extraction.

Normalize EXIF orientation before creating a `CGImage`; its entry point does not read `UIImage.imageOrientation`. Odd dimensions crop the last row/column only. Final RGBA8888 dimensions must be even and within 2–4096. `HiFaceRGBAImage` copies input pixels. Each instance queues one running and four waiting static requests; further requests return `.busy`. A two-image comparison is one indivisible request.

HF1 is a 604-character Q7/Base64URL representation, **not encryption**. Protect stored images/features according to your application requirements.

## Capture, liveness, and 1:1 verification

Keep sending frames during a camera flow. Only one capture, liveness, verification, or search flow may use the SDK camera slot at a time. Callbacks arrive asynchronously on `MainActor`.

```swift
let request = sdk.startCameraCapture(callbacks: FaceCaptureCallbacks(
    onProgress: { progress in /* Update guidance. */ },
    onResult: { capture in /* Save capture.feature after confirmation. */ },
    onError: { error in /* Handle a technical failure. */ }
))
let callbacks = FaceFlowCallbacks(
    onProgress: { progress in /* Update action/liveness guidance. */ },
    onResult: { result in /* Check result.outcome first. */ },
    onError: { error in /* Handle a technical failure. */ }
)
// Start these separately, after ending any existing camera flow.
let liveness = sdk.startCameraLiveness(
    configuration: try FaceLivenessConfiguration(actions: [.blink, .shakeHead]),
    callbacks: callbacks
)
let verification = sdk.startCameraVerification(
    configuration: try FaceVerificationConfiguration(
        referenceFeature: referenceHF1, requiresSilentLiveness: true,
        actions: [.blink, .shakeHead]),
    callbacks: callbacks
)
```

`onResult` includes both acceptance and rejection. Check `FaceFlowResult.outcome`; technical errors use `onError`. A nil similarity or liveness score means the stage did not produce a result, not zero. Empty actions still run seven-frame passive liveness with both models. Core applies the 0.90 passive liveness threshold; do not repeat the decision in the UI. Use the public configuration defaults and `maximumActionCount` for supported limits.

Stop frame delivery and cancel the active request when leaving, backgrounding, or switching cameras. Then close the SDK. `cancel()` / `close()` suppress stale callbacks immediately and release native resources after in-flight calls finish.

## Face library and continuous search

The database is independent of inference and serializes operations on an actor.

```swift
let database = try HiFaceSDK.openSearchDatabase()
let exists = try await database.contains(faceID: "user-1")
try await database.saveFeature(faceID: "user-1", feature: hf1, group: "staff")
try await database.saveFeatures(featureBatch)
let matches = try await database.search(feature: hf1)
let record = try await database.entry(faceID: "user-1")
let metadata = try await database.recentEntries()
let exportPage = try await database.featureEntries(offset: 0, limit: 100)
try await database.updateMetadata(faceID: "user-1", group: "building-A")
await database.close()
```

The SDK manages a private Application Support database excluded from cloud backup. Face IDs contain 1–1024 UTF-8 bytes; group/tag allow at most 1024 bytes; NUL is not allowed. Save operations upsert by ID; atomic batches accept 1–1000 entries. Check `contains` before prompting for overwrite.

SQLite stores the original 450-byte Q7 payload. Full record/search/feature-page results return complete HF1 without re-quantization; `entries` and `recentEntries` return metadata only. `updateMetadata` preserves nil fields and clears empty fields. Validate server features using `FaceFeatureFormat.featureSpaceID` (`jdx-feature-v1`) and `FaceSearchFeatureEntry`. Compression and iOS file protection are not database encryption.

```swift
let search = sdk.startCameraSearch(configuration: .default, callbacks: FaceSearchCallbacks(
    onFrame: { frame in /* Draw current faces and available scores. */ },
    onProgress: { progress in /* Update guidance. */ },
    onResult: { result in /* Check endReason first. */ },
    onError: { error in /* Recover if appropriate, then resume. */ }
))
search.pause()
search.update(configuration: newConfiguration)
search.resume()
search.close()
```

Search manages its own database connection. Pause retains the models, connection, and flow slot but releases the selected gallery cache after in-flight processing. Resume warms the scope again before accepting new frames. `releaseMemory()` applies to an explicit database handle.

Correlate callbacks by `frameID`. An unscored detection (`similarity == nil`) is not an unmatched result. Use final `endReason`; `.spoofDetected` rejects the result even when similarity passes. After 10 seconds without faces, analysis targets 200 ms intervals; skipped frames return `.notReady`. Search interval controls the wait after successful matching, not camera FPS.

Use `HiFaceError.code` for errors and the result outcome for business decisions.
## Multi-face image search

Use `await sdk.captureFaces(image)` with `HiFaceRGBAImage`. The result contains up to ten `FaceImageCapture` values with input pixel bounds and independent captures/errors.
No faces is a successful empty list; faces smaller than 100px do not prevent other captures.
Pass each successful HF1 to `FaceSearchDatabase.search` for static M:N search. Core owns detection, alignment and inference.

The Demo home screen includes Image Face Search: album selection, original-image boxes, identity/score labels,
aligned thumbnails and detected/matched counts. It uses all records, Top-1 and threshold 0.80, without liveness.
Image search preserves original resolution up to the SDK limit of 4096px per side. Only oversized photos are reduced proportionally; odd last rows/columns are cropped by 1px. The size gate uses the resulting pixels. Comparison and bulk import keep their existing decoding limits.

## IR-only liveness

FLIR accepts upright, unmirrored GRAY8 IR measurements, 1..4096 pixels per side,
including odd dimensions and row padding. Supply a detected face box in this IR image
(x/y + width/height, at least 10x10). Camera access, IR face detection, illumination,
and multiple-face handling belong to the host. RGB converted to gray is not IR.

```swift
let frame = InfraredFrame(gray: grayData, width: width, height: height,
    rowStride: rowStride, faceBounds: boundsInIRImage)
let result = await sdk.evaluateInfraredLiveness(frame,
    realThreshold: calibratedThreshold)
```

Calibrate the explicit Float32 real threshold on the target IR camera. `realScore` is
higher for live faces, `spoofScore = 1-realScore`; pass means `realScore >= threshold`.
Check technical errors before reading the result. Android calls are synchronous on a
worker thread; iOS/Harmony calls use the bounded background queue. Input is owned/copied.
This entry does not change the RGB camera liveness/search flows. iOS requires accessible
IR image data; the SDK does not obtain the internal Face ID IR stream.

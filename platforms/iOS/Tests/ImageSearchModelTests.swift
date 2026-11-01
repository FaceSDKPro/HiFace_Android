import Foundation

// Control SDK completion order while exercising the real Demo model; no photos or database are read.
// 仅控制 SDK 完成顺序，执行真实 Demo 模型；不读取照片或数据库。
typealias CGImage = String
struct HiFaceRGBAImage: Sendable {
    let name: String
    var width: Int { 640 }
    var height: Int { 480 }
    func makeCGImage() -> CGImage? { name }
}
struct HiFaceError: Error, Sendable {
    enum Code: Sendable { case imageFaceTooSmall, processingFailed, unsupportedFormat }
    let code: Code
    let message: String
}
struct FaceBounds: Sendable {}
struct FaceCaptureResult: Sendable {
    let feature: String
    var alignedFace: HiFaceRGBAImage? { nil }
}
struct FaceImageCapture: Sendable {
    let bounds = FaceBounds()
    let capture: Result<FaceCaptureResult, HiFaceError>
}
struct FaceSearchMatch: Sendable {
    struct Entry: Sendable { let faceID: String }
    let entry: Entry
    let score: Float
}
actor FaceSearchDatabase {
    func count() -> UInt64 { 1 }
    func search(feature: String) -> [FaceSearchMatch] {
        [FaceSearchMatch(entry: .init(faceID: feature), score: 0.95)]
    }
    func close() {}
}
enum DemoErrorPresentation {
    static func message(for error: Error) -> String {
        (error as? HiFaceError)?.message ?? error.localizedDescription
    }
}
actor ImageSearchWork {
    private var pending: [String: CheckedContinuation<Result<[FaceImageCapture], HiFaceError>, Never>] = [:]
    func capture(_ image: HiFaceRGBAImage) async -> Result<[FaceImageCapture], HiFaceError> {
        await withCheckedContinuation { pending[image.name] = $0 }
    }
    func hasCapture(_ name: String) -> Bool { pending[name] != nil }
    func complete(_ name: String, with faces: [FaceImageCapture] = []) {
        guard let continuation = pending.removeValue(forKey: name) else {
            preconditionFailure("Missing image request")
        }
        continuation.resume(returning: .success(faces))
    }
}
final class HiFaceSDK: Sendable {
    @MainActor static var latest: HiFaceSDK?
    let work = ImageSearchWork()
    @MainActor init() { Self.latest = self }
    static func openSearchDatabase() -> FaceSearchDatabase { FaceSearchDatabase() }
    func captureFaces(_ image: HiFaceRGBAImage) async -> Result<[FaceImageCapture], HiFaceError> {
        await work.capture(image)
    }
    func close() {}
}

@main struct ImageSearchModelTests {
    @MainActor static func waitUntil(_ condition: () async -> Bool) async throws {
        for _ in 0..<1000 {
            if await condition() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        preconditionFailure("Timed out waiting for image search")
    }
    @MainActor static func flush() async { for _ in 0..<100 { await Task.yield() } }
    static func face(_ id: String) -> FaceImageCapture {
        FaceImageCapture(capture: .success(FaceCaptureResult(feature: id)))
    }

    @MainActor static func main() async throws {
        try await pickerFailureRejectsPendingSearch()
        try await replacingPhotoRejectsOldResult()
        try await stopRejectsPendingSearch()
        try await pickerFailurePreservesCompletedPhoto()
        print("Image search model: 4 cancellation/replacement/preservation regressions passed")
    }

    @MainActor static func pickerFailureRejectsPendingSearch() async throws {
        let model = ImageSearchModel()
        let work = HiFaceSDK.latest!.work
        model.load(HiFaceRGBAImage(name: "old"))
        try await waitUntil { await work.hasCapture("old") }
        model.pickerFailed(HiFaceError(code: .unsupportedFormat, message: "Unreadable replacement"))
        precondition(!model.processing, "Picker failure left the old search running")
        await work.complete("old", with: [face("OLD")])
        await flush()
        precondition(model.status == "Unreadable replacement" && model.faces.isEmpty)
        precondition(model.preview == "old")
    }

    @MainActor static func replacingPhotoRejectsOldResult() async throws {
        let model = ImageSearchModel()
        let work = HiFaceSDK.latest!.work
        model.load(HiFaceRGBAImage(name: "old"))
        try await waitUntil { await work.hasCapture("old") }
        model.load(HiFaceRGBAImage(name: "new"))
        try await waitUntil { await work.hasCapture("new") }
        await work.complete("new", with: [face("NEW")])
        try await waitUntil { !model.processing }
        await work.complete("old", with: [face("OLD")])
        await flush()
        precondition(model.preview == "new" && model.faces.first?.label == "NEW,0.95")
    }

    @MainActor static func stopRejectsPendingSearch() async throws {
        let model = ImageSearchModel()
        let work = HiFaceSDK.latest!.work
        model.load(HiFaceRGBAImage(name: "pending"))
        try await waitUntil { await work.hasCapture("pending") }
        model.stop()
        let status = model.status
        await work.complete("pending", with: [face("LATE")])
        await flush()
        precondition(!model.processing && model.faces.isEmpty && model.status == status)
    }

    @MainActor static func pickerFailurePreservesCompletedPhoto() async throws {
        let model = ImageSearchModel()
        let work = HiFaceSDK.latest!.work
        model.load(HiFaceRGBAImage(name: "ready"))
        try await waitUntil { await work.hasCapture("ready") }
        await work.complete("ready", with: [face("READY")])
        try await waitUntil { !model.processing }
        model.pickerFailed(HiFaceError(code: .unsupportedFormat, message: "Unreadable replacement"))
        precondition(model.preview == "ready" && model.faces.first?.label == "READY,0.95")
        precondition(!model.processing && model.status == "Unreadable replacement")
    }
}

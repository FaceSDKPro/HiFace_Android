import SwiftUI
import UIKit

/// Keep search awake in the foreground; restore brightness and allow automatic lock on exit.
/// 搜索页在前台时始终常亮；离开时恢复亮度并允许系统自动锁屏。
@MainActor
private final class SearchScreenPresentation: ObservableObject {
    private static let activeBrightness: CGFloat = 0.90
    private static let idleBrightness: CGFloat = 0.10
    private static let noFaceIdleNanoseconds: UInt64 = 10_000_000_000
    private var previousBrightness: CGFloat? // 非 nil 表示搜索显示策略生效，也用于恢复原亮度 / A non-nil value indicates the search display policy is active and retains the brightness to restore.
    private var appliedBrightness: CGFloat?
    private var facePresent = false // 检测状态变化用于启动或取消连续无人脸倒计时 / Detection-state changes start or cancel the continuous no-face countdown.
    private var idleTask: Task<Void, Never>?
    var onIdleStateChanged: (@MainActor (Bool) -> Void)?

    func enter(facePresent: Bool) {
        guard previousBrightness == nil else { return }
        previousBrightness = UIScreen.main.brightness
        UIApplication.shared.isIdleTimerDisabled = true
        self.facePresent = facePresent
        setBrightness(Self.activeBrightness)
        onIdleStateChanged?(false)
        if !facePresent { scheduleNoFaceIdle() }
    }

    func updateFacePresence(_ hasFace: Bool) {
        guard previousBrightness != nil, facePresent != hasFace else { return }
        facePresent = hasFace
        idleTask?.cancel()
        idleTask = nil
        if hasFace {
            setBrightness(Self.activeBrightness)
            onIdleStateChanged?(false)
        } else {
            scheduleNoFaceIdle()
        }
    }

    func leave() {
        guard let brightness = previousBrightness else { return }
        idleTask?.cancel()
        idleTask = nil
        previousBrightness = nil
        appliedBrightness = nil
        facePresent = false
        onIdleStateChanged?(false)
        UIScreen.main.brightness = brightness
        // The idle-lock setting affects the whole app; release it when leaving search.
        // 防锁屏开关作用于整个应用，离开搜索页后必须释放。
        UIApplication.shared.isIdleTimerDisabled = false
    }

    private func scheduleNoFaceIdle() {
        idleTask?.cancel()
        idleTask = Task { @MainActor [weak self] in
            try? await Task<Never, Never>.sleep(nanoseconds: Self.noFaceIdleNanoseconds)
            guard !Task.isCancelled, let self,
                  previousBrightness != nil, !facePresent else { return }
            // Match Android/Harmony: reduce backlight and camera-session rate only after ten continuous seconds
            // without faces.
            // 与 Android/Harmony 一致：确认连续 10 秒无人脸后再同时降低背光与相机会话帧率。
            setBrightness(Self.idleBrightness)
            onIdleStateChanged?(true)
        }
    }

    private func setBrightness(_ brightness: CGFloat) {
        guard appliedBrightness != brightness else { return }
        UIScreen.main.brightness = brightness
        appliedBrightness = brightness
    }
}

/// Non-search camera screens temporarily use 90% on each foreground entry and restore that entry's
/// previous brightness on exit.
/// 非搜索相机页每次进入前台时临时设为 90% 亮度，离开前台时恢复当次进入前的值。
@MainActor
private final class CameraScreenBrightnessPresentation: ObservableObject {
    private static let activeBrightness: CGFloat = 0.90
    private var previousBrightness: CGFloat?

    func enter() {
        guard previousBrightness == nil else { return }
        previousBrightness = UIScreen.main.brightness
        UIScreen.main.brightness = Self.activeBrightness
    }

    func leave() {
        guard let brightness = previousBrightness else { return }
        previousBrightness = nil
        UIScreen.main.brightness = brightness
    }
}

/// Manage camera-screen lifecycle centrally; feature screens and overlays remain in their own groups.
/// 统一管理相机页面生命周期；各能力的页面和 Overlay 位于对应功能分组。
struct CameraDemoView: View {
    let mode: CameraDemoMode
    @StateObject private var model: CameraDemoModel
    @StateObject private var searchScreenPresentation = SearchScreenPresentation()
    @StateObject private var cameraScreenBrightness = CameraScreenBrightnessPresentation()
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.dismiss) private var dismiss
    @State private var isVisible = false
    @State private var showsReferenceCapture = false
    @State private var presentsResultAlert = false
    @Environment(\.openURL) private var openURL

    init(mode: CameraDemoMode, enrollsInSearchLibrary: Bool = false) {
        self.mode = mode
        _model = StateObject(wrappedValue: CameraDemoModel(mode: mode, enrollsInSearchLibrary: enrollsInSearchLibrary))
    }

    private var isGuidedFlow: Bool { mode != .detection && mode != .search }

    var body: some View {
        Group {
            switch mode {
            case .detection: FaceDetectionView(model: model, onClose: closePage)
            case .capture: FaceCaptureView(model: model, onClose: closePage)
            case .verification: FaceVerificationView(model: model, onClose: closePage)
            case .liveness: FaceLivenessView(model: model, onClose: closePage)
            case .search: FaceSearchView(model: model, onClose: closePage)
            }
        }
        .navigationBarHidden(true)
        .overlay(alignment: .bottomLeading) {
            #if DEBUG
            if ProcessInfo.processInfo.arguments.contains("--camera-lifecycle-test") {
                Text("Camera frame")
                    .font(.caption2)
                    .accessibilityIdentifier("camera.liveFrame")
                    .accessibilityValue(String(model.cameraTestFrameTimestamp))
                    .allowsHitTesting(false)
                // Fresh camera frames alone do not prove the SDK flow recovered.
                // 只有相机新帧不能证明 SDK 流程也已恢复。
                Text("Camera phase")
                    .font(.caption2)
                    .accessibilityIdentifier("camera.phase")
                    .accessibilityValue(String(describing: model.phase))
                    .allowsHitTesting(false)
            }
            #endif
        }
        .preferredColorScheme(isGuidedFlow ? .light : .dark)
        .background(
            NavigationLink(isActive: $showsReferenceCapture) {
                LazyDestination { CameraDemoView(mode: .capture) }
            } label: { EmptyView() }
        )
        .onAppear {
            presentsResultAlert = false
            isVisible = true
            if mode == .search {
                searchScreenPresentation.onIdleStateChanged = { [weak model] idle in
                    model?.setSearchCameraIdle(idle)
                }
            }
            activateCameraPage(for: scenePhase)
        }
        .onDisappear {
            isVisible = false
            presentsResultAlert = false
            deactivateCameraPage()
            searchScreenPresentation.onIdleStateChanged = nil
        }
        .onChange(of: scenePhase) { phase in
            guard isVisible else { return }
            if phase == .active {
                activateCameraPage(for: phase)
            } else {
                // For calls, Control Center, permission prompts, and backgrounding, stop the camera before restoring
                // the foreground-entry display policy.
                // 来电、控制中心、权限弹窗和后台都先停相机，再恢复进入前台前的显示策略。
                suspendCameraPage()
            }
        }
        .onChange(of: model.searchDetectedCount) { count in
            if mode == .search { searchScreenPresentation.updateFacePresence(count > 0) }
        }
        .onChange(of: model.searchFrame?.frameID) { _ in
            guard mode == .search else { return }
            // Face-count progress is deduplicated; synchronize empty/nonempty state each frame so resume or
            // callback coalescing cannot leave idle timing blocked.
            // 人数进度会去重；逐帧同步空/非空状态，避免恢复或回调合并后旧状态阻止待机计时。
            searchScreenPresentation.updateFacePresence(model.searchFrame?.faces.isEmpty == false)
        }
        .onChange(of: shouldPresentResult) { presentsResultAlert = $0 }
        .demoDialog(isPresented: presentsResultAlert, onDismiss: closePage) {
            CameraDialogTitle(
                title: model.phase == .needsReference ? DemoL10n.text("请先完成人脸采集") : model.status,
                subtitle: model.detail ?? "",
                symbol: model.phase == .completed ? "checkmark.circle" : "exclamationmark.circle"
            )
            if model.needsCameraSettings, let url = URL(string: UIApplication.openSettingsURLString) {
                Button(DemoL10n.text("打开设置")) {
                    presentsResultAlert = false
                    openURL(url)
                }
            } else if model.phase == .needsReference {
                Button(DemoL10n.text("录入参考人脸")) {
                    presentsResultAlert = false
                    showsReferenceCapture = true
                }
            } else if model.failureAllowsRetry {
                Button(DemoL10n.text("再试一次"), action: retryFlow)
            }
            Button(DemoL10n.text("关闭"), role: .cancel, action: closePage)
        }
        .demoDialog(isPresented: model.confirmsSearchOverwrite,
                    onDismiss: { model.confirmsSearchOverwrite = false }) {
            CameraDialogTitle(
                title: DemoL10n.text("该 ID 已存在"),
                subtitle: DemoL10n.text("确认覆盖 {0} 的人脸特征和分组？", [String(describing: model.enrollmentFaceID)]),
                symbol: "exclamationmark.triangle"
            )
            Button(DemoL10n.text("确认覆盖"), role: .destructive) {
                model.confirmsSearchOverwrite = false
                model.saveSearchCapture(replace: true)
            }
            Button(DemoL10n.text("取消"), role: .cancel) { model.confirmsSearchOverwrite = false }
        }
    }

    private var shouldPresentResult: Bool {
        guard isVisible, !showsReferenceCapture, isGuidedFlow,
              model.pendingReferenceFeature == nil else { return false }
        return model.phase == .completed || model.phase == .failed || model.phase == .needsReference
    }

    /**
     * On becoming active, remember current display settings and apply overrides before starting the
     * camera.
     *
     * 前台活动时重新记录当前显示策略；应用覆盖生效后再启动相机。
     */
    private func activateCameraPage(for phase: ScenePhase) {
        // The single-argument onChange invokes an old closure whose scenePhase may still be inactive. Use the
        // event's new state to resume camera and display policy correctly.
        // 单参数 onChange 调用旧闭包，捕获的 scenePhase 仍可能是 inactive。
        // 必须使用事件传入的新状态，否则回前台时会跳过相机和显示策略的恢复。
        guard isVisible, phase == .active else { return }
        if mode == .search {
            searchScreenPresentation.enter(facePresent: model.searchDetectedCount > 0)
        } else {
            cameraScreenBrightness.enter()
        }
        model.start()
    }

    /**
     * Foreground/background transitions pause resources only; keep search and resume it after camera
     * recovery.
     *
     * 前后台切换只暂停资源，搜索会话保持并在相机恢复后继续。
     */
    private func suspendCameraPage() {
        model.suspend()
        leaveCameraPresentation()
    }

    /**
     * Invalidate camera and late callbacks before cancelling timers, restoring brightness, and releasing
     * keep-awake.
     *
     * 先使相机和晚到回调失效，再取消计时、恢复亮度并释放防锁屏。
     */
    private func deactivateCameraPage() {
        model.stop()
        leaveCameraPresentation()
    }

    private func leaveCameraPresentation() {
        if mode == .search {
            searchScreenPresentation.leave()
        } else {
            cameraScreenBrightness.leave()
        }
    }

    private func closePage() {
        guard isVisible else { return }
        // Dismiss result presentation before navigation animation starts; do not wait for onDisappear to
        // change derived state.
        // 导航动画开始前立即关闭结果层，不能等待 onDisappear 再改变派生状态。
        presentsResultAlert = false
        isVisible = false
        deactivateCameraPage()
        dismiss()
    }

    private func retryFlow() {
        presentsResultAlert = false
        if mode == .verification, model.cameraReady, model.selectedReferenceID != nil {
            model.beginVerification()
        } else {
            // Use a full restart if the camera has not resumed, avoiding a dismissed dialog with the page still
            // completed/failed.
            // 相机尚未恢复时走完整重启，避免弹框已关闭但页面仍停在 completed/failed。
            model.restartFlow()
        }
    }

}

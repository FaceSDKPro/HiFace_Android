import AVFoundation
import Foundation
import SwiftUI
import UIKit

/// All camera screens and overlays share aspect-fit rules to avoid inconsistent cropping or zoom
/// between screens.
/// 所有相机页面与 Overlay 共用完整等比显示规则，避免页面间裁剪放大不一致。
enum CameraPreviewLayout {
    static let gravity: AVLayerVideoGravity = .resizeAspect

    static func scale(view: CGSize, image: CGSize) -> CGFloat {
        min(view.width / image.width, view.height / image.height)
    }
}

final class CameraPreviewView: UIView {
    override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
    var previewLayer: AVCaptureVideoPreviewLayer { layer as! AVCaptureVideoPreviewLayer }
}

struct CameraPreview: UIViewRepresentable {
    let session: AVCaptureSession
    let mirrored: Bool

    func makeUIView(context: Context) -> CameraPreviewView {
        let view = CameraPreviewView()
        view.backgroundColor = .black
        // VoiceOver needs only screen status and action buttons; the raw camera layer has no actionable
        // semantics.
        // VoiceOver 读取页面状态和操作按钮即可；原始相机图层本身没有可操作语义。
        view.isAccessibilityElement = false
        view.accessibilityElementsHidden = true
        view.previewLayer.session = session
        view.previewLayer.videoGravity = CameraPreviewLayout.gravity
        return view
    }

    func updateUIView(_ view: CameraPreviewView, context: Context) {
        guard let connection = view.previewLayer.connection else { return }
        // SwiftUI updates this view for each result; configure the connection only when a value changes.
        // SwiftUI 会随检测结果更新此视图；仅在值改变时配置连接，避免逐帧重复设置。
        if connection.isVideoOrientationSupported, connection.videoOrientation != .portrait {
            connection.videoOrientation = .portrait
        }
        if connection.isVideoMirroringSupported {
            if connection.automaticallyAdjustsVideoMirroring {
                connection.automaticallyAdjustsVideoMirroring = false
            }
            if connection.isVideoMirrored != mirrored { connection.isVideoMirrored = mirrored }
        }
    }

    static func dismantleUIView(_ view: CameraPreviewView, coordinator: ()) {
        // Detach this preview; the screen model owns stopping the shared capture session.
        // 这里只解除预览绑定；共享采集会话由页面模型负责停止。
        view.previewLayer.session = nil
    }
}

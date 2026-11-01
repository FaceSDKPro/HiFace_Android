import SwiftUI

/// 1:N/M:N search example: the screen owns session state while the shared coordinator owns frame
/// delivery and camera lifecycle.
/// 1:N/M:N 搜索接入示例：页面持有会话状态，送帧和相机生命周期由共享协调器负责。
struct FaceSearchView: View {
    @ObservedObject var model: CameraDemoModel
    let onClose: () -> Void
    @State private var showsSearchSettings = false
    @State private var showsResultDetails = false

    var body: some View {
        VStack(spacing: 0) {
            ZStack(alignment: .top) {
                CameraPreview(session: model.pipeline.captureSession, mirrored: model.previewMirrored)
                FaceSearchOverlayView(overlay: model.searchOverlay, mirrored: model.overlayMirrored)
                VStack {
                    DemoCameraControls(model: model, onClose: onClose,
                                       showsResolution: false,
                                       onSwitchCamera: model.switchCamera,
                                       onSearchSettings: { showsSearchSettings = true })
                    Spacer()
                    if model.searchDetectedCount > 1 {
                        Text(model.searchConfiguration.mode == .singleFace
                             ? DemoL10n.text("检测到 {0} 人 · 优先识别最大脸", [String(describing: model.searchDetectedCount)])
                             : DemoL10n.text("检测到 {0} 人 · 本次处理 {1} 人", [String(describing: model.searchDetectedCount), String(describing: model.searchSelectedCount)]))
                            .font(.caption2)
                            .padding(10)
                            .background(DemoTheme.searchSurface.opacity(0.85), in: Capsule())
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(12)
                    }
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .clipped()
            VStack(spacing: 10) {
                HStack(spacing: 8) {
                    Button { showsResultDetails = true } label: {
                        VStack(alignment: .leading, spacing: 4) {
                            HStack(spacing: 7) {
                                Circle().fill(model.phase == .failed ? Color.red : DemoTheme.searchAccent)
                                    .frame(width: 6, height: 6)
                                Text(model.searchOptions).font(.body.weight(.semibold))
                                    .lineLimit(1).minimumScaleFactor(0.75)
                                    .accessibilityIdentifier("search.caption.mode")
                            }.frame(height: 26)
                            Text(resultCaption).font(.headline)
                                .lineLimit(1).truncationMode(.middle)
                                .frame(maxWidth: .infinity, minHeight: 26, maxHeight: 26, alignment: .leading)
                                .accessibilityIdentifier("search.caption.result")
                        }.frame(maxWidth: .infinity, alignment: .leading)
                    }.buttonStyle(.plain)
                    if model.phase == .failed {
                        if model.needsCameraSettings { CameraSettingsLink() }
                        else if model.failureAllowsRetry {
                            Button(DemoL10n.text("重试")) { model.resumeSearch() }
                                .foregroundStyle(DemoTheme.searchAccent)
                        }
                    }
                }
                // Two fixed rows keep the preview stable as scores and errors change.
                // 状态、评分和错误切换时始终固定两行，不挤动摄像头预览。
                .frame(height: 56)
                .padding(.horizontal, 12).padding(.vertical, 8)
                .background(DemoTheme.searchSurface, in: RoundedRectangle(cornerRadius: 12))
                HStack(spacing: 10) {
                    NavigationLink(destination: LazyDestination {
                        CameraDemoView(mode: .capture, enrollsInSearchLibrary: true)
                    }) {
                        Label(DemoL10n.text("录入人脸"), systemImage: "plus")
                            .frame(maxWidth: .infinity, minHeight: 48)
                            .foregroundStyle(DemoTheme.search)
                            .background(DemoTheme.searchAccent, in: RoundedRectangle(cornerRadius: 10))
                    }
                    NavigationLink(destination: LazyDestination { SearchLibraryView() }) {
                        Label(DemoL10n.text("人脸库"), systemImage: "person.3")
                            .frame(maxWidth: .infinity, minHeight: 48)
                            .foregroundStyle(DemoTheme.searchAccent)
                            .background(DemoTheme.searchSurface, in: RoundedRectangle(cornerRadius: 10))
                    }
                }
                .font(.subheadline.weight(.semibold))
                .buttonStyle(.plain)
            }
            .padding(.horizontal, 16)
            .padding(.top, 10)
            .padding(.bottom, 12)
        }
        .foregroundStyle(.white)
        .background(DemoTheme.search.ignoresSafeArea())
        .sheet(isPresented: $showsSearchSettings) {
            SearchSettingsView(configuration: model.searchConfiguration) {
                model.updateSearchConfiguration($0)
            }
        }
        .sheet(isPresented: $showsResultDetails) {
            NavigationView {
                ScrollView { Text(fullDetails).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).padding() }
                    .navigationTitle(DemoL10n.text("人脸搜索识别"))
                    .toolbar { Button(DemoL10n.text("返回")) { showsResultDetails = false } }
            }
        }
    }

    private var resultCaption: String {
        let results = model.searchOverlay.confirmedResults
        let best = model.searchOverlay.bestMatchCaption(includeLiveness: model.searchConfiguration.needsLiveness)
        let value = model.phase != .failed && model.cameraNotice == nil && !results.isEmpty
            ? best ?? results.map(\.title).joined(separator: " · ")
            : model.cameraNotice ?? model.detail ?? model.status
        return value.replacingOccurrences(of: "\r", with: " ").replacingOccurrences(of: "\n", with: " ")
    }

    private var fullDetails: String {
        ([model.searchOptions, model.status, model.cameraNotice ?? model.detail ?? ""]
            + model.searchOverlay.confirmedResults.map { $0.title + "\n" + $0.details }).joined(separator: "\n")
    }

}

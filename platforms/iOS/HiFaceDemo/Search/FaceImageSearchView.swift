import Foundation
import HiFace
import SwiftUI

private struct ImageSearchFace: Identifiable {
    let id: Int
    let bounds: FaceBounds
    let crop: CGImage?
    // Complete ID/score or error text shared by the photo and thumbnails.
    // 完整的“ID,分数”或错误状态，照片与缩略图共用。
    let label: String
    let matched: Bool
}

@MainActor
private final class ImageSearchModel: ObservableObject {
    @Published private(set) var preview: CGImage?
    @Published private(set) var imageSize = CGSize.zero
    @Published private(set) var faces: [ImageSearchFace] = []
    @Published private(set) var status = DemoL10n.text("选择一张包含人脸的图片")
    @Published private(set) var processing = false
    private let sdk = HiFaceSDK()
    private var task: Task<Void, Never>?
    // Increment on image change or screen exit; asynchronous model/database results must belong to the
    // current generation.
    // 换图和离开页面时递增，异步模型/数据库结果必须属于当前这一轮。
    private var version = 0

    func load(_ image: HiFaceRGBAImage) {
        task?.cancel()
        version += 1
        let token = version
        preview = image.makeCGImage()
        imageSize = CGSize(width: image.width, height: image.height)
        faces = []
        processing = true
        status = DemoL10n.text("正在检测、矫正并搜索…")
        task = Task { [weak self, sdk] in
            let extracted = await sdk.captureFaces(image)
            guard let self, !Task.isCancelled, token == version else { return }
            defer { if token == version { task = nil } }
            do {
                let captures = try extracted.get()
                if captures.isEmpty {
                    processing = false
                    status = DemoL10n.text("未检测到人脸，请更换清晰图片")
                    return
                }
                let database = try await Task.detached { try HiFaceSDK.openSearchDatabase() }.value
                var output: [ImageSearchFace] = []
                let count: UInt64
                do {
                    count = try await database.count()
                    for (index, face) in captures.enumerated() {
                        try Task.checkCancellation()
                        var label: String
                        var matched = false
                        var crop: CGImage?
                        switch face.capture {
                        case .failure(let error):
                            label = error.code == .imageFaceTooSmall
                                ? DemoL10n.text("人脸太小") : DemoErrorPresentation.message(for: error)
                        case .success(let capture):
                            crop = capture.alignedFace?.makeCGImage()
                            do {
                                if let match = try await database.search(feature: capture.feature).first {
                                    label = match.entry.faceID + "," + String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), match.score)
                                    matched = true
                                } else { label = DemoL10n.text("未匹配") }
                            } catch let error as HiFaceError { label = DemoErrorPresentation.message(for: error) }
                        }
                        output.append(ImageSearchFace(id: index, bounds: face.bounds,
                                                      crop: crop, label: label, matched: matched))
                    }
                    await database.close()
                } catch {
                    await database.close()
                    throw error
                }
                guard !Task.isCancelled, token == version else { return }
                faces = output
                processing = false
                status = DemoL10n.text("检测到 {0} 个人脸，搜索匹配成功 {1} 个",
                                      [String(output.count), String(output.filter(\.matched).count)])
                if count == 0 { status += "\n" + DemoL10n.text("人脸库为空，请先到人脸库录入") }
            } catch {
                guard !Task.isCancelled, token == version else { return }
                processing = false
                status = DemoErrorPresentation.message(for: error)
            }
        }
    }

    func pickerFailed(_ error: HiFaceError) {
        // A failed replacement also invalidates the previous search, while retaining its displayed photo.
        // 换图失败也要使上一轮搜索失效，同时保留当前展示的照片。
        stop()
        status = DemoErrorPresentation.message(for: error)
    }
    func stop() {
        task?.cancel(); task = nil; version += 1
        if processing {
            processing = false
            status = DemoL10n.text("选择一张包含人脸的图片")
        }
    }
    deinit { sdk.close() }
}

struct FaceImageSearchView: View {
    @StateObject private var model = ImageSearchModel()
    @State private var picking = false
    @State private var selected: Int?
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 12) {
            Text(DemoL10n.text("最多检测 10 张脸 · 匹配阈值 0.80 · 不检测活体"))
                .font(.caption).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
            photo
            if !model.faces.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(alignment: .top, spacing: 12) {
                        ForEach(model.faces) { face in
                            Button { selected = face.id } label: {
                                VStack(spacing: 5) {
                                    if let crop = face.crop {
                                        Image(decorative: crop, scale: 1).resizable().frame(width: 72, height: 72).cornerRadius(10)
                                    } else { Image(systemName: "person.crop.square").font(.largeTitle).foregroundStyle(.secondary).frame(width: 72, height: 72) }
                                    Text(face.label).font(.system(size: 11)).lineLimit(2).frame(width: 96, height: 30)
                                }
                                .padding(8)
                                .background(Color.white, in: RoundedRectangle(cornerRadius: 14))
                                .overlay(RoundedRectangle(cornerRadius: 14).stroke(
                                    selected == face.id ? DemoTheme.contentAccent : Color.gray.opacity(0.15), lineWidth: 1))
                            }.buttonStyle(.plain)
                        }
                    }
                }.frame(height: 128)
            }
            Text(model.status).font(.system(size: 13)).foregroundStyle(.secondary).multilineTextAlignment(.center)
                .accessibilityIdentifier("imageSearch.status")
            Button { picking = true } label: {
                Label(DemoL10n.text("相册添加"), systemImage: "photo.badge.plus")
                    .font(.headline).frame(maxWidth: .infinity, minHeight: 56)
                    .foregroundStyle(.white).background(DemoTheme.contentAccent, in: RoundedRectangle(cornerRadius: 16))
            }.buttonStyle(.plain)
        }
        .padding(.horizontal, 20).padding(.top, 12).padding(.bottom, 12)
        .safeAreaInset(edge: .top, spacing: 0) { navigationHeader }
        .background(Color(uiColor: .systemGroupedBackground).ignoresSafeArea())
        .tint(DemoTheme.contentAccent)
        .foregroundStyle(Color(uiColor: .label))
        .environment(\.colorScheme, .light).preferredColorScheme(.light)
        .navigationBarHidden(true)
        .sheet(isPresented: $picking) {
            FacePhotoPicker(maximumPixelSize: FacePhotoPicker.imageSearchMaximumPixelSize) { result in
                picking = false
                switch result {
                case .success(let image): selected = nil; model.load(image)
                case .failure(let error): model.pickerFailed(error)
                case nil: break
                }
            }
        }
        .onDisappear(perform: model.stop)
    }

    private var navigationHeader: some View {
        HStack(spacing: 10) {
            // Reuse the camera module's back control and accessibility label.
            // 与相机模块复用同一个返回控件和无障碍标签。
            DemoCameraButton(icon: "chevron.backward", label: DemoL10n.text("返回"), onDark: true) { dismiss() }
            Text(DemoL10n.text("图片人脸搜索识别")).font(.headline).lineLimit(1).minimumScaleFactor(0.8)
            Spacer(minLength: 0)
            NavigationLink(destination: LazyDestination { SearchLibraryView() }) {
                Label(DemoL10n.text("人脸库"), systemImage: "person.3")
                    .font(.system(size: 13, weight: .semibold)).padding(.horizontal, 10)
                    .frame(minHeight: 44).background(.white.opacity(0.12), in: RoundedRectangle(cornerRadius: 12))
            }.accessibilityIdentifier("imageSearch.library")
        }
        .padding(.horizontal, 16).frame(minHeight: 60).foregroundStyle(.white).tint(.white)
        .background(DemoTheme.home.ignoresSafeArea(edges: .top))
    }

    private var photo: some View {
        GeometryReader { geometry in
            ZStack(alignment: .topLeading) {
                Color.white
                if let image = model.preview {
                    Image(decorative: image, scale: 1).resizable().scaledToFit()
                        .frame(width: geometry.size.width, height: geometry.size.height)
                    // Keep detection boxes in original image pixels; apply aspect-fit mapping only for display.
                    // 检测框沿用原图像素坐标，只在显示层做 aspect-fit 映射。
                    let scale = min(geometry.size.width / model.imageSize.width,
                                    geometry.size.height / model.imageSize.height)
                    let offsetX = (geometry.size.width - model.imageSize.width * scale) / 2
                    let offsetY = (geometry.size.height - model.imageSize.height * scale) / 2
                    ForEach(model.faces) { face in
                        let rect = CGRect(x: offsetX + CGFloat(face.bounds.x) * scale,
                                          y: offsetY + CGFloat(face.bounds.y) * scale,
                                          width: CGFloat(face.bounds.width) * scale,
                                          height: CGFloat(face.bounds.height) * scale)
                        Rectangle().stroke(face.matched ? Color.green : Color.orange,
                                           lineWidth: selected == face.id ? 4 : 2)
                            .frame(width: rect.width, height: rect.height)
                            .position(x: rect.midX, y: rect.midY)
                        let labelWidth = min(max(96, rect.width), geometry.size.width)
                        Text(face.label).font(.system(size: 10).monospacedDigit()).lineLimit(1)
                            .padding(.horizontal, 4).padding(.vertical, 2)
                            .foregroundStyle(.white).background(.black.opacity(0.72), in: RoundedRectangle(cornerRadius: 3))
                            .frame(maxWidth: labelWidth, alignment: .leading)
                            .offset(x: max(0, min(rect.minX, geometry.size.width - labelWidth)),
                                    y: max(0, rect.minY - 17))
                    }
                } else {
                    VStack(spacing: 12) {
                        Image(systemName: "photo.on.rectangle.angled").font(.largeTitle)
                        Text(DemoL10n.text("选择照片")).font(.subheadline)
                    }.foregroundStyle(.secondary).frame(width: geometry.size.width, height: geometry.size.height)
                }
                if model.processing {
                    ProgressView().padding(14).background(.regularMaterial, in: Capsule())
                        .frame(width: geometry.size.width, height: geometry.size.height)
                }
            }.clipShape(RoundedRectangle(cornerRadius: 18))
                .overlay(RoundedRectangle(cornerRadius: 18).stroke(Color.gray.opacity(0.15), lineWidth: 1))
        }.frame(minHeight: 160, maxHeight: .infinity)
    }
}

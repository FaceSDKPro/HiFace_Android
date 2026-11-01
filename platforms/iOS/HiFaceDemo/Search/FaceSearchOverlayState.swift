import Foundation
import HiFace

/// Cache scores only for drawing; business decisions always follow final SDK results. Match Android:
/// boxes live 1.2 seconds, scores 1 second. Borrow scores only across continuous frames within 300 ms
/// with mutually unique, uncrowded boxes. This affects drawing only, not identity tracking.
/// 只缓存人脸框的显示评分。业务结论始终以 SDK 的最终结果为准。
///
/// 与 Android Demo 一致：检测框保留 1.2 秒，评分保留 1 秒；只有连续 300 毫秒内
/// 双向唯一且不拥挤的框才能借用旧评分。借用仅影响绘制，不是身份跟踪。
struct FaceSearchOverlayState {
    enum BoxColor: Equatable {
        case pending, unmatched, matched
    }

    struct Box {
        let color: BoxColor
        let title: String
        let details: String
    }

    private struct Score {
        let face: FaceSearchFace
        let frameID: UInt64
        let deadline: TimeInterval
        let box: Box
        let finalResult: Bool
        let livenessRejected: Bool
        let livenessScore: Float?

        init(face: FaceSearchFace, frameID: UInt64, deadline: TimeInterval,
             livenessScore: Float?, livenessRejected: Bool, finalResult: Bool) {
            self.face = face
            self.frameID = frameID
            self.deadline = deadline
            self.finalResult = finalResult
            self.livenessRejected = livenessRejected
            self.livenessScore = livenessScore
            let faceID = face.thresholdPassed ? face.faceID : nil
            // Only an actual lookup score can establish mismatch; low scores remain in SDK results.
            // 只有实际查库评分才能判定未匹配，低分仍保留在 SDK 结果中。
            let identity = faceID ?? (face.similarity != nil ? DemoL10n.text("未匹配") : "")
            let title = livenessRejected
                ? (identity.isEmpty ? "" : "\(identity) · ") + DemoL10n.text("活体未通过")
                : identity
            var details: [String] = []
            if faceID != nil, let similarity = face.similarity {
                details.append(String(format: DemoL10n.text("相似度 %.3f"), similarity))
            }
            if let livenessScore {
                details.append(String(format: DemoL10n.text("活体 %.3f"), livenessScore))
            }
            let color: BoxColor = livenessRejected ? .unmatched
                : face.thresholdPassed && !finalResult ? .pending
                : face.thresholdPassed ? .matched : .unmatched
            // Reuse formatted text throughout a score's lifetime instead of formatting floats on every Canvas
            // draw.
            // 评分生命周期内复用文案，不在每次 Canvas 绘制时重复格式化浮点数。
            box = Box(color: color, title: title, details: details.joined(separator: " · "))
        }
    }

    private(set) var frame: FaceSearchFrame?
    private(set) var newestFrameID: UInt64?
    private var scores: [Score?] = []
    // Search selects at most five faces; bit flags avoid clearing/rebuilding Set hash storage on every
    // frame.
    // 搜索最多五张脸；位标记避免每个新帧清空/重建 Set 的哈希存储。
    private var scoredThisFrame: UInt64 = 0
    private var rejectedThrough: UInt64?
    private var completedThrough: UInt64?
    private var frameDeadline: TimeInterval = 0
    private var lastFrameAt: TimeInterval = 0
    private var associationEnabled = true

    var hasVisibleFaces: Bool { frame?.faces.isEmpty == false }

    // The card shares box expiry and confirmation; full IDs never come from an unrelated last result.
    // 卡片与人脸框共用到期和确认规则，完整 ID 不能取自无关的上一轮结果。
    var confirmedResults: [Box] {
        scores.compactMap { value in
            guard let value, value.finalResult else { return nil }
            let title = value.livenessRejected ? DemoL10n.text("活体未通过")
                : value.box.color == .matched ? "Face ID: \(value.face.faceID ?? "")"
                : DemoL10n.text("未匹配")
            return Box(color: value.box.color, title: title, details: value.box.details)
        }
    }

    var confirmedLivenessScore: Float? {
        for value in scores {
            if let value, value.finalResult, let score = value.livenessScore { return score }
        }
        return nil
    }

    var expiresAt: TimeInterval? {
        guard let frame, !frame.faces.isEmpty else { return nil }
        var deadline = frameDeadline
        for score in scores {
            if let score { deadline = min(deadline, score.deadline) }
        }
        return deadline
    }

    func box(at index: Int) -> Box {
        guard scores.indices.contains(index), let score = scores[index] else {
            return Box(color: .pending, title: "", details: "")
        }
        return score.box
    }

    @discardableResult
    mutating func accept(_ value: FaceSearchFrame, now: TimeInterval) -> Bool {
        if let newestFrameID, value.frameID < newestFrameID { return false }
        let wasVisible = hasVisibleFaces
        expire(now: now)
        let sameFrame = value.frameID == newestFrameID
        // An expired frame's late score cannot revive its vanished boxes.
        // 已过期的同帧晚到评分不能使消失的框重新出现。
        if sameFrame && frame == nil { return wasVisible }
        let previousFrame = frame
        let continuous = previousFrame != nil
            && value.imageWidth == previousFrame?.imageWidth
            && value.imageHeight == previousFrame?.imageHeight
            && now - lastFrameAt <= 0.3
        if !sameFrame { scoredThisFrame = 0 }
        var next = Array<Score?>(repeating: nil, count: value.faces.count)
        for (index, face) in value.faces.enumerated() {
            let previousIndex: Int? = sameFrame ? index
                : continuous && associationEnabled
                    ? Self.associate(value.faces, at: index, with: previousFrame!.faces) : nil
            let old = previousIndex.flatMap { scores.indices.contains($0) ? scores[$0] : nil }
            if face.similarity != nil && (rejectedThrough.map { value.frameID > $0 } ?? true) {
                // Duplicate callbacks for one frame do not extend score display lifetime.
                // 同帧重复回调不延长评分显示期限。
                let bit = UInt64(1) << index
                next[index] = scoredThisFrame & bit != 0 ? old
                    : Score(face: face, frameID: value.frameID, deadline: now + 1,
                            livenessScore: nil, livenessRejected: false, finalResult: false)
                scoredThisFrame |= bit
            } else {
                next[index] = old
            }
        }
        if !sameFrame {
            frameDeadline = now + 1.2
            lastFrameAt = now
        }
        newestFrameID = value.frameID
        frame = value
        scores = next
        return wasVisible || !value.faces.isEmpty
    }

    @discardableResult
    mutating func complete(_ result: FaceSearchResult, now: TimeInterval) -> Bool {
        var changed = expire(now: now)
        let resultFrame = result.frame
        let frameID = resultFrame.frameID
        guard let displayed = frame, let newestFrameID, frameID <= newestFrameID else { return changed }
        if let completedThrough, frameID <= completedThrough { return changed }
        completedThrough = frameID
        let rejected = result.endReason == .spoofDetected
        if rejected {
            if frameID > 0 { changed = rejectScores(through: frameID - 1) || changed }
            rejectedThrough = max(rejectedThrough ?? 0, frameID)
        } else if !resultFrame.faces.contains(where: \.thresholdPassed) {
            let boundary = result.endReason == .timeout && frameID > 0 ? frameID - 1 : frameID
            changed = rejectScores(through: boundary) || changed
        }
        guard resultFrame.imageWidth == displayed.imageWidth,
              resultFrame.imageHeight == displayed.imageHeight else { return changed }
        var updated = false
        for index in scores.indices {
            guard let old = scores[index], old.frameID == frameID else { continue }
            scores[index] = Score(face: old.face, frameID: frameID, deadline: old.deadline,
                                  livenessScore: result.livenessScore,
                                  livenessRejected: rejected, finalResult: true)
            updated = true
            changed = true
        }
        // If liveness rejects before intermediate scoring, the current sole face may still display its
        // liveness score.
        // 活体拒绝未收到中途评分时，当前唯一的人脸仍可展示活体分数。
        if rejected && !updated && frameID == newestFrameID && scoredThisFrame & 1 == 0
            && resultFrame.faces.count == 1 && displayed.faces.count == 1 {
            scores[0] = Score(face: resultFrame.faces[0], frameID: frameID,
                              deadline: now + 1, livenessScore: result.livenessScore,
                              livenessRejected: true, finalResult: true)
            scoredThisFrame |= 1
            changed = true
        }
        // A current negative result remains readable without reviving an older identity.
        // 当前未匹配结果应可读，但不能恢复旧身份。
        if result.endReason == .timeout, frameID == newestFrameID,
           resultFrame.faces.count == displayed.faces.count {
            for index in scores.indices where scores[index] == nil && !resultFrame.faces[index].thresholdPassed {
                scores[index] = Score(face: resultFrame.faces[index], frameID: frameID, deadline: now + 1,
                                      livenessScore: result.livenessScore, livenessRejected: false, finalResult: true)
                changed = true
            }
        }
        return changed
    }

    @discardableResult
    mutating func setAssociationEnabled(_ enabled: Bool) -> Bool {
        guard associationEnabled != enabled else { return false }
        var changed = false
        associationEnabled = enabled
        for index in scores.indices where scores[index].map({ $0.frameID < (newestFrameID ?? 0) }) == true {
            scores[index] = nil
            changed = true
        }
        lastFrameAt = -0.301
        return changed
    }

    @discardableResult
    mutating func rejectScores(through frameID: UInt64) -> Bool {
        var changed = false
        rejectedThrough = max(rejectedThrough ?? 0, frameID)
        for index in scores.indices where scores[index].map({ $0.frameID <= frameID }) == true {
            scores[index] = nil
            changed = true
        }
        return changed
    }

    @discardableResult
    mutating func expire(now: TimeInterval) -> Bool {
        var changed = false
        if frame != nil && now >= frameDeadline {
            changed = hasVisibleFaces
            frame = nil
            scores.removeAll()
            frameDeadline = 0
        } else {
            for index in scores.indices where scores[index].map({ now >= $0.deadline }) == true {
                scores[index] = nil
                changed = true
            }
        }
        return changed
    }

    mutating func clear() {
        self = Self()
    }

    private static func associate(_ current: [FaceSearchFace], at index: Int,
                                  with previous: [FaceSearchFace]) -> Int? {
        guard !crowded(current, at: index) else { return nil }
        var match: Int?
        for candidate in previous.indices where near(current[index].bounds, previous[candidate].bounds) {
            guard match == nil else { return nil }
            match = candidate
        }
        guard let match, !crowded(previous, at: match) else { return nil }
        for candidate in current.indices where candidate != index {
            if near(current[candidate].bounds, previous[match].bounds) { return nil }
        }
        return match
    }

    private static func crowded(_ faces: [FaceSearchFace], at index: Int) -> Bool {
        faces.indices.contains { $0 != index && overlap(faces[index].bounds, faces[$0].bounds) > 0.25 }
    }

    private static func near(_ a: FaceBounds, _ b: FaceBounds) -> Bool {
        guard a.width > 0, a.height > 0, b.width > 0, b.height > 0 else { return false }
        let widthRatio = a.width / b.width
        let heightRatio = a.height / b.height
        let dx = (a.x + a.width / 2 - b.x - b.width / 2) / min(a.width, b.width)
        let dy = (a.y + a.height / 2 - b.y - b.height / 2) / min(a.height, b.height)
        return (0.7...1.4).contains(widthRatio)
            && (0.7...1.4).contains(heightRatio)
            && dx * dx + dy * dy <= 0.09 && overlap(a, b) >= 0.5
    }

    private static func overlap(_ a: FaceBounds, _ b: FaceBounds) -> Float {
        let width = max(0, min(a.x + a.width, b.x + b.width) - max(a.x, b.x))
        let height = max(0, min(a.y + a.height, b.y + b.height) - max(a.y, b.y))
        let intersection = width * height
        let union = a.width * a.height + b.width * b.height - intersection
        return union > 0 ? intersection / union : 0
    }
}

/// Coalesce on-screen overlay expiry checks. Extending visibility keeps the earlier timer;
/// earlier expiry or clearing replaces it immediately.
/// 合并搜索框的屏显过期检查；延长显示时复用较早定时器，提前过期或清空时立即重排。
struct FaceSearchOverlayExpirySchedule {
    private(set) var deadline: TimeInterval?

    mutating func update(to value: TimeInterval?) -> Bool {
        guard deadline != value else { return false }
        if let deadline, let value, value >= deadline { return false }
        deadline = value
        return true
    }

    mutating func reset() { deadline = nil }
}

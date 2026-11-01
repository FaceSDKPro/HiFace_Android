package com.hiface.sdk.demo.search;

import com.hiface.sdk.search.FaceSearchFace;
import com.hiface.sdk.search.FaceSearchFrame;
import com.hiface.sdk.search.FaceSearchResult;

import java.util.List;
import java.util.Arrays;
import java.util.Locale;

/**
 * Latest detection boxes and a bounded score-display cache; brief associations are for drawing only,
 * not SDK decisions or identity tracking.
 *
 * 最新检测框与有界评分显示缓存；短时关联仅用于绘制，不改变 SDK 结果或代表身份跟踪。
 */
final class FaceSearchOverlayState {
    static final long STALE_FRAME_MILLIS = 1200;
    static final long SCORE_HOLD_MILLIS = 1000;
    static final long MAX_ASSOCIATION_GAP_MILLIS = 300;
    static final int COLOR_PENDING = 0xFFA4B4C9;
    static final int COLOR_UNMATCHED = 0xFFFF3C3C;
    static final int COLOR_MATCHED = 0xFF28DC64;
    private FaceSearchFrame displayed;
    // Search selects at most five faces; double buffering avoids allocating a score array for every
    // detection.
    // 搜索最多选中五张脸，双缓冲避免每次检测都新建评分数组。
    private Score[] scores = new Score[5], scratch = new Score[5];
    private int scoredThisFrame;
    private long newestFrameId = -1, rejectedThrough = -1;
    private long frameDeadline, lastFrameAt;
    private boolean associationEnabled = true;
    private long completedThrough = -1;
    private final String similarityLabel;
    private final String livenessLabel;
    private final String livenessRejectedLabel;
    private final String unmatchedLabel;

    FaceSearchOverlayState(String similarityLabel, String livenessLabel,
            String livenessRejectedLabel, String unmatchedLabel) {
        this.similarityLabel = similarityLabel;
        this.livenessLabel = livenessLabel;
        this.livenessRejectedLabel = livenessRejectedLabel;
        this.unmatchedLabel = unmatchedLabel;
    }

    private final class Score {
        final FaceSearchFace face;
        final long frameId, deadline;
        final String label;
        final String details;
        final boolean livenessRejected;
        final boolean finalResult;
        final Float livenessScore;

        Score(FaceSearchFace face, long frameId, long deadline, Float livenessScore,
                boolean livenessRejected, boolean finalResult) {
            this.face = face;
            this.frameId = frameId;
            this.deadline = deadline;
            this.livenessRejected = livenessRejected;
            this.finalResult = finalResult;
            this.livenessScore = livenessScore;
            boolean matched = face.isThresholdPassed() && face.getFaceId() != null;
            // Only a database score can establish an identity mismatch; low scores remain in SDK results.
            // 只有查库给出分数，才能断言身份未匹配；低分仍保留在 SDK 结果中。
            String identity = matched ? face.getFaceId()
                    : face.getSimilarity() != null ? unmatchedLabel : "";
            label = livenessRejected ? (identity.isEmpty() ? "" : identity + " · ")
                    + livenessRejectedLabel : identity;
            String similarity = matched && face.getSimilarity() != null
                    ? similarityLabel + " " + formatScore(face.getSimilarity()) : "";
            String liveness = livenessScore != null
                    ? livenessLabel + " " + formatScore(livenessScore) : "";
            details = similarity.isEmpty() ? liveness : liveness.isEmpty()
                    ? similarity : similarity + " · " + liveness;
        }
    }

    private static String formatScore(float score) {
        return String.format(Locale.ROOT, "%.3f", score);
    }

    FaceSearchFrame frame() { return displayed; }
    boolean hasVisibleFaces() { return displayed != null && !displayed.getFaces().isEmpty(); }
    long newestFrameId() { return newestFrameId; }
    // Borrow only this sample's score and ID for drawing, never old coordinates or business authorization.
    // 只借用该采样的评分和 ID 绘制，不能借用旧框坐标，也不能作为业务放行依据。
    FaceSearchFace score(int index) { return scores[index] == null ? null : scores[index].face; }
    String label(int index) { return scores[index] == null ? "" : scores[index].label; }
    String details(int index) { return scores[index] == null ? "" : scores[index].details; }
    // Read full, confirmed identities from the same expiring samples as the boxes.
    // 完整身份与人脸框共用同一份到期采样，不能把旧身份留在结果卡片中。
    String resultText() {
        StringBuilder text = new StringBuilder();
        if (displayed == null) return "";
        for (int i = 0; i < displayed.getFaces().size(); i++) {
            Score value = scores[i];
            if (value == null || !value.finalResult) continue;
            if (text.length() > 0) text.append("\n\n");
            if (value.livenessRejected) text.append(livenessRejectedLabel);
            else if (value.face.isThresholdPassed() && value.face.getFaceId() != null)
                text.append("Face ID: ").append(value.face.getFaceId());
            else text.append(unmatchedLabel);
            if (!value.details.isEmpty()) text.append('\n').append(value.details);
        }
        return text.toString();
    }

    Float livenessScore() {
        for (Score value : scores)
            if (value != null && value.finalResult && value.livenessScore != null)
                return value.livenessScore;
        return null;
    }
    /**
     * Use gray without a score and red for liveness rejection; show green for a qualifying similarity only
     * after terminal confirmation.
     *
     * 无评分为灰框；活体拒绝优先红框，相似度达标的采样需终态确认后才显示绿框。
     */
    int color(int index) {
        Score score = scores[index];
        return score == null ? COLOR_PENDING : score.livenessRejected
                ? COLOR_UNMATCHED : score.face.isThresholdPassed() && !score.finalResult
                ? COLOR_PENDING : colorOf(score.face);
    }
    /**
     * Return the earliest frame/score display deadline; schedule no refresh without visible faces.
     *
     * 返回当前画面和评分中最早的显示截止时间，无可见人脸时不安排刷新。
     */
    long expiresAt() {
        if (displayed == null || displayed.getFaces().isEmpty()) return 0;
        long deadline = frameDeadline;
        for (Score score : scores) if (score != null) deadline = Math.min(deadline, score.deadline);
        return deadline;
    }

    /**
     * Accept current detections or scored samples; briefly retain scores only across continuous,
     * unambiguous frames, never old coordinates.
     *
     * 接收当前检测或评分采样；仅在画面连续且框唯一对应时短暂沿用评分，不沿用旧坐标。
     */
    boolean accept(FaceSearchFrame value, long now) {
        if (value.getFrameId() < newestFrameId) return false;
        boolean wasVisible = hasVisibleFaces();
        expire(now);
        boolean sameFrame = value.getFrameId() == newestFrameId;
        // A late score for the same frame must not resurrect an already expired image.
        // 同帧评分晚到也不能让已经超时消失的画面重新出现。
        if (sameFrame && displayed == null) return wasVisible;
        boolean continuous = displayed != null && value.getImageWidth() == displayed.getImageWidth()
                && value.getImageHeight() == displayed.getImageHeight()
                && now - lastFrameAt <= MAX_ASSOCIATION_GAP_MILLIS;
        List<FaceSearchFace> current = value.getFaces();
        Score[] next = scratch;
        if (!sameFrame) scoredThisFrame = 0;
        for (int i = 0; i < current.size(); i++) {
            // Detection and scoring callbacks for the same SDK frame have identical face order and coordinates, so
            // indices correspond.
            // SDK 同一帧的检测和评分回调具有相同人脸顺序与坐标，可以按索引对应。
            int previous = sameFrame ? i : continuous && associationEnabled
                    ? associate(current, i, displayed.getFaces()) : -1;
            Score old = previous < 0 ? null : scores[previous];
            FaceSearchFace face = current.get(i);
            if (face.getSimilarity() != null && value.getFrameId() > rejectedThrough) {
                // Duplicate scoring callbacks must not extend a score's display lifetime.
                // 重复的评分回调不能延长本次评分的显示期限。
                next[i] = (scoredThisFrame & (1 << i)) != 0 ? old
                        : new Score(face, value.getFrameId(), now + SCORE_HOLD_MILLIS, null, false, false);
                scoredThisFrame |= 1 << i;
            } else next[i] = old;
        }
        if (!sameFrame) { frameDeadline = now + STALE_FRAME_MILLIS; lastFrameAt = now; }
        newestFrameId = value.getFrameId(); displayed = value;
        scratch = scores; scores = next; Arrays.fill(scratch, null);
        // Do not redraw repeated empty frames; clear old boxes once when transitioning from faces to no faces.
        // 连续空帧不刷新画布；有人变无人仍必须清除一次旧框。
        return wasVisible || !current.isEmpty();
    }

    /**
     * Final results supplement only their matching samples; never draw an old identity on another person's
     * box.
     *
     * 最终结果只补充对应采样的显示信息；不能把旧结果画到另一个人的框上。
     */
    void complete(FaceSearchResult result, long now) {
        FaceSearchFrame frame = result.getFrame();
        long frameId = frame.getFrameId();
        expire(now);
        if (displayed == null || frameId > newestFrameId) return;
        if (frameId <= completedThrough) return;
        completedThrough = frameId;
        boolean rejected = result.getEndReason() == FaceSearchResult.EndReason.SPOOF_DETECTED;
        boolean matched = false;
        for (FaceSearchFace face : frame.getFaces()) matched |= face.isThresholdPassed();
        if (rejected) {
            // Keep this batch's identity match for display, clear earlier scores, and prevent green boxes without
            // terminal confirmation.
            // 本批的身份匹配仍用于展示；清掉更早的评分，且禁止重画本批为未经终态确认的绿框。
            rejectScoresThrough(frameId - 1);
            rejectedThrough = Math.max(rejectedThrough, frameId);
        } else if (!matched) {
            // On timeout, preserve this frame's actual low similarity; clear old colors and identities for other
            // failures.
            // 超时保留本帧真实低相似度；其他失败先清掉旧的颜色和身份。
            rejectScoresThrough(result.getEndReason() == FaceSearchResult.EndReason.TIMEOUT
                    ? frameId - 1 : frameId);
        }
        if (displayed == null || frame.getImageWidth() != displayed.getImageWidth()
                || frame.getImageHeight() != displayed.getImageHeight()) return;
        boolean updated = false;
        for (int i = 0; i < scores.length; i++) {
            Score score = scores[i];
            if (score != null && score.frameId == frameId) {
                // Complete a score already associated with newer coordinates without extending its original display
                // deadline.
                // 可补全已经沿用到新坐标的同一份评分，但不能延长原来的显示期限。
                scores[i] = new Score(score.face, frameId, score.deadline,
                        result.getLivenessScore(), rejected, true);
                updated = true;
            }
        }
        // Display this frame's liveness rejection even if no lookup score arrives before the deadline; never
        // apply it to a newer face.
        // 截止前没拿到查库分数时，也能显示当前帧的活体拒绝；不能为较新的人脸补画旧结果。
        if (rejected && !updated && frameId == newestFrameId && (scoredThisFrame & 1) == 0
                && frame.getFaces().size() == 1 && displayed.getFaces().size() == 1) {
            scores[0] = new Score(frame.getFaces().get(0), frameId,
                    now + SCORE_HOLD_MILLIS, result.getLivenessScore(), true, true);
            scoredThisFrame |= 1;
        }
        // Keep this exact frame's negative outcome visible after old positive scores are invalidated.
        // 旧匹配评分失效后，仍展示当前这一帧的未匹配结果；不能借给新的人脸。
        if (result.getEndReason() == FaceSearchResult.EndReason.TIMEOUT && frameId == newestFrameId
                && frame.getFaces().size() == displayed.getFaces().size()) {
            for (int i = 0; i < frame.getFaces().size(); i++) {
                FaceSearchFace face = frame.getFaces().get(i);
                if (scores[i] == null && !face.isThresholdPassed())
                    scores[i] = new Score(face, frameId, now + SCORE_HOLD_MILLIS,
                            result.getLivenessScore(), false, true);
            }
        }
    }

    /**
     * Single-face mode hides unselected boxes when multiple faces appear; disable association to avoid
     * confusing a largest-face switch with continuity.
     *
     * 单人模式检出多人时看不到未选中的框，关闭关联以免把最大脸切换误认为同一个人。
     */
    boolean setAssociationEnabled(boolean enabled) {
        if (associationEnabled == enabled) return false;
        associationEnabled = enabled;
        for (int i = 0; i < scores.length; i++)
            if (scores[i] != null && scores[i].frameId < newestFrameId) scores[i] = null;
        lastFrameAt = -MAX_ASSOCIATION_GAP_MILLIS - 1;
        return true;
    }

    /**
     * Failed rounds or an empty database invalidate old scores without clearing results newer than the
     * specified frame.
     *
     * 失败轮次或空库使旧评分失效，但不能清除比指定帧更新的结果。
     */
    void rejectScoresThrough(long frameId) {
        rejectedThrough = Math.max(rejectedThrough, frameId);
        for (int i = 0; i < scores.length; i++)
            if (scores[i] != null && scores[i].frameId <= rejectedThrough) scores[i] = null;
    }

    /**
     * Clear the whole frame when imagery stalls; clear only the individual score when that score expires.
     *
     * 画面停滞时清空整帧，单个评分到期时仅清除该评分。
     */
    boolean expire(long now) {
        if (displayed != null && now >= frameDeadline) {
            boolean changed = hasVisibleFaces();
            displayed = null; Arrays.fill(scores, null); frameDeadline = 0;
            return changed;
        } else {
            boolean changed = false;
            for (int i = 0; i < scores.length; i++) {
                if (scores[i] != null && now >= scores[i].deadline) {
                    scores[i] = null;
                    changed = true;
                }
            }
            return changed;
        }
    }

    /**
     * Reset frame IDs, rejection boundaries, and both score buffers for a new screen or coordinate space.
     *
     * 清空帧号、拒绝边界和双缓冲评分，为新页面或新坐标空间重新开始显示。
     */
    void clear() {
        displayed = null; Arrays.fill(scores, null); Arrays.fill(scratch, null); newestFrameId = rejectedThrough = -1;
        scoredThisFrame = 0; frameDeadline = lastFrameAt = 0;
        completedThrough = -1;
    }

    /**
     * Associate boxes only when the correspondence is unique in both directions, preventing identity
     * mixing by array index or nearest distance.
     *
     * 只有当前框与上一帧框互相唯一对应时才关联，避免按数组位置或最近距离串用身份。
     */
    private static int associate(List<FaceSearchFace> current, int index, List<FaceSearchFace> previous) {
        FaceSearchFace.Bounds box = current.get(index).getBounds();
        if (crowded(current, index)) return -1;
        int match = -1;
        for (int j = 0; j < previous.size(); j++) {
            if (!near(box, previous.get(j).getBounds())) continue;
            if (match != -1) return -1;
            match = j;
        }
        if (match < 0 || crowded(previous, match)) return -1;
        for (int i = 0; i < current.size(); i++)
            if (i != index && near(current.get(i).getBounds(), previous.get(match).getBounds())) return -1;
        return match;
    }

    /**
     * Detect strongly overlapping neighboring faces and abandon cross-frame score association when
     * ambiguous.
     *
     * 检测重叠较大的邻近人脸，有歧义时放弃跨帧评分关联。
     */
    private static boolean crowded(List<FaceSearchFace> faces, int index) {
        for (int i = 0; i < faces.size(); i++)
            if (i != index && overlap(faces.get(index).getBounds(), faces.get(i).getBounds()) > .25f) return true;
        return false;
    }

    /**
     * Use box size, center displacement, and overlap to decide brief associations for display only.
     *
     * 综合框大小、中心位移和重叠率判断能否短时关联，只服务于界面显示。
     */
    private static boolean near(FaceSearchFace.Bounds a, FaceSearchFace.Bounds b) {
        float wr = a.getWidth() / b.getWidth(), hr = a.getHeight() / b.getHeight();
        float dx = (a.getX() + a.getWidth() / 2 - b.getX() - b.getWidth() / 2) / Math.min(a.getWidth(), b.getWidth());
        float dy = (a.getY() + a.getHeight() / 2 - b.getY() - b.getHeight() / 2) / Math.min(a.getHeight(), b.getHeight());
        return wr >= .7f && wr <= 1.4f && hr >= .7f && hr <= 1.4f
                && dx * dx + dy * dy <= .09f && overlap(a, b) >= .5f;
    }

    /**
     * Compute intersection over union for two boxes to support display association.
     *
     * 计算两个检测框的交并比，供显示关联判断使用。
     */
    private static float overlap(FaceSearchFace.Bounds a, FaceSearchFace.Bounds b) {
        float w = Math.max(0, Math.min(a.getX() + a.getWidth(), b.getX() + b.getWidth()) - Math.max(a.getX(), b.getX()));
        float h = Math.max(0, Math.min(a.getY() + a.getHeight(), b.getY() + b.getHeight()) - Math.max(a.getY(), b.getY()));
        float intersection = w * h;
        return intersection / (a.getWidth() * a.getHeight() + b.getWidth() * b.getHeight() - intersection);
    }

    /**
     * Choose a base box color from score presence and similarity threshold; color handles liveness
     * rejection and terminal confirmation separately.
     *
     * 按是否有分数及相似度门槛返回基础框色，活体拒绝与终态确认由 color 另行处理。
     */
    static int colorOf(FaceSearchFace face) {
        if (face == null || face.getSimilarity() == null) return COLOR_PENDING;
        return face.isThresholdPassed() ? COLOR_MATCHED : COLOR_UNMATCHED;
    }
}

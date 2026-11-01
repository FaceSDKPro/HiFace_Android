package com.hiface.sdk.demo.search;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.View;

import com.hiface.sdk.demo.R;
import com.hiface.sdk.demo.ui.OutlinedOverlayText;
import com.hiface.sdk.search.FaceSearchFace;
import com.hiface.sdk.search.FaceSearchFrame;
import com.hiface.sdk.search.FaceSearchResult;

/**
 * Draw sampled results with the latest detection boxes; OverlayState manages brief display
 * association, not identity tracking.
 *
 * 使用最新检测框绘制采样结果；短时显示关联由 OverlayState 管理，不代表身份跟踪。
 */
public final class FaceSearchOverlayView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint labelFillPaint;
    private final Paint labelOutlinePaint;
    private final FaceSearchOverlayState state;
    private final RectF rect = new RectF();
    private final RectF viewport = new RectF();
    private final float density;
    private Matrix transform;
    private final FaceSearchOverlayExpirySchedule expirySchedule = new FaceSearchOverlayExpirySchedule();
    private final Runnable expire = this::expireState;
    private Runnable onResultChanged;

    public void setOnResultChanged(Runnable listener) { onResultChanged = listener; }
    public String resultText() { return state.resultText(); }
    public Float livenessScore() { return state.livenessScore(); }

    /**
     * Expire scores or stalled imagery at the next deadline, refreshing even when no new frame arrives.
     *
     * 在最近的显示期限到达时清理评分或停留画面，即使没有新帧也会刷新界面。
     */
    private void expireState() {
        expirySchedule.reset();
        if (state.expire(SystemClock.uptimeMillis())) invalidate();
        scheduleExpiry();
    }

    public FaceSearchOverlayView(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = getResources().getDisplayMetrics().density;
        labelFillPaint = OutlinedOverlayText.createFillPaint(14 * density, false);
        labelOutlinePaint = OutlinedOverlayText.createOutlinePaint(labelFillPaint, 3 * density);
        state = new FaceSearchOverlayState(context.getString(R.string.search_ui_overlay_similarity),
                context.getString(R.string.search_ui_overlay_liveness), context.getString(R.string.search_spoof),
                context.getString(R.string.search_ui_overlay_unmatched));
    }

    /**
     * Accept a complete sample and preview transform; ignore old frames and clear associations when the
     * transform is absent or changes.
     *
     * 接收完整采样与预览变换；忽略旧帧，变换缺失或变化时清除旧关联。
     */
    public void show(FaceSearchFrame frame, Matrix matrix) {
        if (frame.getFrameId() < state.newestFrameId()) {
            return;
        }
        if (frame.getFaces().isEmpty()) {
            boolean redraw = state.accept(frame, SystemClock.uptimeMillis());
            transform = null;
            scheduleExpiry();
            if (redraw) postInvalidateOnAnimation();
            return;
        }
        if (matrix == null) {
            clear();
            return;
        }
        if (transform != null && !transform.equals(matrix)) {
            state.clear();
        }
        state.accept(frame, SystemClock.uptimeMillis());
        // Always use coordinates from the latest frame; retain scores and IDs only through brief, unambiguous
        // display association.
        // 坐标始终来自最新帧，分数和 ID 只允许沿用短时且无歧义的显示关联。
        if (transform == null) {
            transform = new Matrix(matrix);
        } else {
            transform.set(matrix);
        }
        postInvalidateOnAnimation();
        scheduleExpiry();
    }

    /**
     * Cancel expiry refreshes and clear imagery, scores, and transforms when pausing, switching cameras,
     * or destroying the screen.
     *
     * 取消过期刷新并清除画面、分数及坐标变换，供暂停、切镜头和销毁时调用。
     */
    public void clear() {
        removeCallbacks(expire);
        expirySchedule.reset();
        state.clear();
        transform = null;
        invalidate();
        if (onResultChanged != null) onResultChanged.run();
    }

    /**
     * Use the final result to complete the matching sample's identity label, liveness score, and box
     * color.
     *
     * 用最终结果补全对应采样的身份标签、活体分数和框色。
     */
    public void showResult(FaceSearchResult result) {
        boolean wasVisible = state.hasVisibleFaces();
        state.complete(result, SystemClock.uptimeMillis());
        // complete may expire an old frame first; refresh once to clear drawn content before cancelling the
        // expiry task.
        // complete 可能先使旧帧到期；撤销到期任务前仍需刷新一次以清掉已绘制的内容。
        if (wasVisible || state.hasVisibleFaces()) postInvalidateOnAnimation();
        scheduleExpiry();
    }

    /**
     * Control brief cross-frame score display; disabling it stops guessing whether the largest face is the
     * same person.
     *
     * 控制短时评分的跨帧显示关联，关闭时不再猜测最大脸是否仍是同一个人。
     */
    public void setAssociationEnabled(boolean enabled) {
        if (!state.setAssociationEnabled(enabled)) return;
        if (state.hasVisibleFaces()) postInvalidateOnAnimation();
        scheduleExpiry();
    }

    /**
     * Clear scores through the specified frame while preserving display results from newer samples.
     *
     * 清除指定帧及更早的评分，保留较新采样已产生的显示结果。
     */
    public void rejectScoresThrough(long frameId) {
        state.rejectScoresThrough(frameId);
        if (state.hasVisibleFaces()) postInvalidateOnAnimation();
        scheduleExpiry();
    }

    /**
     * Clear all scores through the current frame, for example when the current filter scope is empty.
     *
     * 清除截至当前帧的全部旧评分，供当前筛选范围为空等情况使用。
     */
    public void rejectScores() {
        rejectScoresThrough(state.newestFrameId());
    }

    /**
     * Schedule only the nearest expiry refresh instead of enqueuing another delayed task for every frame.
     *
     * 只安排最近一次到期刷新，避免每来一帧就重复排队延时任务。
     */
    private void scheduleExpiry() {
        if (onResultChanged != null) onResultChanged.run();
        long deadline = state.expiresAt();
        if (!expirySchedule.update(deadline)) return;
        removeCallbacks(expire);
        if (deadline != 0) {
            postDelayed(expire, Math.max(1, deadline - SystemClock.uptimeMillis()));
        }
    }

    /**
     * Remove scheduled tasks and release retained display results when the View detaches.
     *
     * View 离开窗口时移除定时任务并释放持有的显示结果。
     */
    @Override
    protected void onDetachedFromWindow() {
        clear();
        super.onDetachedFromWindow();
    }

    /**
     * A container-size change invalidates the old coordinate transform; wait for a new frame to establish
     * mapping.
     *
     * 容器尺寸变化后旧坐标变换不再可靠，等待新帧重新建立映射。
     */
    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        clear();
    }

    /**
     * Draw boxes from the latest frame and labels from display state; extra width affects drawing only.
     *
     * 按最新帧坐标绘框，身份和评分由显示状态提供；额外扩宽只影响绘制。
     */
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        FaceSearchFrame frame = state.frame();
        if (frame == null || transform == null) {
            return;
        }
        viewport.set(0, 0, getWidth(), getHeight());
        for (int i = 0; i < frame.getFaces().size(); i++) {
            FaceSearchFace.Bounds bounds = frame.getFaces().get(i).getBounds();
            rect.set(bounds.getX(), bounds.getY(),
                    bounds.getX() + bounds.getWidth(), bounds.getY() + bounds.getHeight());
            transform.mapRect(rect);
            // Widen display boxes by 10% on each side; SDK boxes and score associations still use original
            // coordinates.
            // 只改变显示宽度：左右各加 10%，SDK 检测框和评分关联仍使用原始坐标。
            rect.inset(-rect.width() * .10f, 0f);
            if (!RectF.intersects(rect, viewport)) {
                continue;
            }
            paint.setColor(state.color(i));
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2 * density);
            canvas.drawRect(rect, paint);
            drawLabel(canvas, state.label(i), state.details(i), density);
        }
    }

    /**
     * Draw identity and scores on separate lines above the box, constrained to the visible area.
     *
     * 把身份与评分分行绘制在框上方，并限制到可见区域内。
     */
    private void drawLabel(Canvas canvas, String label, String details, float density) {
        if (label.isEmpty()) {
            return;
        }
        // Truncate ID and score lines independently so long business IDs do not crowd out similarity or
        // liveness scores.
        // ID 和评分分行截断，长业务 ID 不会挤掉相似度/活体分数。
        label = fitLabel(label, density);
        details = fitLabel(details, density);
        float width = Math.max(
                labelFillPaint.measureText(label),
                labelFillPaint.measureText(details));
        float x = Math.max(6 * density,
                Math.min(rect.left, getWidth() - width - 6 * density));
        float lineHeight = details.isEmpty() ? 0
                : labelFillPaint.descent() - labelFillPaint.ascent() + 2 * density;
        float y = Math.max(-labelFillPaint.ascent() + 4 * density + lineHeight,
                Math.min(rect.top - 6 * density, getHeight() - labelFillPaint.descent()));
        OutlinedOverlayText.draw(
                canvas, label, x, y - lineHeight, labelFillPaint, labelOutlinePaint);
        OutlinedOverlayText.draw(
                canvas, details, x, y, labelFillPaint, labelOutlinePaint);
    }

    /**
     * Truncate long labels using actual drawn width, reserving one line each for identity and scores.
     *
     * 按实际绘制宽度截断长标签，给身份和评分各自保留一行。
     */
    private String fitLabel(String label, float density) {
        int length = labelFillPaint.breakText(
                label, true, Math.max(0, getWidth() - 12 * density), null);
        return length < label.length() ? label.substring(0, Math.max(0, length - 1)) + "…" : label;
    }
}

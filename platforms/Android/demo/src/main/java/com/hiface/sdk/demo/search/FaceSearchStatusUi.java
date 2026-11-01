package com.hiface.sdk.demo.search;

import android.content.Context;
import android.content.res.ColorStateList;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.ColorRes;
import androidx.annotation.StringRes;

import com.hiface.sdk.common.HiFaceError;
import com.hiface.sdk.demo.R;
import com.hiface.sdk.search.FaceSearchSessionConfig;
import com.hiface.sdk.search.FaceSearchMode;
import com.hiface.sdk.search.FaceSearchProgress;

/**
 * Search-specific text and controls; the Activity still demonstrates SDK calls and recognition-result
 * handling.
 *
 * 搜索专用的状态文案与控件展示；SDK 调用和识别结果处理仍由 Activity 演示。
 */
final class FaceSearchStatusUi {
    private final Context context;
    private final TextView guidance;
    private final TextView people;
    private final TextView options;
    private final View retry;
    private final View stateDot;
    private String modeText = "", stateTitle = "", hintText = "", fullResult = "";
    private int stateTitleId;
    private String matchCaption = "";
    private boolean errorVisible;

    FaceSearchStatusUi(View root) {
        context = root.getContext();
        guidance = root.findViewById(R.id.searchGuidance);
        people = root.findViewById(R.id.searchPeopleHint);
        options = root.findViewById(R.id.searchOptions);
        retry = root.findViewById(R.id.searchRetry);
        stateDot = root.findViewById(R.id.searchStateDot);
        guidance.setOnClickListener(view -> showDetails());
    }

    void renderResult(String text, String caption) {
        fullResult = text;
        matchCaption = caption;
        renderCaption();
    }

    // Keep exactly two fixed rows; a dialog retains every ID character and all diagnostic text.
    // 固定两行不增高，完整 ID 和诊断提示在点击后查看/复制。
    private void renderCaption() {
        String text = !errorVisible && !matchCaption.isEmpty() ? matchCaption : !errorVisible && !fullResult.isEmpty()
                ? fullResult.substring(0, fullResult.indexOf('\n') < 0 ? fullResult.length() : fullResult.indexOf('\n'))
                : errorVisible ? hintText : stateTitleId == R.string.search_ui_empty_title
                    ? context.getString(R.string.search_ui_empty_caption) : stateTitle;
        text = text.replace('\r', ' ').replace('\n', ' ');
        if (!modeText.contentEquals(options.getText())) options.setText(modeText);
        if (!text.contentEquals(guidance.getText())) guidance.setText(text);
        guidance.setContentDescription(text);
    }

    private void showDetails() {
        String text = modeText + "\n" + stateTitle + "\n" + hintText
                + (fullResult.isEmpty() ? "" : "\n\n" + fullResult);
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(context)
                .setMessage(text).setPositiveButton(android.R.string.ok, null).show();
        TextView message = dialog.findViewById(android.R.id.message);
        if (message != null) message.setTextIsSelectable(true);
    }

    /**
     * Show preparation guidance on start or resume and hide the previous error's retry button.
     *
     * 显示启动或恢复时的准备提示，并隐藏上一次错误的重试按钮。
     */
    void showPreparing() {
        showState(R.string.search_ui_preparing, R.string.search_ui_preparing_hint,
                R.color.search_accent, false);
    }

    /**
     * Offer retry only for errors recoverable by the same session; otherwise ask users to fix the cause
     * and reenter.
     *
     * 仅为原会话能恢复的错误提供重试；其余提示修复原因后重新进入。
     */
    void showError(HiFaceError error) {
        boolean canRetry = SearchUi.canResumeSearch(error);
        // Show generic guidance for unavailable SDK state; do not offer ineffective retries.
        // SDK 不可用时显示统一提示，不提供无效重试。
        int license = com.hiface.sdk.demo.ui.DemoErrorUi.licenseMessage(error.getCode());
        showState(R.string.search_ui_paused,
                license != 0 ? license : (canRetry ? SearchUi.errorText(error) : R.string.search_ui_error_reenter),
                R.color.search_error_color, canRetry);
    }

    /**
     * Display the current search mode and configured liveness switch.
     *
     * 展示当前搜索模式与活体开关，活体开关属于配置信息。
     */
    void renderConfig(FaceSearchSessionConfig config) {
        if (config.getMode() == FaceSearchMode.MULTI_FACE) {
            modeText = context.getString(R.string.search_ui_multi_options);
        } else {
            modeText = context.getString(R.string.search_ui_options,
                    context.getString(R.string.search_ui_single_short),
                    context.getString(config.needsLiveness()
                            ? R.string.search_ui_live_on : R.string.search_ui_live_off));
        }
        renderCaption();
    }

    /**
     * Map SDK stages to instructions and display detected/selected face counts for single- or multi-face
     * mode.
     *
     * 把 SDK 阶段映射为操作引导，并按单人/多人模式展示检测与选中人数。
     */
    void renderProgress(FaceSearchProgress progress, FaceSearchSessionConfig config) {
        // Map explicit enum meanings rather than relying on ordinal order matching two resource arrays.
        // 按枚举含义映射，不依赖 ordinal 与两份资源数组保持相同顺序。
        switch (progress.getStage()) {
            case WAITING_FOR_FACE:
                showProgress(R.string.search_waiting, R.string.search_ui_waiting_hint);
                break;
            case FACE_TOO_SMALL:
                showProgress(R.string.search_ui_move_closer, R.string.search_ui_size_hint);
                break;
            case EMPTY_GALLERY:
                showProgress(R.string.search_ui_empty_title, R.string.search_ui_empty_hint);
                break;
            case SUCCESS_INTERVAL:
                showProgress(R.string.search_ui_interval_title, R.string.search_ui_interval_hint);
                break;
            case SEARCHING:
                showProgress(R.string.search_searching, R.string.search_ui_searching_hint);
                break;
        }
        if (progress.getDetectedCount() > 1) {
            people.setText(config.getMode() == FaceSearchMode.SINGLE_FACE
                    ? context.getString(R.string.search_ui_single_people, progress.getDetectedCount())
                    : context.getString(R.string.search_ui_multi_people,
                            progress.getDetectedCount(), progress.getSelectedCount()));
            people.setVisibility(View.VISIBLE);
        } else {
            clearPeopleHint();
        }
    }

    /**
     * Clear stale multi-face hints for single-face frames, pauses, and UI resets.
     *
     * 清除旧帧的多人提示，供单人帧、暂停和重置界面时使用。
     */
    void clearPeopleHint() {
        people.setVisibility(View.GONE);
    }

    /**
     * Use the in-progress color for search stages and hide technical-error retry controls.
     *
     * 统一使用进行中色彩显示搜索阶段，同时隐藏技术错误的重试入口。
     */
    private void showProgress(@StringRes int title, @StringRes int hint) {
        showState(title, hint, R.color.search_accent, false);
    }

    /**
     * Update title, instructions, status dot, and retry button together to avoid reusing styles from the
     * previous state.
     *
     * 一次更新标题、引导、状态点和重试按钮，避免沿用上一状态的控件样式。
     */
    private void showState(@StringRes int title, @StringRes int hint,
            @ColorRes int color, boolean canRetry) {
        stateTitle = context.getString(title);
        stateTitleId = title;
        hintText = context.getString(hint);
        errorVisible = color == R.color.search_error_color;
        renderCaption();
        retry.setVisibility(canRetry ? View.VISIBLE : View.GONE);
        stateDot.setBackgroundTintList(ColorStateList.valueOf(context.getColor(color)));
    }
}

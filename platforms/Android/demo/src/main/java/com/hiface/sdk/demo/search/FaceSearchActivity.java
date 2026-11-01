package com.hiface.sdk.demo.search;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.camera.view.PreviewView;
import com.hiface.sdk.HiFaceSdk;
import com.hiface.sdk.common.HiFaceError;
import com.hiface.sdk.demo.R;
import com.hiface.sdk.demo.camera.CameraPreviewFragment;
import com.hiface.sdk.search.FaceSearchSessionConfig;
import com.hiface.sdk.search.FaceSearchFrame;
import com.hiface.sdk.search.FaceSearchListener;
import com.hiface.sdk.search.FaceSearchMode;
import com.hiface.sdk.search.FaceSearchProgress;
import com.hiface.sdk.search.FaceSearchResult;
import com.hiface.sdk.search.FaceSearchSession;

/**
 * Search integration example: start a session, send camera frames, then pause and release it with the
 * screen lifecycle.
 *
 * 搜索接入示例：启动搜索会话、送入相机帧，再按页面生命周期暂停和释放。
 */
public final class FaceSearchActivity extends AppCompatActivity {
    // Keep SDK, camera, and search resources scoped to this screen.
    // SDK、相机和搜索资源均由当前页面管理。
    private HiFaceSdk hiFaceSdk;
    private CameraPreviewFragment camera;
    private FaceSearchOverlayView overlay;
    private FaceSearchStatusUi statusUi;
    private FaceSearchSession faceSearchSession;
    private FaceSearchSessionConfig config = SearchSettingsDialog.demoDefaultConfig();
    private AlertDialog settingsDialog;
    private HiFaceError lastSearchError;
    // Display policy is active only while visible; face presence controls the idle countdown.
    // 仅在页面可见时应用显示策略；最新人脸状态控制待机倒计时。
    private boolean searchDisplayApplied;
    private boolean searchFacePresent;
    private float previousWindowBrightness;
    private float appliedSearchBrightness = Float.NaN;
    private final Handler displayHandler = new Handler(Looper.getMainLooper());
    // Enter idle only if no face is still present at the deadline, preventing late tasks from overriding
    // active state.
    // 到期后仍无人脸才进入待机，避免迟到任务覆盖活跃状态。
    private final Runnable enterSearchCameraIdle = () -> {
        if (searchDisplayApplied && !searchFacePresent) {
            enterSearchIdleMode();
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        restoreConfig(state);
        setContentView(R.layout.activity_face_search);
        bindViews();
        bindActions();
        startSearch();
    }

    private void bindViews() {
        camera = CameraPreviewFragment.require(getSupportFragmentManager(), R.id.searchCamera);
        // Start at 15 FPS; idle preview uses 10 FPS while SDK analysis remains at 5 FPS.
        // 初始 15 FPS，无人脸待机时降至 5 FPS。
        camera.configureSessionFrameRate(15);
        camera.configureOverlayPreview(PreviewView.ScaleType.FILL_CENTER);
        overlay = findViewById(R.id.searchOverlay);
        statusUi = new FaceSearchStatusUi(findViewById(android.R.id.content));
        overlay.setOnResultChanged(() -> statusUi.renderResult(overlay.resultText(), overlay.livenessScore()));
        statusUi.renderConfig(config);
    }

    private void bindActions() {
        findViewById(R.id.searchBack).setOnClickListener(v -> finish());
        findViewById(R.id.searchEnroll).setOnClickListener(v ->
                startActivity(new Intent(this, FaceSearchEnrollmentActivity.class)));
        findViewById(R.id.searchManage).setOnClickListener(v ->
                startActivity(new Intent(this, FaceSearchLibraryActivity.class)));
        findViewById(R.id.searchSettings).setOnClickListener(v -> showSettings());
        findViewById(R.id.searchRetry).setOnClickListener(v -> resumeSearch());
        findViewById(R.id.searchSwitch).setOnClickListener(v -> {
            if (!camera.switchCamera()) {
                Toast.makeText(this, R.string.camera_switch_unavailable, Toast.LENGTH_SHORT).show();
            }
        });
    }

    /**
     * Start search and send frames through one path; the SDK opens the database, prepares models, and
     * drops frames until ready.
     *
     * 直接启动搜索并统一送帧；SDK 自动开库和准备模型，未就绪时丢帧。
     */
    private void startSearch() {
        // 当前页面持有搜索 SDK；普通暂停可复用，页面销毁时自动关闭。
        hiFaceSdk = HiFaceSdk.builder(this).lifecycleOwner(this).build();

        // Continuously submit analysis frames; the SDK drops them during preparation or backpressure.
        // 持续送入相机分析帧，准备和背压期间由 SDK 丢帧。
        camera.setFrameListener(frame -> hiFaceSdk.processFrame(frame));

        faceSearchSession = hiFaceSdk.search().startCameraSearch(config, new FaceSearchListener() {
            /**
             * Main-thread callback; an unscored detection frame does not mean matching failed.
             *
             * 主线程回调；检测帧没有评分不代表匹配失败。
             */
            @Override
            public void onFrame(@NonNull FaceSearchFrame frame) {
                // Progress is deduplicated; synchronize the selected-face state each frame so stale state cannot block
                // idle timing.
                // 进度会去重，逐帧同步选中人脸状态，避免旧状态阻止待机计时。
                updateSearchDisplayFacePresence(!frame.getFaces().isEmpty());
                // Empty frames need no coordinate transform; the overlay still advances frame IDs to prevent late
                // identities from redrawing old boxes.
                // 空画面没有坐标需要变换；Overlay 仍更新帧号，阻止晚到身份重画旧框。
                overlay.show(frame, frame.getFaces().isEmpty() ? null
                        : camera.analysisToPreview(frame.getImageWidth(), frame.getImageHeight()));
            }

            /**
             * Limit associations with old scores if single-face mode sees multiple faces or the scope is empty.
             *
             * 单人模式检出多人或范围为空时，限制旧评分关联。
             */
            @Override
            public void onProgress(@NonNull FaceSearchProgress progress) {
                updateSearchDisplayFacePresence(progress.getDetectedCount() > 0);
                boolean multi = config.getMode() == FaceSearchMode.MULTI_FACE;
                overlay.setAssociationEnabled(multi || progress.getDetectedCount() <= 1);
                if (progress.getStage() == FaceSearchProgress.Stage.EMPTY_GALLERY) {
                    overlay.rejectScores();
                }
                statusUi.renderProgress(progress, config);
            }

            /**
             * The overlay associates terminal results with their samples; this is not identity tracking.
             *
             * Overlay 将终态关联到对应采样，不代表身份跟踪。
             */
            @Override
            public void onResult(@NonNull FaceSearchResult result) {
                overlay.showResult(result);
            }

            /**
             * The SDK paused search; stop analysis delivery but retain preview, and offer retry only for
             * recoverable errors.
             *
             * SDK 已暂停搜索；停止分析送帧并保留预览，仅为可恢复错误显示重试。
             */
            @Override
            public void onError(@NonNull HiFaceError error) {
                lastSearchError = error;
                camera.pauseAnalysis();
                updateSearchDisplayFacePresence(false);
                clearOverlay();
                statusUi.showError(error);
            }
        });
    }

    /**
     * Apply validated settings; the SDK invalidates old samples and prepares the new configuration while
     * the UI clears old scores.
     *
     * 应用已校验的设置；SDK 使旧采样失效并准备新配置，界面同时清掉旧评分。
     */
    private void showSettings() {
        if (settingsDialog != null && settingsDialog.isShowing()) {
            return;
        }
        settingsDialog = SearchSettingsDialog.show(this, config, next -> {
            config = next;
            faceSearchSession.setConfig(next);
            statusUi.renderConfig(config);
            clearOverlay();
            // setConfig restarts a running session automatically but only stores settings while paused; explicitly
            // resume a valid session.
            // setConfig 在运行中自动重启，暂停时只保存配置；显式恢复有效会话。
            resumeSearch();
        });
    }

    /**
     * Clear boxes, scores, and face-count hints together.
     *
     * 同时清除框、评分和人数提示。
     */
    private void clearOverlay() {
        overlay.clear();
        statusUi.clearPeopleHint();
    }

    /**
     * Stop analysis and pause the session, retaining the SDK and database for reuse on return.
     *
     * 停止分析送帧并暂停会话，保留 SDK 和数据库供返回页面时复用。
     */
    private void pauseSearch() {
        camera.pauseAnalysis();
        faceSearchSession.pause();
        clearOverlay();
    }

    /**
     * Resume the session and analysis; the SDK accepts new frames automatically after preparation.
     *
     * 恢复会话与相机分析；SDK 准备完成后自动接收新帧。
     */
    private void resumeSearch() {
        // A rejected session cannot resume; retain nonrecoverable errors instead of replacing them with a
        // preparing message.
        // 被拒绝的会话不能 resume；不可恢复错误保留提示，避免被覆盖成“准备中”。
        if (lastSearchError != null && !SearchUi.canResumeSearch(lastSearchError)) {
            statusUi.showError(lastSearchError);
            return;
        }
        lastSearchError = null;
        statusUi.showPreparing();
        faceSearchSession.resume();
        camera.resumeAnalysis();
    }

    /**
     * Resume search when visible again, including enrollment or management changes to the same database.
     *
     * 页面重新可见后继续搜索，可读取录入或管理页面对同一人脸库的更新。
     */
    @Override
    protected void onStart() {
        super.onStart();
        applySearchDisplayPolicy();
        resumeSearch();
    }

    /**
     * Stop sampling while hidden and invalidate search callbacks not yet delivered.
     *
     * 页面不可见时停止采样，并让尚未交付的搜索回调失效。
     */
    @Override
    protected void onStop() {
        pauseSearch();
        restoreSearchDisplayPolicy();
        super.onStop();
    }

    /**
     * Close screen dialogs; the lifecycle-bound SDK ends the search session and releases its connection
     * automatically.
     *
     * 关闭页面弹窗；绑定生命周期的 SDK 自动结束搜索会话并释放连接。
     */
    @Override
    protected void onDestroy() {
        // Idempotent cleanup also restores the window policy.
        // 幂等清理，兜底恢复窗口策略。
        restoreSearchDisplayPolicy();
        if (settingsDialog != null) {
            settingsDialog.dismiss();
        }
        super.onDestroy();
    }


    /**
     * Change policy only when a face appears or disappears, avoiding window-property writes on every
     * frame.
     *
     * 只在人脸出现或消失时切换策略，避免对每一帧重复写窗口属性。
     */
    private void updateSearchDisplayFacePresence(boolean hasFace) {
        if (!searchDisplayApplied || searchFacePresent == hasFace) {
            return;
        }
        searchFacePresent = hasFace;
        if (hasFace) {
            displayHandler.removeCallbacks(enterSearchCameraIdle);
            enterSearchActiveMode();
        } else {
            scheduleNoFaceIdle();
        }
    }

    private void scheduleNoFaceIdle() {
        displayHandler.removeCallbacks(enterSearchCameraIdle);
        // Enter idle only after ten continuous seconds without a face.
        // 连续无人脸十秒后才进入待机。
        displayHandler.postDelayed(enterSearchCameraIdle, 10_000L);
    }

    /**
     * Dim the backlight and request a lower frame rate while idle; HAL determines actual capture, and
     * analysis throttling remains active.
     *
     * 待机降低背光并请求低帧率；实际采集取决于 HAL，分析门禁仍保留。
     */
    private void enterSearchIdleMode() {
        setSearchBrightness(.10f);
        camera.configureSessionFrameRates(10, 5);
    }

    /**
     * Restore brightness and frame rate dynamically without recreating camera or search resources.
     *
     * 动态恢复亮度和帧率，不重建相机或搜索资源。
     */
    private void enterSearchActiveMode() {
        setSearchBrightness(.90f);
        camera.configureSessionFrameRate(15);
    }

    private void setSearchBrightness(float brightness) {
        if (Float.compare(appliedSearchBrightness, brightness) == 0) {
            return;
        }
        Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.screenBrightness = brightness;
        window.setAttributes(attributes);
        appliedSearchBrightness = brightness;
    }

    /**
     * Save this window's original brightness; use 90% during active search and 10% while idle without
     * faces.
     *
     * 保存本窗口原亮度；搜索活跃时使用 90% 背光，无人脸待机时降为 10%。
     */
    private void applySearchDisplayPolicy() {
        if (searchDisplayApplied) {
            return;
        }
        Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        previousWindowBrightness = attributes.screenBrightness;
        searchFacePresent = false;
        appliedSearchBrightness = Float.NaN;
        enterSearchActiveMode();
        searchDisplayApplied = true;
        scheduleNoFaceIdle();
    }

    /**
     * Stop brightness/frame-rate policies and restore brightness; -1 follows the system, and the keep-
     * awake flag belongs to this screen's window.
     *
     * 停止亮度/帧率策略并恢复原亮度；-1 表示跟随系统亮度，常亮标志属于搜索页窗口。
     */
    private void restoreSearchDisplayPolicy() {
        if (!searchDisplayApplied) {
            return;
        }
        displayHandler.removeCallbacks(enterSearchCameraIdle);
        Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.screenBrightness = previousWindowBrightness;
        window.setAttributes(attributes);
        searchFacePresent = false;
        appliedSearchBrightness = Float.NaN;
        searchDisplayApplied = false;
    }

    /**
     * Save supported search settings for Activity recreation, not as global preferences.
     *
     * 保存本页面支持的搜索设置，供 Activity 重建时恢复；不作为全局偏好存储。
     */
    @Override
    protected void onSaveInstanceState(Bundle state) {
        state.putBoolean("multi", config.getMode() == FaceSearchMode.MULTI_FACE);
        state.putBoolean("live", config.needsLiveness());
        state.putString("group", config.getGroup());
        state.putFloat("threshold", config.getThreshold());
        state.putInt("interval", config.getSearchIntervalMillis());
        state.putInt("timeout", config.getSearchTimeoutMillis());
        state.putInt("max", config.getMaxFacesPerFrame());
        super.onSaveInstanceState(state);
    }

    /**
     * Use Demo defaults for missing fields.
     *
     * 缺失字段沿用 Demo 默认配置。
     */
    private void restoreConfig(Bundle state) {
        if (state == null) {
            return;
        }
        boolean multi = state.getBoolean(
                "multi", config.getMode() == FaceSearchMode.MULTI_FACE);
        config = FaceSearchSessionConfig.builder()
                .mode(multi ? FaceSearchMode.MULTI_FACE : FaceSearchMode.SINGLE_FACE)
                .needLiveness(state.getBoolean("live", !multi)) // 仅单人搜索允许开启活体 / Only single-face search allows liveness.
                .group(state.getString("group", config.getGroup())) // 人脸库分组过滤 / Face-library group filter.
                .threshold(state.getFloat("threshold", config.getThreshold()))
                .searchIntervalMillis(state.getInt(
                        "interval", config.getSearchIntervalMillis()))
                .searchTimeoutMillis(state.getInt("timeout", config.getSearchTimeoutMillis()))
                .maxFacesPerFrame(state.getInt("max", config.getMaxFacesPerFrame()))
                .build();
    }
}

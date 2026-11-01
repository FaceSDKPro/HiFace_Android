#pragma once
#include "workflow.h"
#include "error_ui.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace hiface_linux_demo {
struct Command {
    std::string type;
    std::vector<std::string> values;
    std::vector<FormField> fields;
    uint64_t request_id = 0;
};
// UI reads published snapshots; mutable SDK/database state stays on the workflow thread.
// UI 读取已发布的状态快照；可变的 SDK 和数据库状态留在工作流线程。
struct ViewState {
    bool ready = false, importing = false, connected = false, busy = false, failed = false;
    bool camera_stalled = false, search_idle = false;
    std::string fixed_camera_device;
    HiFaceStatus initialization_status = HIFACE_STATUS_OK;
    int system_error = 0;
    RuntimeInfo runtime;
    std::string device_time_utc;
    uint64_t settings_attempt = 0, photo_search_generation = 0, preview_sequence = 0, visual_revision = 0;
    bool photo_search_busy = false, pending_enrollment = false;
    uint64_t enrollment_generation = 0;
    char pending_capture_kind = 0, saved_capture_kind = 0;
    uint64_t saved_capture_generation = 0, flow_result_generation = 0;
    uint64_t enrollment_attempt = 0;
    uint64_t metadata_attempt = 0, gallery_attempt = 0, gallery_revision = 0;
    uint32_t gallery_next_offset = 0;
    bool gallery_has_more = false;
    std::string gallery_query_id;
    bool flow_result_pending = false;
    bool flow_result_can_retry = true;
    std::string duplicate_enrollment_id, result_title;
    std::vector<GalleryEntry> captured_faces;
    bool comparison_first_ready = false, comparison_second_ready = false;
    bool comparison_first_processing = false, comparison_second_processing = false;
    std::string comparison_first_error, comparison_second_error;
    std::optional<float> comparison_similarity;
    std::shared_ptr<Image> search_photo;
    std::shared_ptr<const ImageSearchReport> photo_search;
    std::string search_photo_path;
    uint32_t import_saved = 0, import_failed = 0, import_processed = 0, import_total = 0;
    std::string status = "Starting SDK...", camera, device = "/dev/video0", reference, data_directory, demo_features, lighting;
    char mode = 'D';
    uint64_t count = 0, filtered_count = 0;
    uint32_t gallery_offset = 0;
    double milliseconds = 0, fps = 0, analysis_fps = 0;
    DemoSettings settings;
    std::shared_ptr<CameraFrame> frame;
    std::shared_ptr<Image> image_first, image_second, aligned_preview;
    std::vector<Box> boxes;
    // Live overlays are immutable and shared across preview-only snapshots.
    // 实时叠加层不可变；仅更新预览时共享它，不反复复制轮廓和标签。
    std::shared_ptr<const std::vector<Box>> camera_boxes;
    int64_t overlay_frame_ms = 0;
    uint64_t overlay_capture_generation = 0;
    int overlay_preview_width = 0, overlay_preview_height = 0;
    const std::vector<Box>& liveBoxes() const { return camera_boxes ? *camera_boxes : boxes; }
    bool overlayFresh(int64_t now, int64_t preview_frame_ms, uint64_t generation, const CameraFrame* preview = nullptr) const {
        return overlay_frame_ms > 0 && now >= overlay_frame_ms && now-overlay_frame_ms <= 1200 &&
            now >= preview_frame_ms && now-preview_frame_ms <= 500 && overlay_capture_generation == generation &&
            (!preview || (preview->width == overlay_preview_width && preview->height == overlay_preview_height));
    }
    std::vector<GalleryEntry> gallery;
    std::vector<GalleryEntry> image_matches;
};
// The capture thread owns V4L2 and continuously publishes only its newest frame.
// The workflow thread owns the SDK/database and analyzes snapshots independently,
// so a slow model call cannot reduce the live-preview cadence.
// 采集线程持有 V4L2 并持续发布最新帧；工作流线程持有 SDK/数据库并独立分析快照，
// 因此缓慢的模型调用不会降低实时预览的刷新频率。
class DesktopApp {
public:
    DesktopApp(std::string models, std::string database, std::string device, bool smoke, std::string assets = {});
    ~DesktopApp();
    void post(Command command);
    std::shared_ptr<const ViewState> snapshot();
private:
    struct CaptureRequest {
        uint64_t generation = 0;
        bool open = false, search_idle = false;
        std::string device, preview_device;
        int width = 640, height = 360, fps = 25;
        int preview_width = 1280, preview_height = 720;
        int preview_output_width = 576, preview_output_height = 1024;
        bool mjpeg = false;
    };
    struct CaptureState {
        bool active = false, ready = false, dual_preview = false;
        uint64_t sequence = 0, analysis_sequence = 0, generation = 0;
        int64_t last_frame_ms = 0, last_analysis_ms = 0;
        double fps = 0;
        std::string description = "Camera disconnected", error, lighting;
        std::shared_ptr<CameraFrame> frame, analysis_frame;
    };
    void captureLoop();
    void configureCapture(const std::string& device, const DemoSettings& settings, bool open);
    void setSearchIdle(bool idle);
    CaptureState captureSnapshot();
    void run(const std::string& models, const std::string& database, std::string device, bool smoke, const std::string& assets);
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> photo_generation_{0};
    std::atomic<uint64_t> comparison_generations_[2]{};
    bool smoke_ = false;
    std::mutex mutex_;
    std::condition_variable capture_changed_;
    std::condition_variable work_changed_;
    std::vector<Command> commands_;
    std::shared_ptr<const ViewState> state_ = std::make_shared<ViewState>();
    CaptureRequest capture_request_;
    CaptureState capture_state_;
    uint64_t capture_completed_ = 0;
    uint64_t visual_revision_ = 0;
    std::thread capture_worker_, worker_;
};
}

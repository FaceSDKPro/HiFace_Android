#include "app.h"
#include "lighting.h"
#include "frame_pool.h"
#include "navigation.h"
#include "search_power.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace hiface_linux_demo {
namespace {
constexpr int kPreviewFps = 25;
constexpr int64_t kPreviewIntervalMs = 1000 / kPreviewFps;
constexpr int64_t kAnalysisIntervalMs = 125; // Target 8 FPS; actual rate is reported separately. / 目标为 8 FPS，实际帧率另行报告。
int environmentInteger(const char* name, int fallback, int low, int high) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    return end && !*end && parsed >= low && parsed <= high ? static_cast<int>(parsed) : fallback;
}
std::vector<Box> previewBoxes(const std::vector<Box>& source, int analysis_width, int analysis_height,
                              const CameraFrame* preview) {
    if (analysis_width <= 0 || analysis_height <= 0 || !preview ||
        (analysis_width == preview->width && analysis_height == preview->height))
        return source;
    const float x_scale = float(preview->width) / analysis_width;
    const float y_scale = float(preview->height) / analysis_height;
    auto mapped = source;
    for (auto& box : mapped) {
        box.x *= x_scale; box.width *= x_scale;
        box.y *= y_scale; box.height *= y_scale;
        for (auto& point : box.points) { point.x *= x_scale; point.y *= y_scale; }
        for (auto& line : box.contours) for (auto& point : line) { point.x *= x_scale; point.y *= y_scale; }
    }
    return mapped;
}
}
DesktopApp::DesktopApp(std::string models, std::string database, std::string device, bool smoke, std::string assets)
    : smoke_(smoke), capture_worker_([this] { captureLoop(); }),
      worker_([this, models, database, device, smoke, assets] { run(models, database, device, smoke, assets); }) {}
DesktopApp::~DesktopApp() {
    stopping_ = true;
    capture_changed_.notify_all();
    work_changed_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (capture_worker_.joinable()) capture_worker_.join();
}
void DesktopApp::post(Command command) {
    std::lock_guard<std::mutex> lock(mutex_);
    work_changed_.notify_one();
    if (command.type == "preview-photo") {
        const int slot = command.values.at(0) == "first" ? 0 : 1;
        command.request_id = ++comparison_generations_[slot];
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(), [&](const Command& queued) {
            return queued.type == "preview-photo" && queued.values.at(0) == command.values.at(0);
        }), commands_.end());
        auto pending = std::make_shared<ViewState>(*state_); pending->busy = true; pending->comparison_similarity.reset();
        (slot == 0 ? pending->comparison_first_processing : pending->comparison_second_processing) = true;
        pending->visual_revision = ++visual_revision_; state_ = std::move(pending);
        commands_.push_back(std::move(command)); return;
    }
    if (command.type == "page") {
        ++comparison_generations_[0]; ++comparison_generations_[1];
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(), [](const Command& queued) { return queued.type == "preview-photo"; }), commands_.end());
    }
    if (command.type == "image-search" || command.type == "cancel-image-search") {
        command.request_id = ++photo_generation_;
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(), [](const Command& queued) { return queued.type == "image-search" || queued.type == "cancel-image-search"; }), commands_.end());
        auto pending = std::make_shared<ViewState>(*state_);
        pending->visual_revision = ++visual_revision_;
        pending->photo_search_generation = command.request_id; pending->photo_search_busy = pending->busy = true; pending->failed = false;
        pending->search_photo.reset(); pending->photo_search.reset(); pending->search_photo_path.clear();
        pending->status = command.type == "image-search" ? "Queued photo search..." : "Cancelling photo search..."; state_ = std::move(pending);
        commands_.push_back(std::move(command)); return;
    }
    // Bound repeated key presses while a model call is in flight.
    // 模型调用执行期间限制重复按键产生的任务数量。
    if (commands_.size() < 32) {
        auto pending = std::make_shared<ViewState>(*state_); pending->busy = true; pending->visual_revision = ++visual_revision_;
        state_ = std::move(pending); commands_.push_back(std::move(command));
    }
}
std::shared_ptr<const ViewState> DesktopApp::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!capture_state_.active) return state_;
    auto view = std::make_shared<ViewState>(*state_);
    if (capture_state_.active) {
        view->connected = capture_state_.ready;
        const auto now = monotonicMilliseconds();
        view->camera_stalled = capture_state_.ready && (!capture_state_.frame || !capture_state_.analysis_frame ||
            now - capture_state_.last_frame_ms > 3000 || now - capture_state_.last_analysis_ms > 3000);
        view->camera = capture_state_.description;
        view->frame = capture_state_.frame;
        view->fps = capture_state_.fps;
        view->preview_sequence = capture_state_.sequence;
        view->lighting = capture_state_.lighting;
        if (!capture_state_.ready || !view->overlayFresh(monotonicMilliseconds(), capture_state_.last_frame_ms, capture_request_.generation, view->frame.get())) {
            view->camera_boxes.reset(); view->boxes.clear();
            if (view->mode == 'S' && !view->failed && !view->busy && view->overlay_frame_ms > 0) {
                view->analysis_fps = 0;
                view->status = "Waiting for fresh camera analysis...";
            }
        }
        if (view->camera_stalled) {
            view->fps = view->analysis_fps = 0;
            if (!view->failed) view->status = "No fresh camera frames. Use Reconnect to retry the current camera.";
        }
        if (!capture_state_.error.empty()) {
            view->failed = true;
            view->status = capture_state_.error;
        }
    }
    return view;
}
DesktopApp::CaptureState DesktopApp::captureSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return capture_state_;
}
void DesktopApp::configureCapture(const std::string& device, const DemoSettings& settings, bool open) {
    std::unique_lock<std::mutex> lock(mutex_);
    const uint64_t generation = ++capture_request_.generation;
    capture_request_.open = open;
    capture_request_.search_idle = false;
    capture_request_.device = device;
    capture_request_.width = settings.camera_width;
    capture_request_.height = settings.camera_height;
    capture_request_.fps = std::clamp(settings.camera_fps, 20, kPreviewFps);
    capture_request_.mjpeg = settings.camera_mjpeg;
    const char* preview_device = std::getenv("HIFACE_PREVIEW_DEVICE");
    capture_request_.preview_device = preview_device ? preview_device : "";
    capture_request_.preview_width = environmentInteger("HIFACE_PREVIEW_WIDTH", 1280, 2, 1920);
    capture_request_.preview_height = environmentInteger("HIFACE_PREVIEW_HEIGHT", 720, 2, 1080);
    capture_request_.preview_output_width = environmentInteger("HIFACE_PREVIEW_OUTPUT_WIDTH", 576, 2, 1920);
    capture_request_.preview_output_height = environmentInteger("HIFACE_PREVIEW_OUTPUT_HEIGHT", 1024, 2, 1920);
    capture_changed_.notify_all();
    capture_changed_.wait(lock, [&] { return stopping_ || capture_completed_ >= generation; });
    if (stopping_) return;
    if (open && !capture_state_.error.empty()) throw DemoError{capture_state_.error};
}
void DesktopApp::setSearchIdle(bool idle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capture_request_.search_idle == idle) return;
    capture_request_.search_idle = idle;
    capture_changed_.notify_all();
}
void DesktopApp::captureLoop() {
    Camera camera, preview_camera;
    CameraFramePool preview_pool, analysis_pool;
    CameraLighting lighting;
    const char* light_device_env = std::getenv("HIFACE_LIGHT_CAMERA_DEVICE");
    const std::string light_device = light_device_env ? light_device_env : "/dev/video31";
    uint64_t handled = 0;
    bool idle_applied = false;
    int64_t fps_start = monotonicMilliseconds(), next_synthetic = fps_start, next_analysis_capture = fps_start;
    int64_t next_preview_capture = fps_start;
    uint32_t frames = 0;
    while (!stopping_) {
        CaptureRequest request;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request = capture_request_;
            if (capture_request_.generation != handled) {
                changed = true;
            }
        }
        if (changed) {
            lighting.stop();
            camera.close();
            preview_camera.close();
            CaptureState next;
            next.generation = request.generation;
            next.active = request.open;
            next.last_frame_ms = next.last_analysis_ms = monotonicMilliseconds();
            fps_start = next.last_frame_ms;
            next_synthetic = fps_start;
            next_analysis_capture = fps_start;
            next_preview_capture = fps_start;
            idle_applied = false;
            frames = 0;
            try {
                if (request.open) {
                    if (smoke_) {
                        auto frame = std::make_shared<CameraFrame>();
                        rgbaFrame(Image{640, 360, std::vector<uint8_t>(640 * 360 * 4, 128)}, *frame);
                        next.ready = true;
                        next.description = "Synthetic frame / desktop smoke test";
                        next.frame = std::move(frame);
                    } else {
                        camera.open(request.device, request.width, request.height, request.fps, request.mjpeg);
                        next.ready = true;
                        next.description = camera.description();
                        const bool wants_dual = !request.preview_device.empty() && request.preview_device != request.device;
                        if (wants_dual) try {
                            preview_camera.open(request.preview_device, request.preview_width,
                                request.preview_height, request.fps, false);
                            next.dual_preview = true;
                            next.description = "Preview: " + preview_camera.description() + " -> " +
                                std::to_string(request.preview_output_width) + "x" +
                                std::to_string(request.preview_output_height) + " RGA | Recognition: " +
                                camera.description();
                        } catch (const DemoError& error) {
                            preview_camera.close();
                            next.description += " | HD preview fallback: " + error.message;
                        }
                    }
                }
            } catch (const DemoError& error) {
                next.error = error.message;
                next.description = "Camera disconnected";
                camera.close();
                preview_camera.close();
            } catch (const std::exception& error) {
                next.error = error.what();
                next.description = "Camera disconnected";
                camera.close();
                preview_camera.close();
            } catch (...) {
                next.error = "Unexpected camera failure";
                next.description = "Camera disconnected";
                camera.close();
                preview_camera.close();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                capture_state_ = std::move(next);
                capture_completed_ = request.generation;
                handled = request.generation;
            }
            capture_changed_.notify_all();
            continue;
        }

        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = capture_state_.active && capture_state_.ready;
        }
        if (!ready) {
            lighting.stop();
            std::unique_lock<std::mutex> lock(mutex_);
            capture_changed_.wait(lock, [&] {
                return stopping_ || capture_request_.generation != handled;
            });
            continue;
        }

        try {
            const auto cadence_now = monotonicMilliseconds();
            if (idle_applied != request.search_idle) {
                const int requested_fps = request.search_idle ? kSearchIdlePreviewFps : request.fps;
                const bool camera_rate = smoke_ || camera.requestFrameRate(requested_fps);
                const bool preview_rate = smoke_ || !preview_camera.ready() || preview_camera.requestFrameRate(requested_fps);
                std::fprintf(stderr, "[SearchPower] %s preview_target=%d hardware_rate=%s\n",
                    request.search_idle ? "idle" : "active", requested_fps,
                    camera_rate && preview_rate ? "requested" : "unsupported; processing throttled");
                idle_applied = request.search_idle;
                fps_start = next_synthetic = next_analysis_capture = next_preview_capture = cadence_now;
                frames = 0;
            }
            const auto preview_interval = request.search_idle ? kSearchIdlePreviewIntervalMs : kPreviewIntervalMs;
            const auto analysis_interval = request.search_idle ? int64_t(HIFACE_IDLE_FRAME_INTERVAL_MS) : kAnalysisIntervalMs;
            // Both active and idle capture have deadlines. Sleep until the next
            // due stream instead of polling every millisecond between frames.
            // 活跃和待机采集都有截止时间；等待下一路到期，避免帧间每毫秒轮询。
            if (cadence_now < std::min(next_preview_capture, next_analysis_capture)) {
                std::unique_lock<std::mutex> lock(mutex_);
                capture_changed_.wait_for(lock, std::chrono::milliseconds(std::min(next_preview_capture, next_analysis_capture) - cadence_now), [&] {
                    return stopping_ || capture_request_.generation != handled || capture_request_.search_idle != idle_applied;
                });
                continue;
            }
            const bool lighting_active = !smoke_ && request.device == light_device;
            lighting.update(nullptr, lighting_active);
            std::shared_ptr<CameraFrame> frame, analysis_frame;
            const auto now = monotonicMilliseconds();
            if (smoke_) {
                if (now < next_synthetic) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                next_synthetic = next_preview_capture = next_analysis_capture = now + preview_interval;
                std::lock_guard<std::mutex> lock(mutex_);
                frame = capture_state_.frame;
                analysis_frame = frame;
            } else {
                bool dual = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    dual = capture_state_.dual_preview;
                }
                if (dual) {
                    try {
                        if (now >= next_preview_capture && preview_camera.waitForFrame(10)) {
                            auto candidate = preview_pool.acquire();
                            if (candidate && preview_camera.readPreview(*candidate, request.preview_output_width,
                                    request.preview_output_height)) {
                                frame = std::move(candidate); next_preview_capture = now + preview_interval;
                            }
                        }
                    } catch (const DemoError& error) {
                        preview_camera.close();
                        std::lock_guard<std::mutex> lock(mutex_);
                        capture_state_.dual_preview = false;
                        capture_state_.description = camera.description() + " | HD preview fallback: " + error.message;
                    }
                    if (now >= next_analysis_capture) {
                        auto candidate = analysis_pool.acquire();
                        if (candidate && camera.readAnalysis(*candidate)) {
                            analysis_frame = std::move(candidate);
                            next_analysis_capture = now + analysis_interval;
                        }
                    }
                    if (!frame && !analysis_frame) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                } else {
                    auto candidate = preview_pool.acquire();
                    if (!candidate || !camera.waitForFrame(10) || !camera.read(*candidate)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        continue;
                    }
                    frame = analysis_frame = std::move(candidate);
                    next_preview_capture = next_analysis_capture = now + preview_interval;
                }
            }
            const auto captured = monotonicMilliseconds();
            if (analysis_frame) lighting.update(analysis_frame.get(), lighting_active);
            if (frame) ++frames;
            std::lock_guard<std::mutex> lock(mutex_);
            if (capture_request_.generation != handled || !capture_state_.active) continue;
            capture_state_.lighting = lighting.status();
            if (frame) {
                capture_state_.frame = std::move(frame);
                capture_state_.last_frame_ms = captured;
                ++capture_state_.sequence;
            }
            if (analysis_frame) {
                capture_state_.analysis_frame = std::move(analysis_frame);
                capture_state_.last_analysis_ms = captured;
                ++capture_state_.analysis_sequence;
                work_changed_.notify_one();
                if (!capture_state_.dual_preview && !capture_state_.frame)
                    capture_state_.frame = capture_state_.analysis_frame;
            }
            if (captured - fps_start >= 1000) {
                capture_state_.fps = frames * 1000.0 / std::max<int64_t>(1, captured - fps_start);
                frames = 0;
                fps_start = captured;
            }
        } catch (const DemoError& error) {
            lighting.stop();
            camera.close();
            preview_camera.close();
            std::lock_guard<std::mutex> lock(mutex_);
            capture_state_.ready = false;
            capture_state_.frame.reset();
            capture_state_.analysis_frame.reset();
            capture_state_.error = error.message;
            capture_state_.description = "Camera disconnected";
        } catch (const std::exception& error) {
            lighting.stop();
            camera.close();
            preview_camera.close();
            std::lock_guard<std::mutex> lock(mutex_);
            capture_state_.ready = false;
            capture_state_.frame.reset();
            capture_state_.analysis_frame.reset();
            capture_state_.error = error.what();
            capture_state_.description = "Camera disconnected";
        }
    }
    camera.close();
    preview_camera.close();
}
void DesktopApp::run(const std::string& models, const std::string& database, std::string device, bool smoke, const std::string& assets) {
    auto publish = [&](std::shared_ptr<ViewState> state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state->visual_revision = ++visual_revision_;
        if (state->ready && state->photo_search_generation != photo_generation_) {
            state->photo_search_generation = photo_generation_; state->photo_search_busy = state->busy = true;
            state->search_photo.reset(); state->photo_search.reset(); state->search_photo_path.clear(); state->failed = false; state->status = state_->status;
        }
        state_ = std::move(state);
    };
    try {
        const auto data = std::filesystem::path(database).has_parent_path() ? std::filesystem::path(database).parent_path() : std::filesystem::path(".");
        const auto fixture = std::filesystem::path(assets) / "features_10000.json";
        const std::string demo_features = std::filesystem::exists(fixture) ? fixture.string() : "";
        std::filesystem::create_directories(data);
        // Only this worker creates or calls SDK/database resources; V4L2 belongs to captureLoop().
        // 仅此工作线程创建和调用 SDK/数据库资源；V4L2 由 captureLoop() 持有。
        FaceWorkflow workflow(models, database);
        const char* fixed_env = std::getenv("HIFACE_FIXED_CAMERA_DEVICE");
        const std::string fixed_camera = fixed_env ? fixed_env : "";
        require(fixed_camera.empty() || fixed_camera == device, "The configured board camera differs from the startup device");
        bool failed = false;
        uint64_t settings_attempt = 0, photo_generation = 0;
        uint64_t enrollment_attempt = 0;
        uint64_t metadata_attempt = 0, gallery_attempt = 0, gallery_revision = 0;
        uint64_t comparison_completed[2]{};
        std::string page = "home", gallery_id, gallery_group, gallery_tag;
        // Android keeps the photo-search Activity beneath library management.
        // Retain its immutable result only while that child page is open.
        // 与 Android 一样，仅在人脸库子页打开期间保留图片搜索页的不可变结果。
        std::shared_ptr<Image> suspended_photo;
        std::shared_ptr<const ImageSearchReport> suspended_report;
        std::string suspended_photo_path;
        double suspended_photo_ms = 0;
        uint32_t gallery_offset = 0;
        int next_index = 0;
        SearchPowerPolicy search_power;
        SearchBacklight backlight;
        int64_t analysis_fps_start = monotonicMilliseconds(), next_analysis = analysis_fps_start, last_publish = 0;
        uint64_t analyzed_sequence = 0;
        uint32_t analysis_frames = 0;
        double analysis_fps = 0;
        int overlay_width = 0, overlay_height = 0;
        int64_t overlay_frame_ms = 0;
        uint64_t overlay_generation = 0;
        if (device.rfind("/dev/video", 0) == 0) try { next_index = std::stoi(device.substr(10)); } catch (...) {}
        auto connect = [&](bool reset_flow = true) {
            search_power.reset(); backlight.update(false, false);
            require(fixed_camera.empty() || device == fixed_camera, "This board uses the fixed camera " + fixed_camera);
            if (reset_flow) workflow.clearPageResults();
            configureCapture(device, workflow.settings(), true);
            analyzed_sequence = 0;
            overlay_frame_ms = 0;
            analysis_fps_start = next_analysis = monotonicMilliseconds();
            analysis_frames = 0;
            analysis_fps = 0;
            if (reset_flow) workflow.status = "Camera connected. Look at the camera.";
        };
        auto camera_needs_reconnect = [&] {
            const auto capture = captureSnapshot();
            const auto now = monotonicMilliseconds();
            return !capture.active || !capture.ready || !capture.frame || !capture.analysis_frame ||
                now - capture.last_frame_ms > 3000 || now - capture.last_analysis_ms > 3000;
        };
        workflow.status = "Choose a feature from Home to begin.";
        auto gallery = [&](bool append = false) {
            ++gallery_attempt;
            if (!append) { ++gallery_revision; gallery_offset = 0; }
            workflow.loadGallery(gallery_offset, gallery_id, gallery_group, gallery_tag, append);
        };
        while (!stopping_) {
            std::vector<Command> commands;
            { std::lock_guard<std::mutex> lock(mutex_); commands.swap(commands_); }
            bool workflow_updated = !commands.empty();
            if (!commands.empty()) search_power.reset();
            for (const auto& command : commands) {
                if(command.type=="gallery-more" && (page!="gallery" || workflow.importing())){++gallery_attempt;continue;}
                const bool comparison_command = command.type == "preview-photo";
                const int comparison_slot = comparison_command && command.values.at(0) == "second" ? 1 : 0;
                if (comparison_command && command.request_id != comparison_generations_[comparison_slot]) continue;
                const bool photo_command = command.type == "image-search" || command.type == "cancel-image-search";
                if (photo_command && command.request_id != photo_generation_) continue;
                if (photo_command) photo_generation = command.request_id;
                if (command.type == "settings") ++settings_attempt;
                if (command.type == "confirm-enrollment") ++enrollment_attempt;
                if (command.type == "metadata") ++metadata_attempt;
                auto working = std::make_shared<ViewState>(*snapshot());
                working->busy = true; working->failed = false; working->status = command.type == "image-search" ? "Detecting and searching every face..." : command.type == "image" ? "Processing photo" : command.type == "import" ? "Reading import source" : "Applying request";
                publish(working);
                try {
                    failed = false;
                    const auto& v = command.values;
                    if (workflow.importing()) require(command.type == "cancel-import" || command.type == "gallery",
                        "Stop the import before another operation");
                    if (command.type == "page") {
                        require(v.at(0) == "home" || demoPage(v.at(0)), "Unknown feature page");
                        if(page=="gallery" && v.at(0)!="gallery") {
                            workflow.gallery_rows.clear();workflow.gallery_has_more=false;workflow.gallery_next_offset=0;
                        }
                        const bool returning_to_photo = page == "gallery" && v.at(0) == "image-search";
                        if (page == "image-search" && v.at(0) == "gallery") {
                            suspended_photo = workflow.search_photo; suspended_report = workflow.photo_search;
                            suspended_photo_path = workflow.search_photo_path; suspended_photo_ms = workflow.inference_ms;
                        } else if (!returning_to_photo) {
                            suspended_photo.reset(); suspended_report.reset(); suspended_photo_path.clear();
                        }
                        workflow.clearPageResults(); page = v.at(0); analysis_fps = 0; analysis_frames = 0;
                        comparison_completed[0] = comparison_generations_[0]; comparison_completed[1] = comparison_generations_[1];
                        if (returning_to_photo) {
                            workflow.search_photo = std::move(suspended_photo); workflow.photo_search = std::move(suspended_report);
                            workflow.search_photo_path = std::move(suspended_photo_path); workflow.inference_ms = suspended_photo_ms;
                        }
                        if (cameraPage(page)) {
                            connect();
                            const auto mode = demoPage(page)->camera_mode;
                            if (mode != 'D') workflow.action(mode);
                        } else configureCapture(device, workflow.settings(), false);
                        if (page == "gallery") {
                            gallery_offset = 0; gallery_id.clear(); gallery_group.clear(); gallery_tag.clear(); gallery();
                        }
                        if (page == "references") { workflow.loadCapturedFaces(); workflow.status = "Select a captured Face ID to verify."; }
                        if (page == "image-search") workflow.status = workflow.photoSearchSummary();
                        if (page == "camera-settings") workflow.status = "Camera preferences save automatically. Open a camera feature to use them.";
                        if (page == "home") workflow.status = "Choose a feature from Home to begin.";
                        if (page == "images") workflow.status = "Choose two photos to compare. Photo comparison does not perform liveness.";
                        if (page == "settings") workflow.status = "Edit this feature's settings, apply to save, then go back.";
                    } else if (command.type == "image-search") {
                        page = "image-search"; configureCapture(device, workflow.settings(), false);
                        workflow.searchPhoto(v.at(0), [&] { return stopping_ || command.request_id != photo_generation_; });
                    } else if (command.type == "cancel-image-search") { workflow.clearPhotoSearch(); workflow.status = "Photo search cancelled.";
                    } else if (command.type == "action") {
                        if (v.at(0).at(0) != 'X' && camera_needs_reconnect()) connect(false);
                        workflow.action(v.at(0).at(0));
                    } else if (command.type == "connect") {
                        require(fixed_camera.empty() || v.at(0) == fixed_camera, "This board uses the fixed camera " + fixed_camera);
                        device = v.at(0); connect(false);
                    }
                    else if (command.type == "next") {
                        if (!fixed_camera.empty()) {
                            device = fixed_camera; connect(false);
                        } else {
                            bool connected = false;
                            for (int i = 0; i < 64; ++i) {
                                next_index = (next_index + 1) % 64; device = "/dev/video" + std::to_string(next_index);
                                if (!std::filesystem::exists(device)) continue;
                                try { connect(false); connected = true; break; } catch (const DemoError&) {}
                            }
                            if (!connected) workflow.status = "No usable camera. Connect the USB device to Ubuntu and check /dev/video* permissions.";
                        }
                    } else if (command.type == "mirror") workflow.toggleMirror();
                    else if (command.type == "pause") {
                        if (workflow.mode() == 'P' && camera_needs_reconnect()) connect(false);
                        workflow.pauseSearch(); overlay_frame_ms = 0;
                        analysis_fps = 0; analysis_frames = 0;
                        analysis_fps_start = next_analysis = monotonicMilliseconds();
                    }
                    else if (command.type == "settings") { workflow.applySettings(parseSettings(command.fields)); }
                    else if (command.type == "confirm-enrollment") { workflow.confirmEnrollment(v.at(0), v.at(1), v.at(2), v.size() > 3 && v.at(3) == "replace"); }
                    else if (command.type == "capture-reference") workflow.selectCapturedReference(v.at(0));
                    else if (command.type == "preview-photo") {
                        workflow.previewComparisonPhoto(comparison_slot == 0, v.at(1), [&] { return stopping_ || command.request_id != comparison_generations_[comparison_slot]; });
                        comparison_completed[comparison_slot] = command.request_id;
                    }
                    else if (command.type == "enroll") {
                        require(captureSnapshot().ready, "Connect a camera first"); workflow.setEnrollment(v.at(0), v.at(1), v.at(2)); workflow.action('E');
                    } else if (command.type == "image") {
                        configureCapture(device, workflow.settings(), false); analysis_fps = 0; analysis_frames = 0;
                        workflow.imageOperation(v.at(0), v.at(1), v.at(2), v.at(3), v.at(4), v.at(5));
                    }
                    else if (command.type == "gallery") {
                        gallery_offset = static_cast<uint32_t>(std::stoul(v.at(0))); gallery_id = v.at(1); gallery_group = v.at(2); gallery_tag = v.at(3); gallery();
                    } else if (command.type == "gallery-more") {
                        if (page == "gallery" && gallery_id.empty() && workflow.gallery_has_more &&
                            std::stoull(v.at(1)) == gallery_revision && std::stoul(v.at(0)) == workflow.gallery_next_offset) {
                            gallery_offset = workflow.gallery_next_offset; gallery(true);
                        } else ++gallery_attempt;
                    } else if (command.type == "delete") { workflow.deleteEntry(v.at(0));const auto result=workflow.status;gallery();workflow.status=result; }
                    else if (command.type == "metadata") { workflow.updateEntry(v.at(0), v.at(1), v.at(2));const auto result=workflow.status;gallery();workflow.status=result; }
                    else if (command.type == "reference") workflow.selectReference(v.at(0));
                    else if (command.type == "clear-reference") workflow.clearReference();
                    else if (command.type == "export") workflow.exportGallery(v.at(0), gallery_group, gallery_tag);
                    else if (command.type == "import") workflow.startImport(v.at(0), v.at(1) == "images", v.at(2));
                    else if (command.type == "cancel-import") { workflow.cancelImport(); const auto status = workflow.status; gallery(); workflow.status = status; }
                } catch (const DemoError& error) {
                    failed = true; workflow.status = error.message;
                    if (command.type == "action" && !command.values.empty()) workflow.showFlowError(command.values[0][0], error);
                    if (command.type == "page" && !command.values.empty() && cameraPage(command.values[0]))
                        workflow.showFlowError(demoPage(command.values[0])->camera_mode, error);
                    std::fprintf(stderr, "[Demo] %s\n", error.message.c_str());
                }
                catch (const std::exception& error) { failed = true; workflow.status = error.what(); }
                catch (...) { failed = true; workflow.status = "Unexpected command failure"; }
            }
            if (workflow.importing()) {
                try {
                    workflow.stepImport();
                    if (!workflow.importing()) { const auto status = workflow.status; gallery(); workflow.status = status; workflow_updated = true; }
                } catch (const DemoError& error) { failed = true; workflow.status += " | " + error.message; }
                catch (const std::exception& error) { failed = true; workflow.cancelImport(); workflow.status = error.what(); }
            }
            const bool analyze = cameraPage(page) && workflow.mode() != 'P' && !workflow.importing() && !workflow.pendingEnrollment() && !workflow.resultPending();
            const auto power_capture = captureSnapshot();
            const bool power_enabled = analyze && workflow.mode() == 'S' && !failed && power_capture.ready &&
                power_capture.frame && power_capture.analysis_frame && monotonicMilliseconds() - power_capture.last_analysis_ms < 3000 &&
                monotonicMilliseconds() - power_capture.last_frame_ms < 3000;
            if (!power_enabled) search_power.reset();
            if (analyze) {
                const auto latest = captureSnapshot();
                const auto now = monotonicMilliseconds();
                if (!latest.error.empty()) {
                    failed = true;
                    workflow.showFlowError(workflow.mode(), DemoError{latest.error});
                } else if (latest.ready && latest.analysis_frame && latest.analysis_sequence != analyzed_sequence && now >= next_analysis) {
                    analyzed_sequence = latest.analysis_sequence;
                    const auto begin = now;
                    const char active_mode = workflow.mode();
                    try {
                        // Commit boxes, identity and same-frame liveness together below.
                        // A provisional Searching snapshot would erase them once per frame.
                        // 下方一次性发布框、身份和本帧活体结果；中途发布 Searching 会导致逐帧闪烁。
                        const bool processed = workflow.process(*latest.analysis_frame);
                        if (processed) {
                            if (power_enabled) search_power.observe(monotonicMilliseconds(), !workflow.boxes.empty());
                            ++analysis_frames;
                            overlay_width = latest.analysis_frame->width; overlay_height = latest.analysis_frame->height;
                            overlay_frame_ms = latest.last_analysis_ms; overlay_generation = latest.generation;
                        }
                    } catch (const DemoError& error) {
                        search_power.reset();
                        failed = true; overlay_frame_ms = 0; workflow.showFlowError(active_mode, error);
                    }
                    workflow_updated = true;
                    next_analysis = begin + search_power.analysisInterval();
                }
            }
            setSearchIdle(search_power.idle());
            backlight.update(power_enabled, search_power.idle());
            if (!analyze || workflow.pendingEnrollment() || workflow.resultPending()) { analysis_fps = 0; analysis_frames = 0; }
            const auto now = monotonicMilliseconds();
            if (now - analysis_fps_start >= 1000) {
                analysis_fps = analysis_frames * 1000.0 / std::max<int64_t>(1, now - analysis_fps_start);
                analysis_frames = 0;
                analysis_fps_start = now;
            }
            // Capture snapshots carry the newest preview independently. Only
            // copy the larger workflow/gallery state after actual work or the
            // one-second status heartbeat, not 33 times per second while idle.
            // 采集快照独立携带最新预览；仅在实际工作完成或一秒状态心跳时复制较大的工作流与库状态，不在空闲时每秒复制 33 次。
            if (now - last_publish >= 1000 || workflow_updated || (workflow.importing() && now - last_publish >= 30)) {
                const auto capture = captureSnapshot();
                auto state = std::make_shared<ViewState>();
                state->ready = true; state->importing = workflow.importing(); state->connected = capture.ready;
                state->search_idle = search_power.idle();
                state->pending_enrollment = workflow.pendingEnrollment(); state->enrollment_generation = workflow.enrollmentGeneration();
                state->pending_capture_kind = workflow.pendingCaptureKind(); state->captured_faces = workflow.captured_faces;
                state->enrollment_attempt = enrollment_attempt;
                state->metadata_attempt = metadata_attempt; state->gallery_attempt = gallery_attempt; state->gallery_revision = gallery_revision;
                state->gallery_next_offset = workflow.gallery_next_offset; state->gallery_has_more = workflow.gallery_has_more; state->gallery_query_id = gallery_id;
                state->saved_capture_generation = workflow.saved_capture_generation; state->saved_capture_kind = workflow.saved_capture_kind;
                state->duplicate_enrollment_id = workflow.duplicate_enrollment_id;
                state->flow_result_pending = workflow.resultPending(); state->flow_result_generation = workflow.flow_result_generation; state->result_title = workflow.result_title;
                state->flow_result_can_retry = workflow.result_can_retry;
                state->action_seconds_remaining = workflow.actionSecondsRemaining();
                state->comparison_first_processing = comparison_completed[0] != comparison_generations_[0];
                state->comparison_second_processing = comparison_completed[1] != comparison_generations_[1];
                state->comparison_first_ready = workflow.comparison_first_ready && !state->comparison_first_processing;
                state->comparison_second_ready = workflow.comparison_second_ready && !state->comparison_second_processing;
                state->comparison_first_error = workflow.comparison_first_error; state->comparison_second_error = workflow.comparison_second_error;
                state->comparison_similarity = state->comparison_first_processing || state->comparison_second_processing ? std::nullopt : workflow.comparison_similarity;
                state->failed = failed; state->settings_attempt = settings_attempt;
                state->photo_search_generation = photo_generation;
                state->search_photo = workflow.search_photo; state->photo_search = workflow.photo_search; state->search_photo_path = workflow.search_photo_path;
                state->import_saved = workflow.import_saved; state->import_failed = workflow.import_failed; state->import_processed = workflow.import_processed; state->import_total = workflow.import_total;
                state->status = workflow.status; state->camera = capture.description; state->lighting = capture.lighting;
                state->device = device; state->reference = workflow.reference_id; state->data_directory = data.string();
                state->fixed_camera_device = fixed_camera;
                state->demo_features = demo_features;
                state->mode = workflow.mode(); state->count = workflow.gallery_count; state->filtered_count = workflow.filtered_count; state->gallery_offset = workflow.gallery_offset;
                state->settings = workflow.settings(); state->milliseconds = workflow.inference_ms;
                state->fps = capture.fps; state->analysis_fps = analysis_fps; state->frame = capture.frame;
                state->preview_sequence = capture.sequence;
                state->camera_stalled = capture.active && capture.ready && (!capture.frame || !capture.analysis_frame ||
                    now - capture.last_frame_ms > 3000 || now - capture.last_analysis_ms > 3000);
                if (cameraPage(page) && state->camera_stalled)
                    state->status = "No fresh camera frames. Use Reconnect to retry the current camera.";
                if (cameraPage(page)) {
                    state->overlay_frame_ms = overlay_frame_ms; state->overlay_capture_generation = overlay_generation;
                    state->overlay_preview_width = capture.frame ? capture.frame->width : 0;
                    state->overlay_preview_height = capture.frame ? capture.frame->height : 0;
                    if (capture.ready && state->mode != 'P' && state->overlayFresh(monotonicMilliseconds(), capture.last_frame_ms, capture.generation))
                        state->camera_boxes = std::make_shared<const std::vector<Box>>(previewBoxes(workflow.boxes, overlay_width, overlay_height, capture.frame.get()));
                } else state->boxes = workflow.boxes;
                state->gallery = workflow.gallery_rows; state->image_first = workflow.image_first; state->image_second = workflow.image_second; state->aligned_preview = workflow.aligned_preview;
                state->image_matches = workflow.image_matches;
                publish(state); last_publish = now;
            }
            if (workflow.importing()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            else {
                std::unique_lock<std::mutex> lock(mutex_);
                auto deadline = last_publish + 1000;
                if (analyze && capture_state_.ready && capture_state_.analysis_frame && capture_state_.analysis_sequence != analyzed_sequence)
                    deadline = std::min(deadline, std::max(now + 1, next_analysis));
                // Wake on a newly published frame even if it arrives just before
                // next_analysis. The next loop can then wait for that deadline;
                // otherwise the early notification is lost until another frame.
                // 新帧即使略早于 next_analysis 也应唤醒，下一轮再等待识别截止时间；
                // 否则会错过本次通知，一直多等到另一帧到达。
                const auto waiting_sequence = capture_state_.analysis_sequence;
                work_changed_.wait_for(lock, std::chrono::milliseconds(std::max<int64_t>(1, deadline - now)), [&] {
                    return stopping_ || !commands_.empty() || (analyze && !workflow.pendingEnrollment() && !workflow.resultPending() && capture_state_.ready &&
                        capture_state_.analysis_sequence != waiting_sequence);
                });
            }
        }
        configureCapture(device, workflow.settings(), false);
        workflow.cancelImport(); workflow.stop();
    } catch (const DemoError& error) {
        auto state = std::make_shared<ViewState>();
        state->failed = true; state->initialization_status = error.status;
        state->status = "SDK initialization failed: " + error.message;
        const auto now = std::time(nullptr); std::tm utc{}; char formatted[32]{};
        if (gmtime_r(&now, &utc) && std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S UTC", &utc))
            state->device_time_utc = formatted;
        std::fprintf(stderr, "[Startup] %s | device_time=%s\n", state->status.c_str(), state->device_time_utc.c_str());
        publish(state);
    }
    catch (const std::exception& error) { auto state = std::make_shared<ViewState>(); state->failed = true; state->status = error.what(); publish(state); }
    catch (...) { auto state = std::make_shared<ViewState>(); state->failed = true; state->status = "Unexpected worker failure"; publish(state); }
}
}

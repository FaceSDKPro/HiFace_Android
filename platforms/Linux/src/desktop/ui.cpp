#include "ui.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <sys/utsname.h>
#include <sstream>
namespace hiface_linux_demo {
namespace {
constexpr uint32_t background = 0xf5f5f7, surface = 0xffffff, ink = 0x242029, muted = 0x69636f, border = 0xe1e5eb;
constexpr uint32_t accent = 0x077a63, tint = 0xe6f4ef, danger = 0xb33d37, dark = 0x172233;
constexpr uint32_t technology_green = 0x00f5a0;
const std::string& deviceInfo() {
    static const std::string value = [] {
        struct utsname system{};
        return uname(&system) == 0 ? std::string(system.sysname) + " " + system.release + " / " + system.machine : std::string("Linux");
    }();
    return value;
}
bool sdkSetting(const std::string& key) { return key.rfind("camera_", 0) == 0 || key == "mirror" || key == "ui_rotation"; }
bool settingsPage(const std::string& page) { return page == "settings" || page == "camera-settings"; }
std::string trimText(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n\f\v");
    return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n\f\v") - first + 1);
}
Rect fitImage(Rect frame, int width, int height) {
    if (width <= 0 || height <= 0) return {};
    const double scale = std::min(double(frame.w) / width, double(frame.h) / height);
    const int w = std::max(1, int(width * scale)), h = std::max(1, int(height * scale));
    return {frame.x + (frame.w - w) / 2, frame.y + (frame.h - h) / 2, w, h};
}
std::string number(double value, int decimals = 1) { std::ostringstream s; s << std::fixed << std::setprecision(decimals) << value; return s.str(); }
const char* modeName(char mode) {
    switch (mode) { case 'C': return "Capture"; case 'L': return "Liveness"; case 'V': return "Verify"; case 'E': return "Enroll"; case 'S': return "Search"; case 'P': return "Search paused"; default: return "Detection"; }
}
std::string friendly(const std::string& text) {
    if (text.find("IMAGE_NO_FACE") != std::string::npos) return "No face found. Choose a clear photo with a visible face.";
    if (text.find("IMAGE_FACE_TOO_SMALL") != std::string::npos) return "The face is too small. Choose a photo with a face at least 100 pixels wide and high.";
    if (text.find("ALREADY_EXISTS") != std::string::npos) return "This face ID already exists. Choose another ID or edit its entry in Face library.";
    return text;
}
}
DemoUi::DemoUi(Measure measure) : measure_(std::move(measure)) {
    if (!measure_) measure_ = [](const std::string& value, Font font) {
        int width = 0; for (unsigned char c : value) if ((c & 0xc0) != 0x80) width += c < 128 ? (font == Font::Title ? 12 : font == Font::Large ? 10 : font == Font::Small ? 6 : 7) : (font == Font::Title ? 22 : font == Font::Large ? 18 : 14); return width;
    };
}
void DemoUi::text(int x, int y, std::string value, uint32_t color, int max_width, Font font) {
    for (auto& c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
    if (max_width <= 0) return;
    if (measure_(value, font) > max_width) {
        if (measure_("...", font) > max_width) return;
        while (!value.empty() && measure_(value + "...", font) > max_width) {
            size_t end = value.size() - 1; while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end; value.resize(end);
        }
        value += "...";
    }
    scene_.text(x, y, value, color, font, measure_(value, font));
}
int DemoUi::wrap(int x, int y, const std::string& value, uint32_t color, int width, int max_lines, Font font) {
    if (width <= 0 || max_lines <= 0) return 0;
    std::istringstream words(value); std::string word, line; std::vector<std::string> lines;
    while (words >> word) {
        if (!line.empty() && measure_(line + ' ' + word, font) > width) { lines.push_back(std::move(line)); line.clear(); }
        // IDs and paths may have no spaces. Split only at UTF-8 code point boundaries.
        // ID 和路径可能不含空格，只能在 UTF-8 码点边界断行。
        while (measure_(word, font) > width && int(lines.size()) <= max_lines) {
            size_t end = 0, next = 0;
            while (next < word.size()) {
                ++next; while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xc0) == 0x80) ++next;
                if (measure_(word.substr(0, next), font) > width) break;
                end = next;
            }
            if (!end) end = next;
            lines.push_back(word.substr(0, end)); word.erase(0, end);
        }
        if (int(lines.size()) > max_lines) break;
        if (!line.empty()) line += ' '; line += word;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    const bool truncated = int(lines.size()) > max_lines;
    const int step = font == Font::Large || font == Font::Title ? 26 : 19;
    for (int i = 0; i < std::min(max_lines, int(lines.size())); ++i)
        text(x, y + i * step, lines[i] + (truncated && i == max_lines - 1 ? "..." : ""), color, width, font);
    return std::min(max_lines, int(lines.size())) * step;
}
void DemoUi::card(Rect r) {
    scene_.box({r.x, r.y + 2, r.w, r.h}, 0xe9ecf0, 12); scene_.box(r, border, 12); scene_.box({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, surface, 11);
}
void DemoUi::button(Rect r, const std::string& label, const std::string& action, int style, bool enabled, Font font) {
    const bool hover = enabled && r.contains(pointer_x_, pointer_y_);
    const bool primary = style == 1, selected = style == 2, destructive = style == 3, navigation = style == 4;
    uint32_t fill = primary ? accent : selected ? tint : navigation ? 0x204438 : surface;
    uint32_t line = primary || navigation ? fill : selected ? accent : border;
    uint32_t color = primary || navigation ? 0xffffff : destructive ? danger : selected ? accent : ink;
    if (hover) fill = primary ? 0x05624f : navigation ? 0x2b574b : destructive ? 0xffefed : 0xedf5f1;
    if (!enabled) { fill = 0xf0f1f4; line = 0xe5e7ec; color = 0xa0a5af; }
    scene_.box(r, keyboard_focus_ == action ? accent : line, 8); scene_.box({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, fill, 7);
    const int label_width = measure_(label, font);
    text(r.x + std::max(9, (r.w - label_width) / 2), r.y + r.h / 2 + (font == Font::Large ? 6 : 5), label, color, r.w - 18, font);
    const auto bounds = r.intersect(scene_.clip); if (bounds.w > 0 && bounds.h > 0) controls_.push_back({bounds, action, "", enabled});
}
void DemoUi::field(Rect r, const std::string& key, const std::string& label, const std::string& initial, bool enabled) {
    auto& value = values_.emplace(key, initial).first->second;
    text(r.x, r.y + 12, label, muted, r.w, Font::Small);
    const Rect input{r.x, r.y + 21, r.w, 34};
    scene_.box(input, invalid_field_ == key ? danger : focused_ == key ? accent : border, 7);
    scene_.box({input.x + 1, input.y + 1, input.w - 2, input.h - 2}, enabled ? surface : 0xf3f4f6, 6);
    auto shown = value; size_t shown_start = 0;
    if (focused_ == key) {
        auto position = std::min(cursor_, value.size()); shown.insert(position, "|");
        while (!shown.empty() && measure_(shown.substr(0, std::min(position + 1, shown.size())), Font::Body) > input.w - 22) {
            size_t end = 1; while (end < shown.size() && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80) ++end; shown.erase(0, end); shown_start += end; position -= std::min(position, end);
        }
        while (!shown.empty() && measure_(shown, Font::Body) > input.w - 20) {
            size_t end = shown.size() - 1; while (end && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80) --end; shown.resize(end);
        }
        if (select_all_) scene_.box({input.x + 5, input.y + 5, std::min(input.w - 10, measure_(shown, Font::Body) + 6), input.h - 10}, 0xc6e9dc, 3);
    }
    text(input.x + 10, input.y + 22, shown, enabled ? ink : muted, input.w - 20);
    const auto bounds = input.intersect(scene_.clip); if (bounds.w && bounds.h) controls_.push_back({bounds, "", key, enabled, shown_start, label});
}
void DemoUi::toggle(int x, int y, int width, const std::string& key, const std::string& label, bool enabled) {
    const bool on = values_[key] == "1";
    text(x, y + 19, label, enabled ? ink : 0x9da3ad, width - 64);
    const Rect r{x + width - 48, y, 44, 26}; scene_.box(r, enabled ? on ? accent : 0xd5dae1 : 0xe9ecf0, 13);
    scene_.box({r.x + (on ? 22 : 4), r.y + 4, 18, 18}, surface, 9);
    if (keyboard_focus_ == "toggle:" + key) scene_.box({x, y + 28, width, 2}, accent);
    controls_.push_back({{x, y, width, 30}, "toggle:" + key, "", enabled});
}
void DemoUi::empty(Rect r, const std::string& title, const std::string& hint) {
    const bool preview = r.h > 150;
    scene_.box(r, 0x101a29, 8);
    if (preview) {
        scene_.box({r.x + r.w / 2 - 24, r.y + r.h / 2 - 56, 48, 40}, 0x293d51, 10);
        scene_.box({r.x + r.w / 2 - 10, r.y + r.h / 2 - 46, 20, 20}, 0x172638, 10);
    }
    text(r.x + std::max(12, (r.w - measure_(title, Font::Body)) / 2), r.y + r.h / 2 + 12, title, 0xdce5ee, r.w - 24);
    text(r.x + std::max(12, (r.w - measure_(hint, Font::Small)) / 2), r.y + r.h / 2 + 38, hint, 0x90a2b7, r.w - 24, Font::Small);
}
void DemoUi::picture(Rect r, const Image* image, const CameraFrame* camera, bool mirror, const std::vector<Box>& boxes) {
    const int iw = image ? image->width : camera ? camera->width : 0, ih = image ? image->height : camera ? camera->height : 0;
    const auto* data = image ? &image->rgba : camera ? &camera->rgba : nullptr;
    if (!data || data->empty() || iw <= 0 || ih <= 0) { empty(r, "No image yet", "Choose a photo or capture a face"); return; }
    scene_.box(r, 0x101a29, 8);
    const double scale = std::min(double(r.w) / iw, double(r.h) / ih);
    const Rect image_rect = fitImage(r, iw, ih);
    scene_.image(image_rect, data->data(), iw, ih, mirror, camera && camera->x11_bgrx);
    const auto old_clip = scene_.clip; scene_.clip = scene_.clip.intersect(image_rect);
    for (const auto& box : boxes) {
        const bool detection = page_ == "detection";
        const uint32_t box_color = box.search_mark == SearchMark::Pending ? 0xa4b4c9 :
            box.search_mark == SearchMark::Rejected ? 0xff3c3c : box.search_mark == SearchMark::Matched ? 0x28dc64 : technology_green;
        if (!detection) {
        const int x = image_rect.x + int((mirror ? iw - box.x - box.width : box.x) * scale), y = image_rect.y + int(box.y * scale);
        const int bw = std::max(1, int(box.width * scale)), bh = std::max(1, int(box.height * scale));
        const int stroke = box.selected ? 6 : 4;
        scene_.box({x, y, bw, stroke}, box_color);
        scene_.box({x, y + bh - stroke, bw, stroke}, box_color);
        scene_.box({x, y, stroke, bh}, box_color);
        scene_.box({x + bw - stroke, y, stroke, bh}, box_color);
        const int label_width = std::min(image_rect.w, std::max(bw, measure_(box.label, Font::Title) + 20));
        const int label_x = std::clamp(x, image_rect.x, image_rect.x + image_rect.w - label_width);
        const int label_y = std::max(image_rect.y, y - 36);
        scene_.box({label_x, label_y, label_width, 34}, dark, 5);
        text(label_x + 10, label_y + 25, box.label, 0xffffff, label_width - 20, Font::Title);
        } else {
            // Project selected Android contour edges through the same preview/mirror transform.
            for (int pass = 0; pass < 2; ++pass) for (const auto& line : box.contours) {
                const auto project = [&](const HiFacePointV1& point) {
                    return std::pair<int,int>{image_rect.x+int((mirror?iw-point.x:point.x)*scale),image_rect.y+int(point.y*scale)};
                };
                const auto from = project(line[0]), to = project(line[1]);
                scene_.line(from.first,from.second,to.first,to.second,pass ? 0x00bccd : 0x002d34,pass ? 2 : 4);
            }
        }
        const int point_size = detection ? 5 : 7;
        for (const auto& point : box.points) if (point.x >= 0 && point.x < iw && point.y >= 0 && point.y < ih) {
            const int point_x = image_rect.x + int((mirror ? iw - point.x : point.x) * scale);
            const int point_y = image_rect.y + int(point.y * scale);
            scene_.dot(point_x, point_y, point_size, detection ? 0x9c27b0 : box_color);
        }
    }
    scene_.clip = old_clip;
}
void DemoUi::home(const ViewState& s, Rect b) {
    scene_.box(b, background);
    text(28, 42, "HelloFace SDK", ink, b.w - 56, Font::Title);
    wrap(28, 70, "Offline | Private | Fast", muted, b.w - 56, 2);
    text(28, 94, deviceInfo(), muted, b.w - 56, Font::Small);
    const int columns = 1, gap = 11, count = home_page_count;
    const int rows = (count + columns - 1) / columns;
    const int w = (b.w - 56 - (columns - 1) * gap) / columns;
    const int h = 88;
    home_max_scroll_ = std::max(0, 112 + rows * h + (rows - 1) * gap - (b.h - 124));
    home_scroll_ = std::clamp(home_scroll_, 0, home_max_scroll_);
    const auto old_clip = scene_.clip; scene_.clip = {0, 100, b.w, b.h - 216};
    for (int i = 0; i < count; ++i) {
        const auto& feature = demo_pages[i];
        const Rect r{28 + (i % columns) * (w + gap), 112 + (i / columns) * (h + gap) - home_scroll_, w, h};
        button(r, "", std::string("page:") + feature.route, 0, !s.busy && !s.importing);
        text(r.x + 14, r.y + 30, feature.title, accent, w - 28, Font::Large);
        wrap(r.x + 14, r.y + 55, feature.description, muted, w - 28, h > 100 ? 3 : 2, Font::Small);
    }
    scene_.clip = old_clip;
    const std::string sdk_label = "SDK" HIFACE_DEMO_SDK_VERSION;
    const int footer_y = b.h - 32, colon_x = 28 + measure_(sdk_label, Font::Body);
    const int url_x = colon_x + 14 + measure_(" ", Font::Body);
    text(28, footer_y, sdk_label, accent, b.w - 190, Font::Body);
    // Draw U+FF1A in a full-width cell. Minimal Buildroot only has the C
    // locale; Xutf8DrawString otherwise stops here and loses the entire URL.
    scene_.dot(colon_x + 7, footer_y - 8, 2, accent);
    scene_.dot(colon_x + 7, footer_y - 3, 2, accent);
    text(url_x, footer_y, "https://github.com/FaceSDKPro", accent, b.w - 162 - url_x, Font::Body);
    button({b.w - 142, b.h - 54, 114, 34}, "Exit Demo", "quit", 0);
}
void DemoUi::startupFailure(const ViewState& state) {
    scene_.box({0, 0, scene_.width, scene_.height}, background);
    text(28, 42, "HiFace Linux Demo", ink, scene_.width - 56, Font::Title);
    text(28, 70, "Device status", muted, scene_.width - 56);
    const bool clock = state.initialization_status == HIFACE_STATUS_LICENSE_CLOCK_INVALID;
    const bool license = clock || (state.initialization_status >= HIFACE_STATUS_LICENSE_MISSING &&
        state.initialization_status <= HIFACE_STATUS_LICENSE_FEATURE_DENIED);
    const int w = std::min(scene_.width - 56, 660), x = (scene_.width - w) / 2;
    const int y = std::max(100, (scene_.height - 420) / 2);
    card({x, y, w, 420});
    text(x + 24, y + 38, clock ? "Time error: SDK functions unavailable" : "SDK startup failed", danger, w - 48, Font::Title);
    std::string reason = state.status;
    std::string action = "Check the application log and deployment, then restart the Demo.";
    if (clock) {
        reason = "Device time is invalid or a clock anomaly was detected. Check the system clock and try again.";
        action = "Set the correct system time and RTC, then restart the Demo.";
    } else if (license) {
        action = "Update to the latest public SDK build. Contact support if the error remains.";
    }
    wrap(x + 24, y + 78, reason, ink, w - 48, 3);
    text(x + 24, y + 150, "Device time: " + (state.device_time_utc.empty() ? "unavailable" : state.device_time_utc), muted, w - 48);
    text(x + 24, y + 180, "Code: " + std::string(statusName(state.initialization_status)) + " (" + std::to_string(state.initialization_status) + ")", danger, w - 48);
    wrap(x + 24, y + 222, action, muted, w - 48, 5);
    button({x + 24, y + 366, w - 48, 34}, "Exit Demo", "quit", 0);
}
void DemoUi::pageHeader(const ViewState& state, const std::string& title, bool camera_view) {
    const int width = scene_.width;
    if (camera_view && page_ == "camera") {
        // Keep only configuration at the top; search navigation stays below the preview.
        // 顶部仅保留配置入口，搜索页的导航按钮统一放在预览下方。
        button({width - 276, 12, 146, 36}, "Settings", "feature-settings", 4, content_enabled_);
        button({width - 122, 12, 106, 36}, camera_menu_ ? "Close menu" : "Menu", "camera-menu", 4,
               state.ready && !state.importing);
        return;
    }
    button({16, 12, 86, 36}, "< Back", "back", camera_view ? 4 : 0,
           state.ready && (!state.busy || state.photo_search_busy || state.comparison_first_processing || state.comparison_second_processing || state.importing));
    const bool library_link = page_ == "image-search", library_menu = page_ == "gallery";
    const bool switch_camera = camera_view && page_ == "detection";
    text(118, 38, title, camera_view ? 0xffffff : ink, width - 142 - (library_link || library_menu || switch_camera ? 154 : 0), Font::Title);
    if (library_link) button({width - 154, 12, 138, 36}, "Face library", "page:gallery", 0, content_enabled_);
    if (library_menu) button({width-122,12,106,36},"Menu","library-menu",0,state.ready && !state.importing && !state.busy && !metadata_pending_);
    if (switch_camera) button({width - 154, 12, 138, 36}, "Switch camera", "next", 4, content_enabled_);
    if (!camera_view) {
        if (page_ != "gallery") text(24, 80, demoPage(page_)->description, muted, width - 48, Font::Small);
        return;
    }
    if (page_ == "verification") {
        button({16, 60, 204, 34}, "Choose Face ID", "page:references", 4, content_enabled_);
        button({230, 60, 146, 34}, "Settings", "feature-settings", 4, content_enabled_);
        text(392, 82, state.reference.empty() ? "No Face ID selected" : state.reference, 0xc2ceda, width - 408, Font::Small);
    } else if (page_ == "detection" || page_ == "liveness") {
        button({16, 60, 146, 34}, "Settings", "feature-settings", 4, content_enabled_);
        text(180, 82, demoPage(page_)->description, 0xc2ceda, width - 196, Font::Small);
    } else text(16, 82, demoPage(page_)->description, 0xc2ceda, width - 32, Font::Small);
}
void DemoUi::camera(const ViewState& s, Rect b) {
    scene_.box(b, 0x101a29);
    const bool search_page = page_ == "camera";
    // Preserve the two caption rows; halve only the bottom padding (28 -> 14 px).
    // 保留固定两行状态，仅将底部留白减半（28 -> 14 像素）。
    const int bottom_padding = search_page ? 14 : 28;
    const int result_height = 78 + 12 + 46 + bottom_padding;
    Rect preview = b;
    if (search_page) preview.h = std::max(80, b.h - result_height);
    static const std::vector<Box> no_boxes;
    if (!s.busy && s.image_first) picture(b, s.image_first.get(), nullptr, false, s.boxes);
    else if (s.frame && s.connected) picture(preview, nullptr, s.frame.get(), s.settings.mirror, !s.busy && (page_ == "camera" || page_ == "detection") ? s.liveBoxes() : no_boxes);
    else empty(b, "Connect a camera to begin", "Open Menu to choose a video device");

    const bool guided = page_ == "capture" || page_ == "enrollment" || page_ == "verification" || page_ == "liveness";
    if (guided) {
        const int cx = b.w / 2, cy = b.h * 45 / 100;
        const int radius = std::max(40, std::min(b.w * 40 / 100, (b.h - 240) / 2));
        const int top = cy - radius, bottom = cy + radius;
        scene_.box({0,0,b.w,std::max(0,top)},background);
        scene_.box({0,bottom,b.w,std::max(0,b.h-bottom)},background);
        for (int y = top; y < bottom; y += 2) {
            const double dy = y + 1 - cy;
            const int extent = int(std::sqrt(std::max(0.0, double(radius)*radius-dy*dy)));
            scene_.box({0,y,cx-extent,2},background); scene_.box({cx+extent,y,b.w-cx-extent,2},background);
            scene_.box({cx-extent,y,2,2},border); scene_.box({cx+extent-2,y,2,2},border);
        }
        button({16,12,86,36},"< Back","back",0,s.ready && !s.busy);
        text(118,38,demoPage(page_)->title,ink,b.w-260,Font::Title);
        if (page_ == "liveness") button({b.w-150,12,134,36},"Switch camera","next",0,content_enabled_);
        if (page_ == "verification") {
            button({16,60,204,34},"Choose Face ID","page:references",0,content_enabled_);
            text(236,82,s.reference,muted,b.w-412,Font::Small);
            button({b.w-162,60,146,34},"Settings","feature-settings",0,content_enabled_);
        } else if (page_ == "liveness") button({16,60,146,34},"Settings","feature-settings",0,content_enabled_);
        wrap(28,bottom+36,friendly(s.status),s.failed?danger:accent,b.w-56,b.h < 700 ? 2 : 3,Font::Large);
        if (s.action_seconds_remaining > 0) text(28,std::min(bottom+110,b.h-82),std::to_string(s.action_seconds_remaining)+" s remaining",muted,b.w-56,Font::Large);
        if (s.mode == 'D' && !s.pending_enrollment && !s.flow_result_pending)
            button({16,b.h-58,150,40},"Try again","start-task",1,content_enabled_ && s.connected);
        button({b.w-176,b.h-58,160,40},"Camera options","camera-menu",0,s.ready && !s.importing);
    } else {
    // Camera feature pages share the kiosk renderer: the camera consumes the complete
    // portrait viewport and diagnostics stay in compact overlays.
    // 各相机功能页共用全屏竖向渲染器，诊断信息放在紧凑叠加层中。
    scene_.box({b.x, b.y, b.w, search_page ? 60 : 160}, dark);
    pageHeader(s, demoPage(page_)->title, true);
    const int metric_width = std::max(70, (b.w - 32) / 4);
    const std::string metrics[] = {number(s.fps) + " FPS", number(s.analysis_fps) + " FPS",
        number(s.milliseconds) + " ms", std::to_string(page_ == "detection" ? s.liveBoxes().size() : s.count)};
    const char* metric_names[] = {"Preview", "Recognition", "Last analysis", page_ == "detection" ? "Faces" : "Library"};
    for (int i = 0; !search_page && i < 4; ++i) {
        const int x = b.x + 16 + i * metric_width;
        text(x, b.y + 124, metrics[i], i == 3 ? technology_green : 0xffffff, metric_width - 8, Font::Large);
        text(x, b.y + 147, metric_names[i], 0xc2ceda, metric_width - 8, Font::Body);
    }
    if (page_ == "capture" && s.aligned_preview && !s.busy) {
        const Rect preview{b.x + b.w - 190, b.y + b.h - 354, 168, 168};
        scene_.box(preview, dark, 8); picture(preview, s.aligned_preview.get(), nullptr, false, {});
        text(preview.x, preview.y - 8, "Aligned capture", 0xffffff, preview.w, Font::Small);
    }

    const int controls_y = b.y + b.h - bottom_padding - 46;
    if (search_page) searchResults(s, {b.x, b.y + b.h - result_height, b.w, 78});
    else {
        scene_.box({b.x, b.y + b.h - 164, b.w, 164}, dark);
        text(b.x + 18, b.y + b.h - 137, modeName(s.mode), technology_green, b.w - 36, Font::Large);
        wrap(b.x + 18, b.y + b.h - 108, friendly(s.status), 0xffffff, b.w - 36, 2, Font::Body);
    }
    if (search_page) {
        scene_.box({b.x, controls_y - 10, b.w, 46 + bottom_padding + 10}, dark);
        const bool reconnect = !s.connected || s.camera_stalled;
        const bool start_search = reconnect || s.mode != 'S';
        const int count = start_search ? 5 : 4, gap = 6;
        const int action_width = (b.w - 24 - gap * (count - 1)) / count;
        const Font font = start_search ? Font::Body : Font::Large;
        int x = b.x + 12;
        auto action = [&](const std::string& label, const std::string& id, bool enabled, int style = 4) {
            button({x, controls_y, action_width, 46}, label, id, style, enabled, font);
            x += action_width + gap;
        };
        action("< Back", "back", s.ready && (!s.busy || s.importing));
        action("Enroll", "page:enrollment", content_enabled_);
        action("Face library", "page:gallery", content_enabled_);
        action(s.settings.mirror ? "Mirror on" : "Mirror off", "mirror", content_enabled_, s.settings.mirror ? 2 : 4);
        if (start_search) action(reconnect ? "Reconnect" : s.mode == 'P' ? "Resume" : "Start search", "start-task", content_enabled_);
    } else {
        const int action_width = (b.w - 48) / 3;
        button({b.x + 12, controls_y, action_width, 46}, "Try again", "start-task", 4,
            content_enabled_ && s.connected && s.mode == 'D' && !s.pending_enrollment && !s.flow_result_pending &&
            (selected_task_ != "V" || !s.reference.empty()), Font::Large);
        button({b.x + 24 + action_width, controls_y, action_width, 46}, s.settings.mirror ? "Mirror on" : "Mirror off", "mirror",
            s.settings.mirror ? 2 : 4, content_enabled_, Font::Large);
        button({b.x + 36 + action_width * 2, controls_y, action_width, 46}, camera_menu_ ? "Close menu" : "Menu", "camera-menu", 4,
            s.ready && !s.importing, Font::Large);
    }
    if (!search_page && !s.lighting.empty()) text(b.x + 18, b.y + b.h - 8, s.lighting, 0xc2ceda, b.w - 36, Font::Small);

    }
    if (!camera_menu_) return;
    const int menu_h = (page_ == "detection" || page_ == "capture" || page_ == "enrollment") ? 292 : 248;
    const int menu_y = search_page ? b.y + 60 : b.y + b.h - 86 - menu_h;
    scene_.box({b.x + 12, menu_y, b.w - 24, menu_h}, 0x253342, 14);
    text(b.x + 32, menu_y + 32, "Camera options", 0xffffff, b.w - 64, Font::Title);
    const int x = b.x + 32, w = b.w - 64;
    field({x, menu_y + 48, w - 224, 56}, "device", "Camera device", s.device, !s.busy && s.fixed_camera_device.empty());
    button({x + w - 212, menu_y + 69, 96, 34}, "Connect", "connect", 1, content_enabled_);
    button({x + w - 106, menu_y + 69, 106, 34}, "Next", "next", 0, content_enabled_ && s.fixed_camera_device.empty());
    text(x, menu_y + 126, s.camera, 0x9badbf, w, Font::Small);
    button({x, menu_y + 148, w, 34}, "Stop this workflow", "mode:X", 0, content_enabled_ && s.mode != 'D');
    if (page_ == "verification") {
        text(x, menu_y + 212, s.reference.empty() ? "Select a Face ID using Choose reference above." : "Reference: " + s.reference,
             0xc2ceda, w - 96, Font::Small);
        button({x + w - 86, menu_y + 192, 86, 32}, "Clear", "clear-reference", 0, content_enabled_ && !s.reference.empty());
    }
    if (page_ == "detection" || page_ == "capture" || page_ == "enrollment")
        button({x, menu_y + 206, w, 34}, page_ == "detection" ? "Detect a photo" : page_ == "capture" ? "Choose a photo reference" : "Capture a photo for enrollment", "browse:feature-photo", 0, content_enabled_);

}
void DemoUi::photos(const ViewState& s, Rect b) {
    const bool picker_enabled = s.ready && !s.importing;
    const int width = (b.w - 16) / 2;
    const int score_height = s.comparison_similarity ? 60 : 0;
    const int card_height = std::min(width + 82, b.h - 64 - score_height);
    const int side = std::max(40, std::min(width - 24, card_height - 102));
    for (int slot = 0; slot < 2; ++slot) {
        const int x = b.x + slot * (width + 16);
        card({x, b.y, width, card_height});
        text(x + 16, b.y + 28, slot ? "Photo 2" : "Photo 1", ink, width - 32);
        const Rect preview{x + (width - side) / 2, b.y + 48, side, side};
        const auto* image = slot ? s.image_second.get() : s.image_first.get();
        picture(preview, image, nullptr, false, {});
        const bool processing = slot ? s.comparison_second_processing : s.comparison_first_processing;
        const auto& error = slot ? s.comparison_second_error : s.comparison_first_error;
        wrap(x + 16, b.y + card_height - 34, processing ? "Processing photo..." : !error.empty() ? friendly(error) : image ? "Click to change photo" : "Click to choose photo",
             error.empty() ? muted : danger, width - 32, 2, Font::Small);
        controls_.push_back({preview, slot ? "browse:photo-b" : "browse:photo-a", "", picker_enabled});
    }
    if (s.comparison_similarity) {
        const std::string result = "Similarity " + number(*s.comparison_similarity * 100, 2) + "%";
        text(b.x + std::max(0, (b.w - measure_(result, Font::Large)) / 2), b.y + card_height + 36, result, accent, b.w, Font::Large);
    }
    button({b.x, b.y + b.h - 46, b.w, 46}, "Compare", "image:compare", 1,
           content_enabled_ && s.comparison_first_ready && s.comparison_second_ready);
}
void DemoUi::imageSearch(const ViewState& s, Rect b) {
    if (image_search_generation_ != s.photo_search_generation) { image_search_generation_ = s.photo_search_generation; selected_image_face_ = image_face_offset_ = 0; }
    const bool picker_enabled = s.ready && !s.importing && (!s.busy || s.photo_search_busy);
    card({b.x, b.y, b.w, 116});
    field({b.x + 16, b.y + 14, b.w - 326, 56}, "search-path", "Photo to search (Enter to search)", "", picker_enabled);
    button({b.x + b.w - 298, b.y + 35, 78, 34}, "Browse", "browse:search-path", values_["search-path"].empty() ? 1 : 0, picker_enabled);
    button({b.x + b.w - 210, b.y + 35, 98, 34}, "Search", "image-search", 1, picker_enabled && !values_["search-path"].empty());
    button({b.x + b.w - 102, b.y + 35, 86, 34}, "Cancel", "cancel-image-search", 0, s.photo_search_busy);
    text(b.x + 16, b.y + 96, "All groups | Top-1 " + number(HIFACE_DEFAULT_VERIFICATION_THRESHOLD, 2) + " | up to 10 faces | no liveness", muted, b.w - 188, Font::Small);
    if (b.w < 720 && scene_.height > scene_.width) {
        const int count = s.photo_search ? int(s.photo_search->faces.size()) : 0;
        const int top = b.y + 132, height = b.h - 132, strip = count ? 182 : 0;
        const Rect photo_card{b.x,top,b.w,height-strip}; card(photo_card);
        text(b.x+16,top+28,"Selected photo",ink,b.w-32);
        const Rect preview{b.x+12,top+44,b.w-24,photo_card.h-56};
        selected_image_face_ = std::clamp(selected_image_face_,0,std::max(0,count-1));
        std::vector<Box> boxes;
        if (s.search_photo) {
            for (int i=0;i<count;++i) {
                const auto& face = s.photo_search->faces[i];
                boxes.push_back({face.x,face.y,face.width,face.height,face.label,face.matched(),{},i==selected_image_face_});
            }
            picture(preview,s.search_photo.get(),nullptr,false,boxes);
        } else empty(preview,s.photo_search_busy?"Searching photo...":"Select a photo","Click here or Browse to choose a photo.");
        controls_.push_back({preview,count?"image-photo":"browse:search-path","",picker_enabled});
        if (count) {
            image_face_rows_ = std::max(1,(b.w-54)/122);
            image_face_offset_ = selected_image_face_/image_face_rows_*image_face_rows_;
            const int row_y=top+height-strip+14;
            image_result_area_={b.x,row_y,b.w,strip};
            for(int i=image_face_offset_;i<std::min(count,image_face_offset_+image_face_rows_);++i) {
                const auto& face=s.photo_search->faces[i];const Rect row{b.x+38+(i-image_face_offset_)*122,row_y,112,142};
                card(row);if(i==selected_image_face_)scene_.box({row.x+2,row.y+2,row.w-4,3},accent);
                if(!face.preview.rgba.empty())picture({row.x+20,row.y+8,72,72},&face.preview,nullptr,false,{});
                else {scene_.box({row.x+20,row.y+8,72,72},0xe9edf2,8);text(row.x+50,row.y+52,"?",muted,24,Font::Large);}
                wrap(row.x+6,row.y+100,face.label,face.matched()?accent:muted,row.w-12,2,Font::Small);
                controls_.push_back({row,"image-face:"+std::to_string(i),"",content_enabled_});
            }
            button({b.x,row_y+48,30,34},"<","image-faces-prev:"+std::to_string(image_face_rows_),0,content_enabled_&&image_face_offset_>0);
            button({b.x+b.w-30,row_y+48,30,34},">","image-faces-next:"+std::to_string(image_face_rows_),0,content_enabled_&&image_face_offset_+image_face_rows_<count);
        }
        return;
    }
    const int y = b.y + 132, h = b.h - 132, side = 260, w = b.w - side - 16, sx = b.x + w + 16;
    const int count = s.photo_search ? int(s.photo_search->faces.size()) : 0;
    image_face_rows_ = std::max(2, std::min(10, (h - 94) / 76));
    image_result_area_ = {sx, y + 48, side, h - 48};
    selected_image_face_ = std::clamp(selected_image_face_, 0, std::max(0, count - 1));
    image_face_offset_ = (selected_image_face_ / image_face_rows_) * image_face_rows_;
    card({b.x, y, w, h}); text(b.x + 16, y + 28, "Selected photo", ink, w - 138);
    if (s.search_photo) text(b.x + w - 116, y + 28, std::to_string(s.search_photo->width) + " x " + std::to_string(s.search_photo->height), muted, 100, Font::Small);
    const Rect preview{b.x + 12, y + 48, w - 24, h - (count ? 156 : 60)};
    std::vector<Box> boxes;
    if (count) for (int i = 0; i < count; ++i) {
        const auto& f = s.photo_search->faces[i];
        boxes.push_back({f.x, f.y, f.width, f.height, "#" + std::to_string(i+1), f.matched(), {}, i == selected_image_face_});
    }
    if (s.search_photo) picture(preview, s.search_photo.get(), nullptr, false, boxes);
    else empty(preview, s.photo_search_busy ? "Searching photo..." : "Choose a photo to search", "Browse to select a local photo. No camera needed.");
    controls_.push_back({preview, "image-photo", "", picker_enabled});
    if (count) {
        const auto& selected = s.photo_search->faces[selected_image_face_];
        const bool technical_error = (selected.capture_status != HIFACE_STATUS_OK && selected.capture_status != HIFACE_STATUS_IMAGE_FACE_TOO_SMALL) || selected.search_status != HIFACE_STATUS_OK;
        const uint32_t tone = selected.matched() ? accent : technical_error ? danger : 0x9b6b16;
        const int top = y + h - 92;
        scene_.box({b.x+16, top-5, w-32, 1}, border);
        const std::string selected_status = selected.matched() ? "Matched" : technical_error ? "Error" : selected.capture_status == HIFACE_STATUS_IMAGE_FACE_TOO_SMALL ? "Small face" : "No match";
        text(b.x+16, top+14, "Face " + std::to_string(selected_image_face_+1) + " of " + std::to_string(count) + "  /  " + selected_status, tone, w-32, Font::Small);
        wrap(b.x+16, top+38, selected.label, ink, w-32, 2);
        const auto metadata = selected.matched() ? "Group: " + (selected.matches.front().group.empty() ? "-" : selected.matches.front().group) + "   Tag: " + (selected.matches.front().tag.empty() ? "-" : selected.matches.front().tag) : "Click a thumbnail or use arrow keys to select a face.";
        text(b.x+16, top+77, metadata, muted, w-32, Font::Small);
    }
    card({sx, y, side, h});
    text(sx + 16, y + 28, "Faces: " + std::to_string(count), ink, 106);
    text(sx + 128, y + 28, "Matched: " + std::to_string(s.photo_search ? s.photo_search->matched : 0), accent, side - 144, Font::Small);
    if (!count) {
        wrap(sx + 16, y + 75, s.photo_search_busy ? "Detecting faces and searching the library. You can choose another photo or cancel." : s.photo_search ? "No faces found. Try a clear, upright photo." : "Choose one photo to search every detected face.", muted, side - 32, 5);
        if (s.photo_search && s.photo_search->library_count == 0) wrap(sx + 16, y + 186, "Your library is empty. Open Face library to enroll or import features.", 0x9b6b16, side - 32, 4, Font::Small);
        return;
    }
    for (int i = image_face_offset_; i < std::min(count, image_face_offset_ + image_face_rows_); ++i) {
        const auto& f = s.photo_search->faces[i]; const Rect row{sx + 12, y + 48 + (i-image_face_offset_) * 76, side - 24, 70};
        const bool selected = i == selected_image_face_;
        const bool failed = (f.capture_status != HIFACE_STATUS_OK && f.capture_status != HIFACE_STATUS_IMAGE_FACE_TOO_SMALL) || f.search_status != HIFACE_STATUS_OK;
        const uint32_t tone = f.matched() ? accent : failed ? danger : 0x9b6b16;
        scene_.box(row, selected || keyboard_focus_ == "image-face:" + std::to_string(i) ? accent : border, 8);
        scene_.box({row.x+1, row.y+1, row.w-2, row.h-2}, selected ? tint : row.contains(pointer_x_,pointer_y_) ? 0xf2f7f5 : surface, 7);
        if (!f.preview.rgba.empty()) picture({row.x+7, row.y+7, 56, 56}, &f.preview, nullptr, false, {});
        else { scene_.box({row.x+7, row.y+7, 56, 56}, 0xe9edf2, 6); text(row.x+29, row.y+42, "?", muted, 22); }
        text(row.x+73, row.y+20, "Face " + std::to_string(i+1), muted, 64, Font::Small);
        scene_.box({row.x+row.w-82,row.y+7,74,18}, f.matched() ? tint : failed ? 0xffeeeb : 0xfff4dc, 4);
        text(row.x+row.w-77,row.y+20,f.matched() ? "Matched" : failed ? "Error" : f.capture_status==HIFACE_STATUS_IMAGE_FACE_TOO_SMALL ? "Small face" : "No match",tone,64,Font::Small);
        wrap(row.x+73, row.y+40, f.label, tone, row.w-83, 2, Font::Small);
        controls_.push_back({row, "image-face:" + std::to_string(i), "", content_enabled_});
    }
    button({sx+12, y+h-36, 76, 26}, "Previous", "image-faces-prev:" + std::to_string(image_face_rows_), 0, content_enabled_ && image_face_offset_ > 0);
    text(sx+98, y+h-18, std::to_string(image_face_offset_+1) + "-" + std::to_string(std::min(count,image_face_offset_+image_face_rows_)) + " / " + std::to_string(count), muted, 68, Font::Small);
    button({sx+side-80, y+h-36, 68, 26}, "Next", "image-faces-next:" + std::to_string(image_face_rows_), 0, content_enabled_ && image_face_offset_+image_face_rows_ < count);
}
void DemoUi::library(const ViewState& s, Rect b) {
    if (s.gallery_revision != library_revision_) { library_revision_=s.gallery_revision;library_scroll_=0;library_more_pending_=library_clear_pending_=false; }
    if (library_more_pending_ && s.gallery_attempt > library_attempt_ && !s.busy) library_more_pending_=false;
    if (s.gallery_query_id.empty()) library_clear_pending_=false;
    if (library_menu_) {
        card({b.x+b.w-340,b.y,340,210});
        const int x=b.x+b.w-324;
        button({x,b.y+16,308,36},"Refresh","library-refresh",0,content_enabled_);
        button({x,b.y+62,308,36},"Import synthetic features (10k)","import-demo",0,content_enabled_ && !s.demo_features.empty());
        button({x,b.y+108,308,36},"Import local image folder","library-import-images",0,content_enabled_);
        button({x,b.y+154,308,36},"Import / export HF1 files","library:transfer",0,content_enabled_);
        return;
    }
    if (library_tab_ == "transfer") {
        card({b.x, b.y, b.w, b.h}); text(b.x + 20, b.y + 32, "Move your face library", ink, b.w - 40);
        wrap(b.x + 20, b.y + 57, "Import Android-compatible HF1 JSON or a local image folder. Export keeps the original HF1 features.", muted, b.w - 40, 2, Font::Small);
        field({b.x + 20, b.y + 94, b.w - 132, 56}, "exchange-path", "JSON file or image directory", s.data_directory + "/features.json", !s.importing && !s.busy);
        button({b.x + b.w - 96, b.y + 115, 76, 34}, "Browse", "browse:exchange-path", 0, content_enabled_);
        const bool path = !values_["exchange-path"].empty(); const int bw = (b.w - 56) / 3;
        button({b.x + 20, b.y + 172, bw, 36}, "Import HF1 JSON", "import-features", 1, content_enabled_ && path);
        button({b.x + 28 + bw, b.y + 172, bw, 36}, "Import image folder", "import-images", 0, content_enabled_ && path);
        button({b.x + 36 + bw * 2, b.y + 172, bw, 36}, "Export all JSON", "export", 0, content_enabled_ && path);
        const int py = b.y + 240; text(b.x + 20, py, s.importing ? "Import in progress" : "Import progress", ink, b.w - 40);
        const double fraction = s.import_total ? std::min(1.0, double(s.import_processed) / s.import_total) : 0;
        scene_.box({b.x + 20, py + 18, b.w - 40, 8}, 0xe9eef1, 4); if (fraction > 0) scene_.box({b.x + 20, py + 18, int((b.w - 40) * fraction), 8}, accent, 4);
        text(b.x + 20, py + 56, std::to_string(s.import_processed) + " / " + std::to_string(s.import_total) + " processed   |   Saved " + std::to_string(s.import_saved) + "   Skipped " + std::to_string(s.import_failed), muted, b.w - 40, Font::Small);
        button({b.x + 20, py + 76, 154, 34}, "Stop import", "cancel-import", 3, s.importing && !s.busy);
        if (!s.demo_features.empty()) button({b.x + 190, py + 76, 210, 34}, "Synthetic sample (10k)", "import-demo", 0, content_enabled_);
        if (b.h > 408) wrap(b.x + 20, b.y + b.h - 42, "Imports add or replace matching IDs; stopping preserves completed batches. Export writes the complete library, even during an ID lookup.", muted, b.w - 40, 2, Font::Small);
        return;
    }

    card({b.x,b.y,b.w,78});
    field({b.x+16,b.y+8,b.w-208,56},"query-id","Exact Face ID","",content_enabled_);
    button({b.x+b.w-184,b.y+29,72,34},"Clear","gallery-clear",0,content_enabled_ && !values_["query-id"].empty());
    button({b.x+b.w-104,b.y+29,88,34},"Find","gallery-load",1,content_enabled_);
    std::string summary=s.gallery_query_id.empty()?"Total: "+std::to_string(s.count)+" faces":"Exact ID lookup";
    if(!s.importing && s.status.rfind("Import ",0)==0)summary+=" | "+s.status;
    else if(s.status.rfind("Metadata updated:",0)==0)summary+=" | Metadata saved";
    else if(s.status.rfind("Deleted ",0)==0)summary+=" | "+s.status;
    text(b.x+12,b.y+101,s.busy ? "Loading faces..." : s.failed ? friendly(s.status) : summary,s.failed?danger:muted,b.w-24,Font::Small);
    library_area_={b.x,b.y+118,b.w,b.h-118};
    if (s.importing) {
        card({b.x,library_area_.y,b.w,112});
        text(b.x+16,library_area_.y+26,"Import in progress",ink,b.w-180);
        text(b.x+16,library_area_.y+52,std::to_string(s.import_processed)+" / "+std::to_string(s.import_total)+" processed; saved "+std::to_string(s.import_saved)+", skipped "+std::to_string(s.import_failed),muted,b.w-32,Font::Small);
        const int progress=s.import_total?int(double(b.w-32)*std::min(1.0,double(s.import_processed)/s.import_total)):0;
        scene_.box({b.x+16,library_area_.y+70,b.w-32,8},border,4);if(progress)scene_.box({b.x+16,library_area_.y+70,progress,8},accent,4);
        button({b.x+b.w-160,library_area_.y+14,144,34},"Stop import","cancel-import",3,!s.busy);
        library_area_.y+=124;library_area_.h-=124;
    }
    gallery_rows_=std::max(1,(library_area_.h-38)/98);
    const int loaded=int(s.gallery.size()), max_scroll=std::max(0,loaded-gallery_rows_);
    library_scroll_=std::clamp(library_scroll_,0,max_scroll);
    if (!loaded) {
        const int y=library_area_.y+std::max(28,(library_area_.h-110)/2);
        text(b.x+24,y,s.busy?"Loading faces...":s.failed?"Unable to load the library":!s.gallery_query_id.empty()?"This ID was not found":"No faces yet",s.failed?danger:ink,b.w-48,Font::Large);
        wrap(b.x+24,y+38,s.failed?"Open Menu and Refresh to try again.":!s.gallery_query_id.empty()?"Check the full ID, or clear the search to see all faces.":"Import demo data from the top-right Menu, or enroll a face from Face Search.",muted,b.w-48,3);
    }
    for(int i=library_scroll_;i<std::min(loaded,library_scroll_+gallery_rows_);++i) {
        const auto& person=s.gallery[i];const Rect row{b.x,library_area_.y+(i-library_scroll_)*98,b.w,88};card(row);
        scene_.box({row.x+14,row.y+23,40,40},tint,20);
        scene_.box({row.x+28,row.y+31,12,12},accent,6);scene_.box({row.x+23,row.y+46,22,12},accent,6);
        wrap(row.x+70,row.y+26,person.id,ink,row.w-248,2);
        const std::string metadata="Group: "+(person.group.empty()?"Ungrouped":person.group)+(person.tag.empty()?"":"  |  Tag: "+person.tag);
        text(row.x+70,row.y+72,metadata,muted,row.w-248,Font::Small);
        button({row.x+row.w-170,row.y+24,72,40},"Edit","gallery-edit:"+person.id,0,content_enabled_);
        button({row.x+row.w-90,row.y+24,78,40},"Delete","gallery-delete:"+person.id,3,content_enabled_);
    }
    if (loaded) text(b.x+12,library_area_.y+library_area_.h-12,std::to_string(loaded)+" loaded  |  Scroll or drag to browse",muted,b.w-174,Font::Small);
    if (s.gallery_has_more || library_more_pending_) button({b.x+b.w-150,library_area_.y+library_area_.h-34,150,30},library_more_pending_?"Loading...":"Load more","gallery-more",0,content_enabled_ && !library_more_pending_);
    if (max_scroll) {
        const int track=std::max(1,library_area_.h-40),thumb=std::max(24,track*gallery_rows_/loaded);
        scene_.box({b.x+b.w-4,library_area_.y+(track-thumb)*library_scroll_/max_scroll,3,thumb},0xb9c4c1,2);
    }
    if (!s.gallery_query_id.empty() && trimText(values_["query-id"]).empty() && !s.busy && !s.importing && !library_clear_pending_ && !metadata_dialog_ && !confirming_ && !browsing_) {
        library_clear_pending_=true;emit("gallery",{"0","","",""});
    } else if (loaded && library_scroll_+gallery_rows_>=loaded-10 && !s.failed) loadMoreGallery(s);
}
void DemoUi::references(const ViewState& s, Rect b) {
    card(b);
    if (s.captured_faces.empty()) {
        text(b.x + 24, b.y + 42, "Capture a face first", ink, b.w - 48, Font::Large);
        wrap(b.x + 24, b.y + 76, "Open Face Capture from Home and save a Face ID. Verification uses these captured references; the search library is separate.", muted, b.w - 48, 5);
        return;
    }
    const int rows = std::max(1, std::min(10, (b.h - 96) / 48));
    reference_offset_ = std::clamp(reference_offset_, 0, std::max(0, int(s.captured_faces.size()) - 1));
    for (int i = reference_offset_; i < std::min(int(s.captured_faces.size()), reference_offset_ + rows); ++i)
        button({b.x + 20, b.y + 20 + (i - reference_offset_) * 48, b.w - 40, 40}, s.captured_faces[i].id, "reference-pick:" + std::to_string(i), 0, content_enabled_);
    button({b.x + 20, b.y + b.h - 52, 100, 32}, "Previous", "references-prev:" + std::to_string(rows), 0, reference_offset_ > 0);
    button({b.x + b.w - 120, b.y + b.h - 52, 100, 32}, "Next", "references-next:" + std::to_string(rows), 0, reference_offset_ + rows < int(s.captured_faces.size()));
}
void DemoUi::resetDraft(const DemoSettings& settings) { for (const auto& f : settingsFields(settings)) values_["setting:" + f.key] = f.value; invalid_field_.clear(); }
bool DemoUi::settingVisible(const std::string& key) const {
    if (page_ == "camera-settings") return sdkSetting(key);
    if (settings_tab_ == "detection") return key == "detection_faces" || key == "detailed";
    if (settings_tab_ == "search") return key.rfind("search_", 0) == 0 || key == "group" || key == "tag";
    return settings_tab_ == "verification" ? key.rfind("verification_", 0) == 0
        : key == "actions" || key == "allow_multiple" || key == "random_actions";
}
bool DemoUi::settingsDirty(const ViewState& state) const { for (const auto& f : settingsFields(state.settings)) { const auto it = values_.find("setting:" + f.key); if (settingVisible(f.key) && it != values_.end() && it->second != f.value) return true; } return false; }
void DemoUi::settings(const ViewState& s, Rect b) {
    const std::string tab = page_ == "camera-settings" ? "camera" : settings_tab_;
    const Rect r{b.x, b.y, b.w, b.h - 62}; card(r); const int x = r.x + 20, y = r.y + 22, w = r.w - 40, half = (w - 20) / 2;
    auto f = [&](const char* key, const char* label, int px, int py, int width, bool enabled = true) { field({px, py, width, 56}, std::string("setting:") + key, label, "", !s.busy && enabled); };
    auto t = [&](const char* key, const char* label, int py, bool enabled = true) { toggle(x, py, w, std::string("setting:") + key, label, !s.busy && enabled); };
    if (tab == "detection") {
        text(x, y + 12, "Face detection", ink, w); wrap(x, y + 39, "Show five landmarks and selected eye, nose, lip and face contours.", muted, w, 2, Font::Small);
        f("detection_faces", "Maximum faces (1..10)", x, y + 78, half); t("detailed", "Key facial contours", y + 170);
        wrap(x, y + 224, "Detailed analysis uses more CPU. Capture and liveness automatically prepare the capabilities they need.", muted, w, 3, Font::Small);
    } else if (tab == "flow" || tab == "verification") {
        const bool verification = tab == "verification";
        const std::string action_key = verification ? "verification_actions" : "actions";
        const std::string random_key = verification ? "verification_random_actions" : "random_actions";
        text(x, y + 12, tab == "verification" ? "Actions and verification" : "Liveness actions", ink, w); text(x, y + 39, "No actions = silent liveness. Up to four steps; repeats are allowed.", muted, w, Font::Small);
        const int aw = (w - 24) / 4; const char* actions[] = {"blink", "nod", "shake", "mouth"}; const char* names[] = {"Blink", "Nod head", "Shake head", "Open mouth"};
        const auto& sequence = values_["setting:" + action_key]; const int count = sequence.empty() ? 0 : 1 + std::count(sequence.begin(), sequence.end(), ',');
        for (int i = 0; i < 4; ++i) button({x + i * (aw + 8), y + 58, aw, 32}, names[i], std::string("add-action:") + actions[i], 0, !s.busy && count < 4);
        text(x, y + 120, values_["setting:" + random_key] == "1" ? (verification ? "Two distinct random actions per retry" : "Three distinct random actions per retry") : sequence.empty() ? "Sequence: silent liveness" : "Sequence: " + sequence, sequence.empty() ? muted : accent, w - 144);
        button({x + w - 130, y + 101, 60, 30}, "Undo", "actions-undo", 0, !s.busy && count > 0); button({x + w - 62, y + 101, 62, 30}, "Clear", "actions-clear", 0, !s.busy && count > 0);
        if (verification) {
            toggle(x, y + 156, half, "setting:verification_random_actions", "Random actions (2)", !s.busy);
            toggle(x + half + 20, y + 156, half, "setting:verification_allow_multiple", "Allow extra faces", !s.busy);
            toggle(x, y + 218, half, "setting:verification_liveness", "Silent liveness", !s.busy);
            f("verification_threshold", "Verification threshold (0..1)", x + half + 20, y + 202, half);
        } else {
            t("random_actions", "Random actions (3)", y + 156);
            t("allow_multiple", "Continue with the largest face", y + 202);
        }
    } else if (tab == "search") {
        const bool multi = values_["setting:search_multi"] == "1";
        button({x, y, half, 34}, "1:N Single face", "search-single", multi ? 0 : 2, !s.busy); button({x + half + 20, y, half, 34}, "M:N Multiple faces", "search-multi", multi ? 2 : 0, !s.busy);
        t("search_liveness", multi ? "Liveness is unavailable in M:N" : "Silent liveness during search", y + 50, !multi);
        f("search_threshold", "Threshold (0.75..0.85)", x, y + 96, multi ? half : w);
        if (multi) f("search_faces", "M:N faces (1..5)", x + half + 20, y + 96, half);
        const bool compact = b.h < 450;
        f("group", "Group (empty = all)", x, y + (compact?156:172), half); f("tag", "Tag (empty = all)", x + half + 20, y + (compact?156:172), half);
        button({x,y+(compact?220:248),230,32},search_advanced_ ? "Hide advanced settings" : "Advanced search settings","search-advanced",0,!s.busy);
        if (search_advanced_) {
            f("search_interval", "Success interval ms (0..3000)", x, y + (compact?254:296), half);
            f("search_timeout", "Round timeout ms (1000..3000)", x + half + 20, y + (compact?254:296), half);
        }
    } else {
        text(x, y + 12, sdk_advanced_ ? "Advanced video settings" : "Camera resolution", ink, w);
        wrap(x, y + 35, sdk_advanced_ ? "Video formats and exact dimensions depend on the Linux camera driver." : "Changes save automatically. UI rotation turns the camera and face overlays together.", muted, w, 2, Font::Small);
        const int preset = (w - 16) / 3;
        if (sdk_advanced_) {
            for (int i = 0; i < 3; ++i) button({x + i * (preset + 8), y + 70, preset, 30}, i == 0 ? "640 x 360" : i == 1 ? "1280 x 720" : "1920 x 1080", "resolution:" + std::to_string(i), 0, !s.busy);
            const int third = (w - 24) / 3; f("camera_width", "Width (even)", x, y + 112, third); f("camera_height", "Height (even)", x + third + 12, y + 112, third); f("camera_fps", "FPS (1..60)", x + third * 2 + 24, y + 112, third);
            t("camera_mjpeg", "Prefer MJPEG format", y + 180);
        } else {
            button({x,y+70,half,38},"Default 640 x 360","resolution:0",values_["setting:camera_width"]=="640" && values_["setting:camera_height"]=="360"?2:0,!s.busy);
            button({x+half+20,y+70,half,38},"High 1280 x 720","resolution:1",values_["setting:camera_width"]=="1280" && values_["setting:camera_height"]=="720"?2:0,!s.busy);
            t("mirror", "Mirror preview and face overlays", y + 142);
            text(x, y + 214, "UI rotation", ink, 96, Font::Small);
            const int rw = (w - 108 - 24) / 4;
            for (int i = 0; i < 4; ++i) {
                const auto angle = std::to_string(i * 90);
                button({x + 108 + i * (rw + 8), y + 194, rw, 30}, angle + "°", "ui-rotation:" + angle,
                       values_["setting:ui_rotation"] == angle ? 2 : 0, !s.busy, rw < 60 ? Font::Small : Font::Body);
            }
        }
        button({x,y+258,220,32},sdk_advanced_ ? "Back to SDK settings" : "Advanced video settings","sdk-advanced",0,!s.busy);
    }
    button({b.x, b.y + b.h - 44, 170, 38}, settings_pending_ ? "Saving..." : tab == "camera" ? "Apply typed values" : "Apply settings", "settings-apply", 1, content_enabled_ && !settings_pending_ && (tab != "camera" || settingsDirty(s)));
    button({b.x + 182, b.y + b.h - 44, 154, 38}, "Reset draft", "settings-defaults", 0, !s.busy);
    text(b.x + 354, b.y + b.h - 20, settingsDirty(s) ? "Unsaved changes" : "Settings are saved", settingsDirty(s) ? 0x9b6b16 : muted, b.w - 354, Font::Small);
}
void DemoUi::emit(std::string type, std::vector<std::string> values) { commands_.push_back({std::move(type), std::move(values), {}}); notice_.clear(); invalid_field_.clear(); }
std::vector<Command> DemoUi::takeCommands() { std::vector<Command> commands; commands.swap(commands_); return commands; }
void DemoUi::beginOverlay() {
    soft_keyboard_open_ = false;
    library_drag_active_ = false;
    overlay_return_focus_ = keyboard_focus_; focused_.clear(); select_all_ = false; notice_.clear(); invalid_field_.clear();
}
void DemoUi::dismissOverlay() {
    soft_keyboard_open_ = false;
    confirming_ = browsing_ = enrolling_ = result_dialog_ = metadata_dialog_ = false; notice_.clear(); invalid_field_.clear(); browser_error_.clear(); select_all_ = false;
    keyboard_focus_ = overlay_return_focus_; focused_ = keyboard_focus_.rfind("field:", 0) == 0 ? keyboard_focus_.substr(6) : ""; cursor_ = values_[focused_].size();
}
void DemoUi::confirm(Command pending, const std::string& title, const std::string& message) {
    beginOverlay(); pending_ = std::move(pending); confirm_title_ = title; confirm_message_ = message; confirming_ = true; keyboard_focus_ = "cancel";
}
void DemoUi::selectImageFace(int index, const ViewState& state) {
    if (state.busy || state.importing || !state.photo_search || state.photo_search->faces.empty()) return;
    selected_image_face_ = std::clamp(index, 0, int(state.photo_search->faces.size()) - 1);
    image_face_offset_ = selected_image_face_ / image_face_rows_ * image_face_rows_;
    focused_.clear(); keyboard_focus_ = "image-face:" + std::to_string(selected_image_face_); select_all_ = false;
}
void DemoUi::searchResults(const ViewState& s, Rect panel) {
    scene_.box(panel, dark);
    std::string mode = s.settings.search_multi ? "M:N - No liveness check" :
        s.settings.search_liveness ? "1:N - Liveness enabled" : "1:N - Liveness disabled";
    std::string status = friendly(s.status);
    const auto live_suffix = status.find(" | liveness ");
    if (live_suffix != std::string::npos) status.resize(live_suffix);
    std::string summary;
    bool matched = false;
    std::optional<float> liveness;
    if (!s.failed && s.connected && !s.camera_stalled && s.mode == 'S') {
        for (const auto& box : s.liveBoxes()) {
            if (!box.similarity && !box.liveness && box.face_id.empty()) continue;
            const std::string title = box.matched ? "Face ID: " + box.face_id : box.label;
            if (!summary.empty()) summary += " | ";
            summary += title;
            if (box.similarity) summary += " - " + number(*box.similarity, 3);
            matched = matched || box.matched;
            if (box.liveness) {
                if (!liveness) liveness = box.liveness;
            }
        }
    }
    if (liveness) mode += " - " + number(*liveness, 3);
    if (summary.empty()) summary = status;
    std::replace(summary.begin(), summary.end(), '\n', ' ');
    std::replace(summary.begin(), summary.end(), '\r', ' ');
    // Keep both captions on one line and use the width released by the removed copy action.
    // 两行状态均不换行，复用移除复制按钮后释放的整行宽度。
    text(panel.x + 18, panel.y + 30, mode, technology_green, panel.w - 36, Font::Large);
    text(panel.x + 18, panel.y + 61, summary, s.failed ? 0xff7777 : matched ? technology_green : 0xffffff,
         panel.w - 36, Font::Large);
}
void DemoUi::scroll(int steps, int x, int y, const ViewState& state) {
    if (soft_keyboard_open_) return;
    if (page_ == "home" && !confirming_ && !browsing_ && !enrolling_) { home_scroll_ = std::clamp(home_scroll_ + steps * 96, 0, home_max_scroll_); keyboard_focus_.clear(); return; }
    if (page_ == "image-search" && !confirming_ && !browsing_ && !enrolling_ && image_result_area_.contains(x,y)) selectImageFace(selected_image_face_ + steps, state);
    if (page_ == "gallery" && library_tab_ == "people" && !confirming_ && !browsing_ && !metadata_dialog_ && library_area_.contains(x,y)) {
        library_scroll_=std::clamp(library_scroll_+steps,0,std::max(0,int(state.gallery.size())-gallery_rows_));
        if (library_scroll_+gallery_rows_>=int(state.gallery.size())-10) loadMoreGallery(state);
    }
}
void DemoUi::drag(int x, int y, const ViewState& state) {
    if (!library_drag_active_ || soft_keyboard_open_ || metadata_dialog_ || confirming_ || browsing_) return;
    const int steps=(library_drag_y_-y)/48;
    if (steps) { scroll(steps,library_area_.x+8,library_area_.y+8,state);library_drag_y_-=steps*48; }
}
void DemoUi::loadMoreGallery(const ViewState& state) {
    if (!state.gallery_has_more || state.busy || state.importing || library_more_pending_ || !state.gallery_query_id.empty() || metadata_dialog_ || confirming_ || browsing_) return;
    library_more_pending_=true;library_attempt_=state.gallery_attempt;
    emit("gallery-more",{std::to_string(state.gallery_next_offset),std::to_string(state.gallery_revision)});
}
void DemoUi::submitField(const ViewState& state) {
    if (browsing_) { handle("browser-go", state); return; }
    if (!state.ready || state.importing || (state.busy && !(page_ == "image-search" && state.photo_search_busy) && !(page_ == "images" && (state.comparison_first_processing || state.comparison_second_processing)))) return;
    const auto field = focused_;
    std::string action;
    if (enrolling_) action = "enrollment-start";
    else if (field == "search-path") action = "image-search";
    else if (field == "device") action = "connect";
    else if (field.rfind("query-",0) == 0) action = "gallery-load";
    else if (metadata_dialog_ && (field == "selected-group" || field == "selected-tag")) return;
    else if (field.rfind("setting:",0) == 0 && !settings_pending_ && settingsDirty(state)) action = "settings-apply";
    else if ((field == "photo-a" || field == "photo-b") && !values_[field].empty()) action = "preview:" + field;
    if (action.empty()) { focused_.clear(); return; }
    // Use current field values: typing and Enter can arrive before the next paint.
    // 读取当前字段值，因为输入和回车可能先于下一次绘制到达。
    focused_.clear(); keyboard_focus_ = action; handle(action, state);
    if (!invalid_field_.empty()) { focused_ = invalid_field_; keyboard_focus_ = "field:" + focused_; cursor_ = values_[focused_].size(); }
}
void DemoUi::page(const std::string& next, const ViewState& state, bool force) {
    if (next == page_) return;
    if (state.importing || (state.busy && !state.photo_search_busy && !state.comparison_first_processing && !state.comparison_second_processing)) { notice_ = "Finish or stop the current operation before changing pages."; return; }
    if (!force && next != page_ && settingsPage(page_) && settingsDirty(state)) {
        confirm({"ui-page", {next}, {}}, "Discard changes?", "Your settings have not been applied. Discard the draft and leave this page?"); return;
    }
    if (next != "home" && !demoPage(next)) return;
    const bool returning = !page_history_.empty() && page_history_.back() == next;
    if (next == "verification" && page_ != "references" && !returning && !force) {
        page("references", state, true); page_history_ = {"home"}; return;
    }
    if (state.photo_search_busy) emit("cancel-image-search");
    if (next == "settings") { settings_tab_ = page_ == "detection" ? "detection" : page_ == "liveness" ? "flow" : page_ == "verification" ? "verification" : "search"; search_advanced_ = false; }
    if (!page_history_.empty() && page_history_.back() == next) page_history_.pop_back();
    else if (next == "home") page_history_.clear();
    else if (homePage(next)) page_history_ = {"home"};
    else if (page_ == "home") page_history_ = next == "references" ? std::vector<std::string>{"home"} : std::vector<std::string>{"home", "camera"};
    else page_history_.push_back(page_);
    page_ = next; camera_menu_ = library_menu_ = soft_keyboard_open_ = false; focused_.clear(); keyboard_focus_.clear(); select_all_ = false;
    if (next == "gallery") {
        library_tab_ = "people";
        library_scroll_=0;library_more_pending_=library_clear_pending_=library_drag_active_=false;
        for (const char* key : {"query-id", "query-group", "query-tag", "selected-id", "selected-group", "selected-tag"}) values_[key].clear();
    }
    if (cameraPage(next)) selected_task_ = std::string(1, demoPage(next)->camera_mode);
    emit("page", {page_});
    if (settingsPage(next)) resetDraft(state.settings);
    values_["photo-a"].clear(); values_["photo-b"].clear(); values_["feature-photo"].clear();
}
void DemoUi::back(const ViewState& state) {
    if (library_menu_) { library_menu_ = false; return; }
    if (page_ == "camera-settings" && sdk_advanced_) { sdk_advanced_ = false; return; }
    if (page_ == "home") return;
    if (page_ == "gallery" && library_tab_ == "transfer" && !state.importing) { library_tab_="people";return; }
    const std::string parent = page_history_.empty() ? "home" : page_history_.back();
    if (state.importing) {
        confirm({"ui-back", {parent}, {}}, "Stop import and go back?", "Completed batches are saved. Stop importing and return to the previous page?");
        return;
    }
    page(parent, state);
}
void DemoUi::requestClose(const ViewState& state) {
    if (metadata_pending_) { notice_="Wait for the metadata save to finish.";return; }
    if (confirming_ || browsing_ || enrolling_ || metadata_dialog_) dismissOverlay();
    if (state.importing) confirm({"ui-quit", {}, {}}, "Stop import and exit?", "Completed batches are already saved. The current batch will finish before the app closes.");
    else if (settingsPage(page_) && settingsDirty(state)) confirm({"ui-quit", {}, {}}, "Discard draft and exit?", "Your settings have not been applied. Saved face data will be kept.");
    else running_ = false;
}
void DemoUi::readDirectory() {
    browser_entries_.clear(); browser_offset_ = 0; browser_error_.clear(); std::error_code error;
    for (std::filesystem::directory_iterator it(browser_directory_, error), end; !error && it != end; it.increment(error)) {
        auto extension = it->path().extension().string(); std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
        std::error_code status;
        if (it->is_directory(status) || browser_target_ == "exchange-path" && extension == ".json" || browser_target_ != "exchange-path" && browser_target_ != "image-folder"
            && (extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" || extension == ".ppm")) browser_entries_.push_back(*it);
    }
    if (error) { browser_entries_.clear(); browser_error_ = "Cannot read folder: " + error.message(); }
    std::sort(browser_entries_.begin(), browser_entries_.end(), [](const auto& a, const auto& b) { std::error_code ea, eb; const bool ad = a.is_directory(ea), bd = b.is_directory(eb); return ad != bd ? ad : a.path().filename().string() < b.path().filename().string(); });
    values_["browser-path"] = browser_directory_;
}
void DemoUi::browse(const std::string& target) {
    beginOverlay(); browser_target_ = target; browsing_ = true; const std::filesystem::path value = values_[target]; std::error_code error;
    if (std::filesystem::is_directory(value,error)) browser_directory_ = value.string();
    else if (value.has_parent_path() && std::filesystem::is_directory(value.parent_path(),error)) browser_directory_ = value.parent_path().string();
    else { const char* home_dir = std::getenv("HOME"); browser_directory_ = home_dir ? home_dir : "/"; }
    readDirectory(); focused_ = "browser-path"; keyboard_focus_ = "field:browser-path"; cursor_ = values_[focused_].size(); select_all_ = true;
}
void DemoUi::handle(const std::string& action, const ViewState& state) {
    if (action == "sdk-advanced") { sdk_advanced_ = !sdk_advanced_; return; }
    if (action == "search-advanced") { search_advanced_ = !search_advanced_; return; }
    if (action == "library-menu") { library_menu_ = !library_menu_; return; }
    if (action == "library-refresh") {
        library_menu_ = false; library_tab_ = "people";
        library_scroll_=0;library_clear_pending_=true;
        clearLibrarySelection();
        values_["query-id"].clear(); values_["query-group"].clear(); values_["query-tag"].clear();
        emit("gallery", {"0", "", "", ""}); return;
    }
    if (action == "library-import-images") { library_menu_ = false; browse("image-folder"); return; }
    if (action == "gallery-clear") { values_["query-id"].clear();handle("gallery-load",state);return; }
    if (action == "gallery-more") { loadMoreGallery(state);return; }
    if (action.rfind("gallery-edit:",0)==0) {
        const auto id=action.substr(13);
        const auto row=std::find_if(state.gallery.begin(),state.gallery.end(),[&](const GalleryEntry& value){return value.id==id;});
        if (row==state.gallery.end()) { notice_="This record changed. Refresh the library.";return; }
        beginOverlay();metadata_dialog_=true;metadata_pending_=metadata_error_=false;
        values_["selected-id"]=row->id;values_["selected-group"]=row->group;values_["selected-tag"]=row->tag;return;
    }
    if (action.rfind("gallery-delete:",0)==0) {
        const auto id=action.substr(15);
        confirm({"delete",{id},{}},"Delete this face?","Face ID: "+id+". This entry will be removed from the library.");return;
    }
    if (action == "camera-menu") { camera_menu_ = !camera_menu_; focused_.clear(); keyboard_focus_.clear(); return; }
    if (action == "back") { back(state); return; }
    if (action == "result-close") { dismissOverlay(); back(state); return; }
    if (action == "result-retry") { if (!result_dialog_) return; dismissOverlay(); emit("action", {selected_task_}); return; }
    if (action == "replacement-cancel") { replacement_declined_ = true; return; }
    if (action == "replacement-confirm") { if (enrollment_saving_) return; enrollment_saving_ = true; enrollment_attempt_ = state.enrollment_attempt; emit("confirm-enrollment", {values_["enroll-id"], values_["enroll-group"], values_["enroll-tag"], "replace"}); return; }
    if (action.rfind("references-prev:", 0) == 0) { reference_offset_ = std::max(0, reference_offset_ - std::stoi(action.substr(16))); return; }
    if (action.rfind("references-next:", 0) == 0) { reference_offset_ += std::stoi(action.substr(16)); return; }
    if (action.rfind("reference-pick:", 0) == 0) {
        emit("capture-reference", {state.captured_faces.at(std::stoul(action.substr(15))).id});
        page("verification", state, true); return;
    }
    if (action.rfind("preview:", 0) == 0) {
        const auto field = action.substr(8); emit("preview-photo", {field == "photo-a" ? "first" : "second", values_[field]}); return;
    }
    if (action == "feature-settings") { page("settings", state); return; }
    if (action.rfind("image-face:", 0) == 0) { selectImageFace(std::stoi(action.substr(11)), state); return; }
    if (action.rfind("image-faces-prev:", 0) == 0) { selectImageFace(std::max(0, image_face_offset_ - image_face_rows_), state); return; }
    if (action.rfind("image-faces-next:", 0) == 0) { selectImageFace(image_face_offset_ + image_face_rows_, state); return; }
    if (action == "image-photo") { browse("search-path"); return; }
    if (action == "quit") { requestClose(state); return; }
    if (action == "cancel") { if (metadata_pending_) return;const bool leaving_capture = enrolling_; if (enrolling_) emit("action", {"X"}); dismissOverlay(); if (leaving_capture) back(state); return; }
    if (action == "confirm") {
        const auto command = pending_; dismissOverlay();
        if (command.type == "ui-quit") running_ = false;
        else if (command.type == "ui-page") page(command.values.at(0), state, true);
        else if (command.type == "ui-back") {
            if (state.importing) emit("cancel-import");
            auto stopped = state; stopped.importing = false;
            page(command.values.at(0), stopped, true);
        }
        else { if (command.type == "delete") clearLibrarySelection(); commands_.push_back(command); notice_.clear(); }
        return;
    }
    if (action.rfind("page:", 0) == 0) { page(action.substr(5), state); return; }
    if (action.rfind("browse:", 0) == 0) { browse(action.substr(7)); return; }
    if (action == "browser-go") { browser_directory_ = values_["browser-path"]; readDirectory(); return; }
    if (action == "browser-up") { browser_directory_ = std::filesystem::path(browser_directory_).parent_path().string(); if (browser_directory_.empty()) browser_directory_ = "/"; readDirectory(); return; }
    if (action == "browser-prev") { browser_offset_ = std::max(0, browser_offset_ - 8); return; }
    if (action == "browser-next") { if (browser_offset_ + 8 < int(browser_entries_.size())) browser_offset_ += 8; return; }
    if (action == "browser-directory") {
        const bool images=browser_target_=="image-folder";const auto path=browser_directory_;
        values_[browser_target_]=path;dismissOverlay();
        if(images)confirm({"import",{path,"images",""},{}},"Import image folder?","Use image filenames as Face IDs. Matching IDs will be replaced; unreadable images and missing faces are skipped. Completed batches remain if stopped.");
        return;
    }
    if (action.rfind("browser-row:", 0) == 0) {
        const auto& entry = browser_entries_.at(std::stoul(action.substr(12)));
        std::error_code error; const bool directory = entry.is_directory(error);
        if (error) { browser_entries_.clear(); browser_offset_ = 0; browser_error_ = "Cannot read file: " + error.message(); return; }
        if (directory) { browser_directory_ = entry.path().string(); readDirectory(); }
        else { const auto target = browser_target_; values_[target] = entry.path().string(); dismissOverlay();
            if (target == "search-path") emit("image-search", {values_[target]});
            else if (target == "photo-a" || target == "photo-b") emit("preview-photo", {target == "photo-a" ? "first" : "second", values_[target]});
            else if (target == "feature-photo") { camera_menu_ = false; emit("image", {page_ == "detection" ? "detect" : page_ == "capture" ? "reference" : "enroll", values_[target], "", "", "", ""}); }
        }
        return;
    }
    if (action.rfind("library:", 0) == 0) { library_menu_ = false; library_tab_ = action.substr(8); return; }
    if (action.rfind("toggle:", 0) == 0) {
        auto& value = values_[action.substr(7)]; value = value == "1" ? "0" : "1"; invalid_field_.clear();
        if (page_ == "camera-settings") handle("settings-apply", state);
        return;
    }
    if (action.rfind("task:", 0) == 0) { selected_task_ = action.substr(5); return; }
    if (action == "search-single" || action == "search-multi") {
        const bool multi = action == "search-multi"; values_["setting:search_multi"] = multi ? "1" : "0"; values_["setting:search_liveness"] = multi ? "0" : "1"; return;
    }
    if (action.rfind("resolution:", 0) == 0) {
        const int index = std::stoi(action.substr(11)); const char* widths[] = {"640", "1280", "1920"}; const char* heights[] = {"360", "720", "1080"}; values_["setting:camera_width"] = widths[index]; values_["setting:camera_height"] = heights[index]; handle("settings-apply", state); return;
    }
    if (action.rfind("ui-rotation:", 0) == 0) {
        values_["setting:ui_rotation"] = action.substr(12); handle("settings-apply", state); return;
    }
    if (action.rfind("add-action:", 0) == 0) {
        const bool verification = settings_tab_ == "verification";
        values_[verification ? "setting:verification_random_actions" : "setting:random_actions"] = "0";
        auto& sequence = values_[verification ? "setting:verification_actions" : "setting:actions"]; if (!sequence.empty()) sequence += ','; sequence += action.substr(11); return;
    }
    if (action == "actions-clear" || action == "actions-undo") {
        const bool verification = settings_tab_ == "verification";
        values_[verification ? "setting:verification_random_actions" : "setting:random_actions"] = "0";
        auto& sequence = values_[verification ? "setting:verification_actions" : "setting:actions"];
        const auto comma = sequence.find_last_of(','); sequence = action == "actions-clear" || comma == std::string::npos ? "" : sequence.substr(0, comma); return;
    }
    if (action == "settings-defaults") {
        for (const auto& f : settingsFields(DemoSettings{})) if (settingVisible(f.key)) values_["setting:" + f.key] = f.value;
        if (page_ == "camera-settings" && settingsDirty(state)) handle("settings-apply", state);
        return;
    }
    if (!state.ready) { notice_ = "The SDK is still loading. Please wait."; return; }
    if (action == "image-search" && values_["search-path"].empty()) { invalid_field_ = "search-path"; notice_ = "Choose a photo or enter its path before searching."; return; }
    if (action == "image-search" || action == "cancel-image-search") { emit(action, action == "image-search" ? std::vector<std::string>{values_["search-path"]} : std::vector<std::string>{}); return; }
    if (action == "image:search") { values_["search-path"] = values_["photo-a"]; page("image-search", state); emit("image-search", {values_["search-path"]}); return; }
    if (action == "start-task") { handle("mode:" + selected_task_, state); return; }
    if (action == "enrollment-start") {
        if (enrollment_saving_) return;
        for (const char* key : {"enroll-id", "enroll-group", "enroll-tag"}) values_[key] = trimText(values_[key]);
        const auto& id = values_["enroll-id"];
        if (values_["enroll-id"].find_first_not_of(" \t\r\n") == std::string::npos) { invalid_field_ = focused_ = "enroll-id"; keyboard_focus_ = "field:enroll-id"; cursor_ = 0; notice_ = "Enter a face ID before confirming enrollment."; return; }
        if (!state.duplicate_enrollment_id.empty() && state.duplicate_enrollment_id == id) { replacement_declined_ = false; return; }
        enrollment_saving_ = true; enrollment_attempt_ = state.enrollment_attempt;
        emit("confirm-enrollment", {values_["enroll-id"], values_["enroll-group"], values_["enroll-tag"]}); return;
    }
    if (action == "enrollment-retake") {
        emit("action", {"X"}); dismissOverlay();
        if (state.connected) emit("action", {selected_task_}); else browse("feature-photo"); return;
    }
    if (action.rfind("mode:", 0) == 0) emit("action", {action.substr(5)});
    else if (action == "connect") emit("connect", {values_["device"]});
    else if (action == "mirror" || action == "next" || action == "pause" || action == "cancel-import") emit(action);
    else if (action == "clear-reference") confirm({"clear-reference", {}, {}}, "Clear reference?", "Remove the saved verification reference. Face library entries and captured photos will be kept.");
    else if (action.rfind("image:", 0) == 0) emit("image", {action.substr(6), values_["photo-a"], values_["photo-b"], "", "", ""});
    else if (action == "gallery-load") {
        clearLibrarySelection();
        library_scroll_=0;library_clear_pending_=true;library_more_pending_=false;
        for (const char* key : {"query-id", "query-group", "query-tag"}) values_[key] = trimText(values_[key]);
        emit("gallery", {"0", values_["query-id"], "", ""});
    } else if (action == "reference") { emit("reference", {values_["selected-id"]}); if (!page_history_.empty() && page_history_.back() == "verification") back(state); }
    else if (action == "metadata") {
        if (metadata_pending_) return;
        values_["selected-group"] = trimText(values_["selected-group"]); values_["selected-tag"] = trimText(values_["selected-tag"]);
        for(const char* name:{"selected-group","selected-tag"})if(values_[name].size()>HIFACE_SEARCH_MAX_TEXT_BYTES){notice_="Metadata must be at most 1024 UTF-8 bytes.";invalid_field_=name;return;}
        metadata_pending_=true;metadata_attempt_=state.metadata_attempt;
        emit("metadata", {values_["selected-id"], values_["selected-group"], values_["selected-tag"]});
    }
    else if (action == "delete") confirm({"delete", {values_["selected-id"]}, {}}, "Delete this face?", "Face ID: " + values_["selected-id"] + ". This entry will be removed from the library.");
    else if (action == "export") emit("export", {values_["exchange-path"]});
    else if (action == "import-demo") { library_menu_ = false; confirm({"import", {state.demo_features, "features", ""}, {}}, "Import synthetic test data?", "10000 synthetic demo features for import and performance testing, not enrolled real users. Matching IDs are replaced; completed batches remain if stopped."); }
    else if (action == "import-features" || action == "import-images") confirm({"import", {values_["exchange-path"], action == "import-images" ? "images" : "features", values_["query-group"]}, {}}, "Import into your library?", "Entries with matching IDs will be replaced. Stopping the import keeps completed batches.");
    else if (action == "settings-apply") {
        Command command{"settings", {}, settingsFields(state.settings)};
        for (auto& f : command.fields) if (settingVisible(f.key)) {
            if (f.key == "group" || f.key == "tag") values_["setting:" + f.key] = trimText(values_["setting:" + f.key]);
            f.value = values_["setting:" + f.key];
        }
        try { parseSettings(command.fields); }
        catch (const DemoError& error) {
            notice_ = error.message;
            for (const auto& f : command.fields) if (error.message.find(f.key) != std::string::npos) {
                invalid_field_ = focused_ = "setting:" + f.key; keyboard_focus_ = "field:" + focused_; cursor_ = values_[focused_].size();
                if (f.key == "search_interval" || f.key == "search_timeout") search_advanced_ = true;
                if (f.key == "camera_width" || f.key == "camera_height" || f.key == "camera_fps") sdk_advanced_ = true;
                break;
            }
            return;
        }
        commands_.push_back(std::move(command)); settings_pending_ = true; settings_attempt_ = state.settings_attempt; notice_.clear(); focused_.clear();
    }
}
void DemoUi::clearLibrarySelection() {
    for (const char* key : {"selected-id", "selected-group", "selected-tag"}) values_[key].clear();
}
void DemoUi::edit(const std::string& text_value, const std::string& field) {
    if (field.empty()) return; auto& value = values_[field]; std::string clean;
    for (unsigned char c : text_value) if (c >= 32 && c != 127) clean += char(c);
    const bool replace = select_all_ && focused_ == field;
    if ((replace ? 0 : value.size()) + clean.size() > 4096) { notice_ = "Input is limited to 4096 bytes."; return; }
    if (replace) { value.clear(); cursor_ = 0; select_all_ = false; }
    const auto position = focused_ == field ? std::min(cursor_, value.size()) : value.size(); value.insert(position, clean); if (focused_ == field) cursor_ = position + clean.size(); invalid_field_.clear(); notice_.clear();
}
void DemoUi::paste(const std::string& text_value, const std::string& field) {
    if (field != focused_) return;
    for (const auto& control : controls_) if (control.field == field && control.enabled) { edit(text_value, field); return; }
}
std::string DemoUi::selectedText() const { const auto it = values_.find(focused_); return select_all_ && it != values_.end() ? it->second : ""; }
void DemoUi::click(int x, int y, const ViewState& state) {
    pointer(x, y);
    library_drag_active_=false;
    try {
        for (auto it = controls_.rbegin(); it != controls_.rend(); ++it) if (it->bounds.contains(x, y)) {
            if (!it->enabled) return;
            if (it->action.rfind("soft:", 0) == 0) { const auto action = it->action; softKeyboardKey(action, state); return; }
            keyboard_focus_ = it->id(); select_all_ = false;
            if (!it->field.empty()) {
                const bool had_caret = focused_ == it->field; const size_t previous_cursor = cursor_; focused_ = it->field;
                const auto& value = values_[focused_]; size_t at = std::min(it->text_start,value.size());
                const int wanted = x-it->bounds.x-10; int previous_x = 0;
                while (at < value.size()) {
                    size_t next = at+1; while (next < value.size() && (static_cast<unsigned char>(value[next]) & 0xc0) == 0x80) ++next;
                    const int px = measure_(value.substr(it->text_start,next-it->text_start),Font::Body) + (had_caret && previous_cursor < next ? measure_("|",Font::Body) : 0);
                    if (wanted < (previous_x+px)/2) break;
                    at = next; previous_x = px;
                }
                cursor_ = at;
                if (soft_keyboard_enabled_) {
                    soft_keyboard_target_ = focused_; soft_keyboard_label_ = it->label; soft_keyboard_open_ = true;
                }
            }
            else { focused_.clear(); handle(it->action, state); }
            return;
        }
        if (soft_keyboard_open_) { softKeyboardKey("soft:done", state); return; }
        if(page_=="gallery" && library_tab_=="people" && library_area_.contains(x,y) && !metadata_dialog_ && !confirming_ && !browsing_ && !state.importing){library_drag_active_=true;library_drag_y_=y;}
        focused_.clear(); keyboard_focus_.clear(); select_all_ = false;
    } catch (const DemoError& error) { notice_ = error.message; }
    catch (const std::exception& error) { notice_ = error.what(); }
}
void DemoUi::key(Key key_value, const std::string& value, bool shift, const ViewState& state) {
    if (soft_keyboard_open_) {
        if (key_value == Key::Escape || key_value == Key::Enter) { softKeyboardKey("soft:done", state); return; }
        if (key_value == Key::Tab || key_value >= Key::Page1 && key_value <= Key::Page5) return;
    }
    if (key_value == Key::Escape) {
        if (metadata_pending_) return;
        if (enrolling_ && enrollment_saving_) return;
        if (enrolling_ && !replacement_declined_ && !state.duplicate_enrollment_id.empty() && state.duplicate_enrollment_id == values_["enroll-id"]) {
            handle("replacement-cancel", state); return;
        }
        if (result_dialog_) { handle("result-close", state); return; }
        if (confirming_ || browsing_ || enrolling_ || metadata_dialog_) handle("cancel", state);
        else if (page_ == "image-search" && state.photo_search_busy) { focused_.clear(); keyboard_focus_.clear(); emit("cancel-image-search"); }
        else if (!focused_.empty()) { focused_.clear(); keyboard_focus_.clear(); select_all_ = false; }
        else if (camera_menu_) { camera_menu_ = false; }
        else back(state);
        return;
    }
    if (page_ == "home" && !confirming_ && !browsing_ && !enrolling_ &&
        (key_value == Key::PreviousPage || key_value == Key::NextPage || key_value == Key::Home || key_value == Key::End)) {
        home_scroll_ = key_value == Key::Home ? 0 : key_value == Key::End ? home_max_scroll_ :
            std::clamp(home_scroll_ + (key_value == Key::NextPage ? 240 : -240), 0, home_max_scroll_); keyboard_focus_.clear(); return;
    }
    if (key_value >= Key::Page1 && key_value <= Key::Page5) {
        if (!confirming_ && !browsing_ && !enrolling_ && !result_dialog_ && !metadata_dialog_) { const char* pages[] = {"camera", "images", "capture", "camera-settings", "image-search"}; page(pages[int(key_value) - int(Key::Page1)], state); }
        return;
    }
    if (key_value == Key::Tab) {
        std::vector<const Control*> active; for (const auto& c : controls_) if (c.enabled) active.push_back(&c);
        if (active.empty()) return;
        auto it = std::find_if(active.begin(), active.end(), [&](const Control* c) { return c->id() == keyboard_focus_; });
        const size_t index = it == active.end() ? shift ? active.size() - 1 : 0 : (it - active.begin() + (shift ? active.size() - 1 : 1)) % active.size();
        keyboard_focus_ = active[index]->id(); focused_ = active[index]->field; cursor_ = values_[focused_].size(); select_all_ = false; return;
    }
    if (!focused_.empty()) {
        const auto active = std::find_if(controls_.begin(), controls_.end(), [&](const Control& control) { return control.field == focused_ && control.enabled; });
        if (active == controls_.end()) return;
        auto& text_value = values_[focused_]; cursor_ = std::min(cursor_, text_value.size());
        auto previous = [&] { if (cursor_) { --cursor_; while (cursor_ && (static_cast<unsigned char>(text_value[cursor_]) & 0xc0) == 0x80) --cursor_; } };
        auto next = [&] { if (cursor_ < text_value.size()) { ++cursor_; while (cursor_ < text_value.size() && (static_cast<unsigned char>(text_value[cursor_]) & 0xc0) == 0x80) ++cursor_; } };
        if (key_value == Key::SelectAll) { select_all_ = true; return; }
        if (key_value == Key::Text) { edit(value, focused_); return; }
        if (key_value == Key::Enter) { submitField(state); return; }
        if (select_all_ && (key_value == Key::Backspace || key_value == Key::Delete)) { text_value.clear(); cursor_ = 0; select_all_ = false; return; }
        if (key_value == Key::Backspace) { const auto old = cursor_; previous(); text_value.erase(cursor_, old - cursor_); }
        else if (key_value == Key::Delete) { const auto old = cursor_; next(); text_value.erase(old, cursor_ - old); cursor_ = old; }
        else if (key_value == Key::Left) { if (select_all_) cursor_ = 0; else previous(); }
        else if (key_value == Key::Right) { if (select_all_) cursor_ = text_value.size(); else next(); }
        else if (key_value == Key::Home) cursor_ = 0; else if (key_value == Key::End) cursor_ = text_value.size();
        select_all_ = false; return;
    }
    if (key_value == Key::Enter || key_value == Key::Text && value == " ") {
        for (const auto& c : controls_) if (c.enabled && c.id() == keyboard_focus_ && c.field.empty()) { const auto action = c.action; handle(action, state); return; }
    }
    if (confirming_ || browsing_ || enrolling_ || result_dialog_ || metadata_dialog_) return;
    if(page_=="gallery" && library_tab_=="people") {
        int next=library_scroll_;
        if(key_value==Key::Home)next=0;else if(key_value==Key::End)next=std::max(0,int(state.gallery.size())-gallery_rows_);
        else if(key_value==Key::Down || key_value==Key::NextPage)next+=key_value==Key::NextPage?gallery_rows_:1;
        else if(key_value==Key::Up || key_value==Key::PreviousPage)next-=key_value==Key::PreviousPage?gallery_rows_:1;
        if(next!=library_scroll_)scroll(next-library_scroll_,library_area_.x+8,library_area_.y+8,state);
    }
    if (page_ == "image-search" && !state.busy) {
        if (key_value == Key::Left || key_value == Key::Up) { selectImageFace(selected_image_face_-1,state); return; }
        if (key_value == Key::Right || key_value == Key::Down) { selectImageFace(selected_image_face_+1,state); return; }
        if (key_value == Key::Home) { selectImageFace(0,state); return; }
        if (key_value == Key::End) { selectImageFace(state.photo_search ? int(state.photo_search->faces.size())-1 : 0,state); return; }
        if (key_value == Key::PreviousPage || key_value == Key::NextPage) { selectImageFace(selected_image_face_ + (key_value == Key::PreviousPage ? -image_face_rows_ : image_face_rows_),state); return; }
    }
    if (key_value == Key::Text && value.size() == 1 && cameraPage(page_) && content_enabled_) {
        const char c = char(std::toupper(static_cast<unsigned char>(value[0])));
        if (c == 'X' && state.connected) handle("mode:X", state);
        else if (std::string("DCLVES").find(c) != std::string::npos) {
            for (const auto& feature : demo_pages) if (feature.camera_mode == c) { page(feature.route, state); break; }
        }
        else if (c == 'M') handle("mirror", state); else if (c == 'N') handle("next", state);
        else if (c == 'P' && (state.mode == 'S' || state.mode == 'P')) handle("pause", state); else if (c == 'Q') requestClose(state);
    }
}
void DemoUi::browser() {
    controls_.clear(); const int w = std::min(760, scene_.width - 80), h = std::min(548, scene_.height - 80), x = (scene_.width - w) / 2, y = (scene_.height - h) / 2;
    scene_.box({0, 0, scene_.width, scene_.height}, 0x253342); card({x, y, w, h}); text(x + 22, y + 34, browser_target_ == "exchange-path" ? "Choose a JSON file or folder" : browser_target_=="image-folder"?"Choose an image folder":"Choose a photo", ink, w - 44, Font::Title);
    field({x + 22, y + 58, w - 218, 56}, "browser-path", "Directory", browser_directory_);
    button({x + w - 184, y + 79, 74, 34}, "Open", "browser-go"); button({x + w - 100, y + 79, 78, 34}, "Parent", "browser-up");
    const int row_y = y + 136, rh = 34;
    for (int i = 0; i < 8 && browser_offset_ + i < int(browser_entries_.size()); ++i) {
        const int index = browser_offset_ + i; const auto& e = browser_entries_[index]; std::error_code error; const bool dir = e.is_directory(error);
        const Rect r{x + 22, row_y + i * rh, w - 44, rh - 2}; scene_.box(r, r.contains(pointer_x_, pointer_y_) ? tint : i % 2 ? 0xf7f9fa : surface, 4);
        text(r.x + 12, r.y + 21, e.path().filename().string() + (dir ? "/" : ""), dir ? accent : ink, r.w - 24);
        if (keyboard_focus_ == "browser-row:" + std::to_string(index)) scene_.box({r.x, r.y + r.h - 2, r.w, 2}, accent);
        controls_.push_back({r, "browser-row:" + std::to_string(index), "", true});
    }
    if (!browser_error_.empty()) {
        scene_.box({x+22,row_y,w-44,116},0xffeeeb,8);
        wrap(x+36,row_y+27,browser_error_,danger,w-72,3);
        text(x+36,row_y+96,"Change the directory above or use Parent to go back.",muted,w-72,Font::Small);
    } else if (browser_entries_.empty()) {
        text(x + 32, row_y + 30, "No matching files in this folder", ink, w - 64);
        wrap(x+32,row_y+62,browser_target_ == "exchange-path" ? "Choose a folder or a .json feature file." : "Supported photos: JPEG, PNG, BMP and PPM.",muted,w-64,2,Font::Small);
    }
    const int bottom = y + h - 94;
    button({x + 22, bottom, 90, 30}, "Previous", "browser-prev", 0, browser_offset_ > 0); button({x + 120, bottom, 78, 30}, "Next", "browser-next", 0, browser_offset_ + 8 < int(browser_entries_.size()));
    text(x + 220, bottom + 20, std::to_string(browser_entries_.size()) + " items", muted, w - 250, Font::Small);
    if (browser_target_ == "exchange-path" || browser_target_=="image-folder") button({x + 22, y + h - 48, 190, 34}, "Use this directory", "browser-directory", 1, browser_error_.empty());
    button({x + w - 112, y + h - 48, 90, 34}, "Cancel", "cancel");
}
void DemoUi::softKeyboardKey(const std::string& action, const ViewState& state) {
    if (!soft_keyboard_open_) return;
    focused_ = soft_keyboard_target_; keyboard_focus_ = "field:" + focused_;
    if (action == "soft:done") { soft_keyboard_open_ = false; return; }
    if (action == "soft:shift") { soft_keyboard_upper_ = !soft_keyboard_upper_; return; }
    if (action == "soft:symbols") { soft_keyboard_symbols_ = !soft_keyboard_symbols_; return; }
    if (action == "soft:backspace") { key(Key::Backspace, "", false, state); return; }
    if (action == "soft:clear") { key(Key::SelectAll, "", false, state); key(Key::Backspace, "", false, state); return; }
    if (action == "soft:space") { edit(" ", focused_); return; }
    if (action.rfind("soft:type:", 0) == 0) edit(action.substr(10), focused_);
}
void DemoUi::softKeyboard() {
    if (!soft_keyboard_open_) return;
    const auto active = std::find_if(controls_.begin(), controls_.end(), [&](const Control& c) { return c.field == soft_keyboard_target_ && c.enabled; });
    if (active == controls_.end()) { soft_keyboard_open_ = false; return; }
    controls_.clear();
    const int w = std::min(900, scene_.width - 16), h = 376, x = (scene_.width - w) / 2, y = scene_.height - h - 8;
    scene_.box({x - 2,y - 3,w + 4,h + 6},border,12);card({x,y,w,h});
    text(x + 16,y + 28,"On-screen keyboard",ink,w - 148,Font::Title);
    button({x+w-116,y+8,100,36},"Done","soft:done",1);
    field({x+16,y+50,w-32,56},soft_keyboard_target_,soft_keyboard_label_);
    const int gap = 4, key_w = (w - 32 - 9*gap) / 10, step = key_w + gap, left = x + (w - (10*key_w+9*gap)) / 2;
    auto character = [&](int px, int py, char value) {
        if (!soft_keyboard_symbols_ && soft_keyboard_upper_) value = char(std::toupper(static_cast<unsigned char>(value)));
        button({px,py,key_w,42},std::string(1,value),"soft:type:"+std::string(1,value),0,true,Font::Large);
    };
    const std::string rows[] = {"1234567890",soft_keyboard_symbols_ ? "!@#$%&*()?" : "qwertyuiop",soft_keyboard_symbols_ ? "-_+=:;,.'" : "asdfghjkl"};
    for (int row=0;row<3;++row) {
        const int inset = (10-int(rows[row].size()))*step/2;
        for (size_t i=0;i<rows[row].size();++i) character(left+inset+int(i)*step,y+118+row*48,rows[row][i]);
    }
    button({left,y+262,key_w,42},"A/a","soft:shift",soft_keyboard_upper_?2:0,!soft_keyboard_symbols_,Font::Small);
    const std::string last = soft_keyboard_symbols_ ? "[]{}<>/" : "zxcvbnm";
    for (size_t i=0;i<last.size();++i) character(left+(int(i)+1)*step,y+262,last[i]);
    button({left+8*step,y+262,key_w*2+gap,42},"Delete","soft:backspace",0);
    button({left,y+310,key_w*2+gap,42},soft_keyboard_symbols_?"ABC":"Symbols","soft:symbols",soft_keyboard_symbols_?2:0);
    button({left+2*step,y+310,key_w*2+gap,42},"Clear","soft:clear",0);
    button({left+4*step,y+310,key_w*2+gap,42},"Space","soft:space",0);
    for(int i=0;i<4;++i) character(left+(6+i)*step,y+310,std::string("-_.@")[i]);
}
void DemoUi::modal(const ViewState& state) {
    if (browsing_) { browser(); return; }
    if(metadata_dialog_) {
        controls_.clear();const int w=std::min(590,scene_.width-64),h=420,x=(scene_.width-w)/2,y=(scene_.height-h)/2;
        scene_.box({0,0,scene_.width,scene_.height},0x253342);card({x,y,w,h});
        text(x+24,y+38,"Edit face metadata",ink,w-48,Font::Title);
        wrap(x+24,y+78,"Face ID: "+values_["selected-id"],muted,w-48,3);
        const bool enabled=!metadata_pending_ && !state.busy;
        field({x+24,y+138,w-48,56},"selected-group","Group","",enabled);
        field({x+24,y+212,w-48,56},"selected-tag","Tag","",enabled);
        if(!notice_.empty() || metadata_error_)wrap(x+24,y+306,!notice_.empty()?notice_:friendly(state.status),danger,w-48,2,Font::Small);
        button({x+24,y+h-62,112,38},"Cancel","cancel",0,enabled);
        button({x+w-184,y+h-62,160,38},metadata_pending_?"Saving...":"Save","metadata",1,enabled);
        return;
    }
    if (result_dialog_) {
        controls_.clear(); const int w = std::min(590, scene_.width - 64), h = 280, x = (scene_.width - w) / 2, y = (scene_.height - h) / 2;
        scene_.box({0,0,scene_.width,scene_.height},0x253342); card({x,y,w,h});
        text(x + 24, y + 40, state.result_title, ink, w - 48, Font::Title);
        wrap(x + 24, y + 90, friendly(state.status), ink, w - 48, 6);
        button({x + 24, y + h - 62, 130, 38}, "Close", "result-close", 0, !state.busy);
        if (state.flow_result_can_retry) button({x + w - 184, y + h - 62, 160, 38}, "Try again", "result-retry", 1, !state.busy);
        return;
    }
    if (!confirming_ && !enrolling_) return;
    controls_.clear(); const int w = std::min(590, scene_.width - 100), h = enrolling_ ? 420 : 250, x = (scene_.width - w) / 2, y = (scene_.height - h) / 2;
    scene_.box({0, 0, scene_.width, scene_.height}, 0x253342); card({x, y, w, h});
    if (enrolling_ && !replacement_declined_ && !state.duplicate_enrollment_id.empty() && state.duplicate_enrollment_id == values_["enroll-id"]) {
        text(x + 24, y + 40, "Replace this Face ID?", ink, w - 48, Font::Title);
        wrap(x + 24, y + 90, "Face ID: " + values_["enroll-id"] + ". The existing search-library feature and group will be replaced only if you confirm.", ink, w - 48, 6);
        button({x + 24, y + h - 62, 150, 38}, "Keep existing", "replacement-cancel", 0, content_enabled_);
        button({x + w - 200, y + h - 62, 176, 38}, "Replace", "replacement-confirm", 3, content_enabled_);
        return;
    }
    text(x + 24, y + 38, enrolling_ ? state.pending_capture_kind == 'C' ? "Use this face photo?" : "Enroll a face" : confirm_title_, ink, w - 48, Font::Title);
    if (enrolling_) {
        picture({x + 24, y + 64, 128, 128}, state.aligned_preview.get(), nullptr, state.settings.mirror, {});
        field({x + 176, y + 68, w - 200, 56}, "enroll-id", "Face ID (required)");
        if (state.pending_capture_kind != 'C') {
            field({x + 176, y + 140, (w - 212) / 2, 56}, "enroll-group", "Group");
            field({x + 188 + (w - 212) / 2, y + 140, (w - 212) / 2, 56}, "enroll-tag", "Tag");
        }
        wrap(x + 24, y + 242, !notice_.empty() ? notice_ : state.failed ? friendly(state.status) : state.pending_capture_kind == 'C' ? "Enter a Face ID to save this capture. Saving an existing ID updates its reference." : "Review this capture. Nothing is saved until you confirm. Replacing an existing ID requires another confirmation.",
             !notice_.empty() || state.failed ? danger : muted, w - 48, 3, Font::Small);
        button({x + 146, y + h - 62, 108, 38}, "Retake", "enrollment-retake", 0, content_enabled_ && !enrollment_saving_);
        button({x + w - 224, y + h - 62, 200, 38}, enrollment_saving_ ? "Saving..." : state.pending_capture_kind == 'C' ? "Done" : "Confirm and save", "enrollment-start", 1, content_enabled_ && state.pending_enrollment && !enrollment_saving_);
    } else {
        wrap(x + 24, y + 82, confirm_message_, muted, w - 48, 5);
        button({x + w - 184, y + h - 62, 160, 38}, pending_.type == "ui-quit" ? "Exit" : "Confirm", "confirm", pending_.type == "delete" ? 3 : 1, !state.busy);
    }
    button({x + 24, y + h - 62, 108, 38}, "Cancel", "cancel", 0, !enrolling_ || !enrollment_saving_ && !state.busy);
}
const Scene& DemoUi::render(const ViewState& state, int width, int height) {
    scene_.draws.clear(); scene_.width = width; scene_.height = height; scene_.clip = {0, 0, width, height}; controls_.clear(); image_result_area_ = {};library_area_={};
    content_enabled_ = state.ready && !state.busy && !state.importing;
    if (!state.ready && state.failed) { startupFailure(state); return scene_; }
    if (state.ready && !initialized_) {
        initialized_ = true; values_["device"] = state.device; values_["exchange-path"] = state.data_directory + "/features.json"; resetDraft(state.settings);
        const char* start_search = std::getenv("HIFACE_START_SEARCH");
        if (start_search && std::string(start_search) == "1") {
            page_ = "camera"; selected_task_ = "S";
            page_history_ = {"home"};
            emit("page", {"camera"});
        }
    }
    if (settings_pending_ && state.settings_attempt > settings_attempt_) {
        settings_pending_ = false;
        if (!state.failed) { resetDraft(state.settings); if (page_ == "settings" && settings_tab_ == "search") back(state); }
    }
    if(metadata_pending_ && state.metadata_attempt>metadata_attempt_ && !state.busy) {
        metadata_pending_=false;metadata_error_=state.failed;if(!state.failed){dismissOverlay();clearLibrarySelection();}
    }
    if ((page_ == "enrollment" || page_ == "capture") && state.pending_enrollment && state.enrollment_generation != enrollment_generation_) {
        enrollment_generation_ = state.enrollment_generation; beginOverlay(); enrolling_ = true;
        replacement_declined_ = false;
        enrollment_saving_ = false;
        values_["enroll-id"].clear(); values_["enroll-group"].clear(); values_["enroll-tag"].clear();
        focused_ = "enroll-id"; keyboard_focus_ = "field:enroll-id"; cursor_ = values_[focused_].size();
    }
    if (enrolling_ && !state.pending_enrollment && !state.busy) dismissOverlay();
    if (enrollment_saving_ && state.enrollment_attempt > enrollment_attempt_ && !state.busy) enrollment_saving_ = false;
    if (state.saved_capture_generation != saved_capture_generation_) {
        saved_capture_generation_ = state.saved_capture_generation;
        if (page_ == "capture" && state.saved_capture_kind == 'C' || page_ == "enrollment" && state.saved_capture_kind == 'E') { dismissOverlay(); back(state); }
    }
    if (cameraPage(page_) && state.flow_result_pending && state.flow_result_generation != flow_result_generation_) {
        flow_result_generation_ = state.flow_result_generation; beginOverlay(); result_dialog_ = true;
    }
    if (page_ == "home") { home(state, {0, 0, width, height}); modal(state); softKeyboard(); return scene_; }
    if (cameraPage(page_)) {
        camera(state, {0, 0, width, height});
        modal(state);
        softKeyboard();
        return scene_;
    }
    scene_.box({0, 0, width, height}, background);
    const int cx = 24, cw = width - 48;
    const auto* feature = demoPage(page_);
    std::string title = feature ? feature->title : "HelloFace SDK";
    if (page_ == "settings") title = settings_tab_ == "detection" ? "Detection settings" : settings_tab_ == "search" ? "Search settings" : settings_tab_ == "verification" ? "Verification settings" : "Liveness settings";
    pageHeader(state, title, false);
    const bool library_page=page_=="gallery", full_library=library_page && library_tab_=="people";
    const Rect body{cx,library_page?64:104,cw,height-(full_library?76:library_page?162:202)};
    if (page_ == "images") photos(state, body); else if (page_ == "gallery") library(state, body); else if (page_ == "references") references(state, body); else if (page_ == "image-search") imageSearch(state, body); else settings(state, body);
    if(full_library){modal(state);softKeyboard();return scene_;}
    const bool rejected = state.status.find("Rejected") != std::string::npos || state.status.find("Not matched") != std::string::npos || state.status.find("timeout") != std::string::npos;
    const bool error = state.failed || !notice_.empty();
    const bool photo_page = page_ == "image-search";
    const bool warning = !error && !state.busy && (rejected || photo_page && state.photo_search && (state.photo_search->library_count == 0 || !state.photo_search->faces.empty() && state.photo_search->matched == 0));
    const uint32_t status_color = error ? danger : warning ? 0x9b6b16 : state.busy ? 0x365f86 : accent;
    scene_.box({cx, height - 78, cw, 58}, error ? 0xffeeeb : warning ? 0xfff4dc : state.busy ? 0xeaf1f8 : 0xe8f3ee, 9);
    scene_.box({cx + 14, height - 58, 7, 7}, status_color, 3);
    wrap(cx + 32, height - 53, friendly(notice_.empty() ? state.busy ? "Working... " + state.status : state.status : notice_), status_color, cw - 48, 2, Font::Small);
    modal(state); softKeyboard(); return scene_;
}
void DemoUi::pageForSmoke(unsigned index) {
    page_ = smoke_pages[index % smoke_page_count]; camera_menu_ = false;
    page_history_ = page_ == "home" ? std::vector<std::string>{} : homePage(page_) ? std::vector<std::string>{"home"} : std::vector<std::string>{"home", "camera"};
    if (page_ == "settings") settings_tab_ = "search";
    if (page_ == "gallery") library_tab_ = "people";
    if (cameraPage(page_)) selected_task_ = std::string(1, demoPage(page_)->camera_mode);
}
}

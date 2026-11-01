#include "settings.h"
#include "file_io.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>

namespace hiface_linux_demo {
bool useSoftwareMouseCursor(const std::string& mode, bool embedded_display) {
    require(mode.empty() || mode == "auto" || mode == "software" || mode == "system",
            "HIFACE_MOUSE_CURSOR must be auto, software or system");
    return mode == "software" || ((mode.empty() || mode == "auto") && embedded_display);
}
int initialUiRotation() {
    const char* value = std::getenv("HIFACE_DISPLAY_ROTATE");
    const std::string rotation = value ? value : "";
    if (rotation.empty() || rotation == "none" || rotation == "0") return 0;
    // Preserve the existing embedded-display aliases and their input mapping.
    // 保留已有嵌入式显示别名及其输入映射；数字表示顺时针角度。
    if (rotation == "left" || rotation == "ccw" || rotation == "90ccw" || rotation == "90") return 90;
    if (rotation == "right" || rotation == "cw" || rotation == "90cw" || rotation == "270") return 270;
    if (rotation == "180") return 180;
    throw DemoError{"HIFACE_DISPLAY_ROTATE must be 0/90/180/270, left/ccw/90ccw, right/cw/90cw or none"};
}
const char* actionName(HiFaceFlowActionV1 action) {
    switch (action) {
        case HIFACE_ACTION_BLINK: return "blink";
        case HIFACE_ACTION_NOD_HEAD: return "nod";
        case HIFACE_ACTION_SHAKE_HEAD: return "shake";
        case HIFACE_ACTION_MOUTH_OPEN: return "mouth";
        default: return "unknown";
    }
}
std::vector<FormField> settingsFields(const DemoSettings& s) {
    std::string actions;
    for (auto action : s.actions) { if (!actions.empty()) actions += ','; actions += actionName(action); }
    std::string verification_actions;
    for (auto action : s.verification_actions) { if (!verification_actions.empty()) verification_actions += ','; verification_actions += actionName(action); }
    auto number = [](float x) { std::ostringstream text; text << x; return text.str(); };
    auto boolean = [](bool x) { return x ? "1" : "0"; };
    return {
        {"detection_faces", "Detection faces (1..10)", std::to_string(s.detection_faces)},
        {"detailed", "468 landmarks / pose (0/1)", boolean(s.detailed)},
        {"actions", "Actions: blink,nod,shake,mouth; empty = silent", actions},
        {"allow_multiple", "Allow other faces in liveness (0/1)", boolean(s.allow_multiple)},
        {"random_actions", "Three random liveness actions (0/1)", boolean(s.random_actions)},
        {"verification_actions", "Verification actions", verification_actions},
        {"verification_random_actions", "Two random verification actions (0/1)", boolean(s.verification_random_actions)},
        {"verification_allow_multiple", "Allow other faces in verification (0/1)", boolean(s.verification_allow_multiple)},
        {"verification_liveness", "Verification silent liveness (0/1)", boolean(s.verification_liveness)},
        {"verification_threshold", "Verification threshold (0..1)", number(s.verification_threshold)},
        {"search_multi", "Search mode: 0 = 1:N, 1 = M:N", boolean(s.search_multi)},
        {"search_liveness", "1:N silent liveness (0/1; disabled for M:N)", boolean(s.search_liveness)},
        {"search_faces", "M:N max faces (1..5)", std::to_string(s.search_faces)},
        {"search_threshold", "Search threshold (0.75..0.85)", number(s.search_threshold)},
        {"search_interval", "Search success interval ms (0..3000)", std::to_string(s.search_interval)},
        {"search_timeout", "Search round timeout ms (1000..3000)", std::to_string(s.search_timeout)},
        {"group", "Search group (empty = all)", s.group}, {"tag", "Search tag (empty = all)", s.tag},
        {"camera_width", "Camera width (even, 2..1920)", std::to_string(s.camera_width)},
        {"camera_height", "Camera height (even, 2..1080)", std::to_string(s.camera_height)},
        {"camera_fps", "Requested camera FPS (1..60)", std::to_string(s.camera_fps)},
        {"camera_mjpeg", "Prefer MJPEG (0 = YUYV, 1 = MJPEG)", boolean(s.camera_mjpeg)},
        {"mirror", "Mirror preview only (0/1)", boolean(s.mirror)},
        {"ui_rotation", "Clockwise UI rotation (0/90/180/270)", std::to_string(s.ui_rotation)}
    };
}
DemoSettings parseSettings(const std::vector<FormField>& fields) {
    std::map<std::string, std::string> values;
    for (const auto& field : fields) {
        require(field.value.find_first_of("\r\n") == std::string::npos && field.value.size() <= HIFACE_SEARCH_MAX_TEXT_BYTES,
            "Invalid or oversized setting: " + field.key);
        require(values.emplace(field.key, field.value).second, "Duplicate setting: " + field.key);
    }
    auto number = [&](const char* key, double low, double high) {
        const auto value = values.find(key); require(value != values.end() && !value->second.empty(), std::string("Missing setting: ") + key);
        size_t end = 0; double parsed = 0;
        try { parsed = std::stod(value->second, &end); } catch (...) { throw DemoError{std::string("Invalid number: ") + key}; }
        require(end == value->second.size() && std::isfinite(parsed) && parsed >= low && parsed <= high, std::string("Out of range: ") + key);
        return parsed;
    };
    auto integer = [&](const char* key, int low, int high) {
        const auto value = number(key, low, high);
        require(std::floor(value) == value, std::string("Expected integer: ") + key);
        return static_cast<int>(value);
    };
    DemoSettings s;
    s.detection_faces = integer("detection_faces", 1, 10); s.detailed = integer("detailed", 0, 1);
    s.allow_multiple = integer("allow_multiple", 0, 1); s.verification_liveness = integer("verification_liveness", 0, 1);
    s.random_actions = integer("random_actions", 0, 1);
    s.verification_random_actions = integer("verification_random_actions", 0, 1);
    s.verification_allow_multiple = integer("verification_allow_multiple", 0, 1);
    s.verification_threshold = number("verification_threshold", 0, 1);
    s.search_multi = integer("search_multi", 0, 1); s.search_liveness = integer("search_liveness", 0, 1) && !s.search_multi;
    s.search_faces = integer("search_faces", 1, 5); s.search_threshold = number("search_threshold", .75, .85);
    s.search_interval = integer("search_interval", 0, 3000); s.search_timeout = integer("search_timeout", 1000, 3000);
    s.group = values["group"]; s.tag = values["tag"];
    s.camera_width = integer("camera_width", 2, 1920); s.camera_height = integer("camera_height", 2, 1080);
    require(!(s.camera_width & 1) && !(s.camera_height & 1), "Camera dimensions must be even");
    s.camera_fps = integer("camera_fps", 1, 60); s.camera_mjpeg = integer("camera_mjpeg", 0, 1); s.mirror = integer("mirror", 0, 1);
    s.ui_rotation = integer("ui_rotation", 0, 270);
    require(s.ui_rotation % 90 == 0, "ui_rotation must be 0, 90, 180 or 270");
    auto parseActions = [&](const std::string& key, std::vector<HiFaceFlowActionV1>& output) {
        std::istringstream actions(values[key]); std::string action;
        while (std::getline(actions, action, ',')) {
            const auto begin = action.find_first_not_of(" \t"), end = action.find_last_not_of(" \t");
            require(begin != std::string::npos, "Empty action in list"); action = action.substr(begin, end - begin + 1);
            HiFaceFlowActionV1 value;
            if (action == "blink") value = HIFACE_ACTION_BLINK; else if (action == "nod") value = HIFACE_ACTION_NOD_HEAD;
            else if (action == "shake") value = HIFACE_ACTION_SHAKE_HEAD; else if (action == "mouth") value = HIFACE_ACTION_MOUTH_OPEN;
            else throw DemoError{"Unknown action: " + action};
            output.push_back(value); require(output.size() <= HIFACE_MAX_FACE_FLOW_ACTIONS, "At most four actions, repeats allowed");
        }
        require(values[key].empty() || values[key].back() != ',', "Trailing comma in actions");
    };
    parseActions("actions", s.actions); parseActions("verification_actions", s.verification_actions);
    return s;
}
DemoSettings loadSettings(const std::string& file) {
    DemoSettings defaults;
    defaults.ui_rotation = initialUiRotation();
    auto fields = settingsFields(defaults);
    std::ifstream input(file);
    if (!std::filesystem::exists(file)) return defaults;
    require(bool(input), "Cannot read settings file");
    std::string key, value, line;
    std::map<std::string, bool> seen;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream row(line);
        require(bool(row >> key >> std::quoted(value)), "Invalid settings line");
        row >> std::ws; require(row.eof(), "Trailing saved setting content");
        require(!seen[key], "Duplicate saved setting: " + key); seen[key] = true;
        bool found = false;
        for (auto& field : fields) if (field.key == key) { field.value = value; found = true; break; }
        require(found, "Unknown saved setting: " + key);
    }
    require(input.eof(), "Invalid settings file");
    // Old files had one custom sequence shared by both flows. Preserve it
    // during migration; fresh installations use Android's random defaults.
    if (seen.count("actions") && !seen.count("random_actions")) {
        std::string old_actions, old_multiple;
        for (auto& field : fields) { if (field.key == "actions") old_actions = field.value; if (field.key == "allow_multiple") old_multiple = field.value; }
        for (auto& field : fields) {
            if (field.key == "random_actions" || field.key == "verification_random_actions") field.value = "0";
            if (field.key == "verification_actions" && !seen.count(field.key)) field.value = old_actions;
            if (field.key == "verification_allow_multiple" && !seen.count(field.key)) field.value = old_multiple;
        }
    }
    return parseSettings(fields);
}
void saveSettings(const std::string& file, const DemoSettings& settings) {
    const auto fields = settingsFields(settings);
    parseSettings(fields);
    std::ostringstream output;
    for (const auto& field : fields) output << field.key << ' ' << std::quoted(field.value) << '\n';
    atomicWrite(file, output.str());
}
}

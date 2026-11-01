#pragma once
#include "sdk_helpers.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <sstream>

namespace hiface_linux_demo {
#ifndef HIFACE_DEMO_VERSION
#define HIFACE_DEMO_VERSION HIFACE_DEMO_SDK_VERSION
#endif
inline constexpr const char* supportEmail = "FaceAISDK.Service@gmail.com";
inline constexpr const char* projectUrl = "https://github.com/FaceSDKPro";
struct RuntimeInfo {
    std::string app = "HelloFace Demo";
    std::string demo = HIFACE_DEMO_VERSION;
    std::string expected_sdk = HIFACE_DEMO_SDK_VERSION;
    std::string actual_sdk;
    bool sdk_loaded = true;
};
struct ErrorPresentation {
    std::string summary, advice, code;
    HiFaceStatus sdk_status = HIFACE_STATUS_OK;
    bool clock = false;
};
inline std::string deviceTime(bool utc = false) {
    const auto now = std::time(nullptr); std::tm time{}; char text[80]{};
    if (!(utc ? gmtime_r(&now, &time) : localtime_r(&now, &time))) return "Unavailable";
    std::strftime(text, sizeof(text), utc ? "%Y-%m-%d %H:%M:%S UTC" : "%Y-%m-%d %H:%M:%S %Z %z", &time);
    return text;
}
inline std::string sdkLabel(const RuntimeInfo& info) {
    // The build version is a declared fallback, never the loaded SDK version.
    // 编译期版本只作明确标注的预期值，不冒充实际加载的 SDK 版本。
    if (!info.sdk_loaded) return "SDK not loaded (expected " + info.expected_sdk + ")";
    if (info.actual_sdk.empty()) return "SDK version unavailable (expected " + info.expected_sdk + ")";
    return "SDK " + info.actual_sdk;
}
inline ErrorPresentation presentError(const std::string& raw, HiFaceStatus status = HIFACE_STATUS_OK,
                                      int system_error = 0, bool camera = false) {
    // Keep the original SDK code. Raw messages are classification inputs only;
    // diagnostics below never copy paths, identifiers or payloads from them.
    // 保留 SDK 原码；原文只用于分类，诊断不会复制其中的路径、标识或数据内容。
    if (status == HIFACE_STATUS_OK) for (int value = 1; value <= HIFACE_STATUS_LICENSE_CLOCK_INVALID; ++value) {
        const auto candidate = static_cast<HiFaceStatus>(value);
        if (raw.find(std::string(statusName(candidate)) + " (" + std::to_string(value) + ")") != std::string::npos) {
            status = candidate; break;
        }
    }
    ErrorPresentation result{"Operation failed.", "Retry or contact support.", "DEMO_ERROR", status};
    if (status != HIFACE_STATUS_OK) result.code = std::string(statusName(status)) + " (" + std::to_string(status) + ")";
    std::string lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    const auto has = [&](const char* value) { return lower.find(value) != std::string::npos; };
    if (status == HIFACE_STATUS_LICENSE_CLOCK_INVALID) {
        result.summary = "Check device time."; result.advice = "Correct the clock, then retry."; result.clock = true;
    } else if (status == HIFACE_STATUS_LICENSE_EXPIRED) {
        result.summary = "Authorization expired."; result.advice = "Contact support.";
    } else if (status >= HIFACE_STATUS_LICENSE_MISSING && status <= HIFACE_STATUS_LICENSE_FEATURE_DENIED) {
        result.summary = "Authorization unavailable."; result.advice = "Check the installation, then retry.";
        if (status == HIFACE_STATUS_LICENSE_FEATURE_DENIED) result.summary = "Feature unavailable.";
        if (status == HIFACE_STATUS_LICENSE_NOT_YET_VALID) { result.summary = "Authorization not yet valid."; result.advice = "Check the clock and authorization dates."; result.clock = true; }
    } else if (system_error == ENOSPC || has("no space left") || has("disk full")) {
        result.summary = "Storage full."; result.advice = "Free space, then retry.";
    } else if (system_error == EACCES || system_error == EPERM || has("permission denied") || has("access denied") || has("read-only file system")) {
        result.summary = "Access denied."; result.advice = "Check file and folder permissions.";
    } else if (status == HIFACE_STATUS_MODEL_LOAD_FAILED || has("missing model") || has("model file")) {
        result.summary = "Models unavailable."; result.advice = "Restore the complete SDK package.";
    } else if (status == HIFACE_STATUS_IMAGE_NO_FACE) {
        result.summary = "No face found."; result.advice = "Use a clear face photo.";
    } else if (status == HIFACE_STATUS_IMAGE_FACE_TOO_SMALL) {
        result.summary = "Face too small."; result.advice = "Move closer or use a clearer photo.";
    } else if (status == HIFACE_STATUS_ALREADY_EXISTS) {
        result.summary = "Face ID already exists."; result.advice = "Use another Face ID.";
    } else if (status == HIFACE_STATUS_NOT_FOUND) {
        result.summary = "Item not found."; result.advice = "Check the selection.";
    } else if (status == HIFACE_STATUS_INVALID_ARGUMENT) {
        result.summary = "Invalid input."; result.advice = "Check the supplied values.";
    } else if (status == HIFACE_STATUS_BUSY) {
        result.summary = "SDK busy."; result.advice = "Try again shortly.";
    } else if (status == HIFACE_STATUS_RESOURCE_EXHAUSTED) {
        result.summary = "Resources unavailable."; result.advice = "Check memory and configured limits.";
    } else if (has("corrupt") || has("malformed database")) {
        result.summary = "Database damaged."; result.advice = "Restore a trusted backup.";
    } else if (status == HIFACE_STATUS_DATABASE_ERROR || has("database locked")) {
        result.summary = "Database unavailable."; result.advice = "Check storage and permissions.";
    } else if (camera || has("camera") || has("v4l2")) {
        result.summary = "Camera unavailable."; result.advice = "Check the camera, then reconnect.";
    } else if (has("file") || has("directory") || has("image")) {
        result.summary = "Cannot read or write the file."; result.advice = "Check the file and permissions.";
    }
    if (system_error) {
        if (result.code == "DEMO_ERROR") result.code.clear(); else result.code += " / ";
        const char* name = system_error == EACCES ? "EACCES" : system_error == EPERM ? "EPERM" :
            system_error == ENOSPC ? "ENOSPC" : system_error == EROFS ? "EROFS" : system_error == ENOENT ? "ENOENT" : "ERROR";
        result.code += "OS " + std::string(name) + " (" + std::to_string(system_error) + ")";
    }
    return result;
}
inline std::string diagnosticText(const RuntimeInfo& info, const ErrorPresentation& error) {
    std::ostringstream out;
    out << info.app << "\nDemo " << info.demo << '\n' << sdkLabel(info)
        << "\nCode: " << error.code << "\nIssue: " << error.summary
        << "\nTime: " << deviceTime() << "\n" << supportEmail << "\n" << projectUrl;
    return out.str();
}
}

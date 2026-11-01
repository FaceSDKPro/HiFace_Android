#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
namespace hiface_linux_demo {
// One inverse transform for the completed scene (preview and overlays included)
// and pointer input. Width/height are the unrotated logical scene dimensions.
// 完整场景（含预览及分析叠加层）与指针输入共用逆变换；宽高为旋转前逻辑尺寸。
struct DisplayTransform {
    int rotation = 0, width = 0, height = 0;
    bool swapsAxes() const { return rotation == 90 || rotation == 270; }
    std::array<int, 9> inverseMatrix() const;
    std::pair<int, int> logicalPoint(int x, int y) const;
};
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect intersect(Rect other) const;
};
enum class Font { Body, Small, Title, Large, Caption };
struct Draw {
    enum Kind { Box, Text, Image, Line, Dot } kind = Box;
    Rect bounds, clip;
    uint32_t color = 0;
    int radius = 0;
    Font font = Font::Body;
    std::string text;
    int text_width = 0;
    const uint8_t* pixels = nullptr;
    int source_width = 0, source_height = 0;
    bool mirror = false, x11_bgrx = false;
    int line_width = 1;
};
// Shared pixels keep native and software cursors identical without a theme/font.
// 系统与软件光标共用像素，不依赖嵌入式系统的光标主题或字体。
struct MouseCursorBitmap {
    // Pad the arrow to a square so every rotation shares one X11 bitmap layout.
    static constexpr int arrow_width = 16, width = 24, height = 24, stride = (width + 7) / 8;
    int hotspot_x = 0, hotspot_y = 0;
    std::array<uint8_t, stride * height> source{}, mask{};
};
const MouseCursorBitmap& mouseCursorBitmap(int clockwise_rotation = 0);
struct Scene {
    int width = 0, height = 0;
    Rect clip;
    std::vector<Draw> draws;
    void box(Rect bounds, uint32_t color, int radius = 0);
    void text(int x, int baseline, std::string value, uint32_t color, Font font, int width);
    void image(Rect bounds, const uint8_t* pixels, int width, int height, bool mirror,
               bool x11_bgrx = false);
    void mouseCursor(int x, int y, int clockwise_rotation = 0);
    void line(int x1, int y1, int x2, int y2, uint32_t color, int width = 1);
    void dot(int x, int y, int size, uint32_t color);
    // Only contiguous commands with identical paint/clip state can share a native request.
    // 仅合并相邻且画笔、裁剪相同的命令，保持遮挡顺序；每批最多 256 项。
    size_t batchEnd(size_t begin) const;
};
// Exports the same drawing commands used by X11, for portable layout QA.
// Image buffers remain borrowed from the ViewState until serialization completes.
// 导出与 X11 相同的绘图命令，供跨平台布局检查；序列化结束前继续借用 ViewState 的图像缓冲。
void writeSceneSvg(const Scene& scene, const std::string& file);
}

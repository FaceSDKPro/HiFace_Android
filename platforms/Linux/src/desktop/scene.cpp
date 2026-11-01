#include "scene.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace hiface_linux_demo {
std::array<int, 9> DisplayTransform::inverseMatrix() const {
    switch (rotation) {
        case 0: return {1, 0, 0, 0, 1, 0, 0, 0, 1};
        case 90: return {0, 1, 0, -1, 0, height, 0, 0, 1};
        case 180: return {-1, 0, width, 0, -1, height, 0, 0, 1};
        case 270: return {0, -1, width, 1, 0, 0, 0, 0, 1};
        default: throw std::invalid_argument("UI rotation must be 0, 90, 180 or 270");
    }
}
std::pair<int, int> DisplayTransform::logicalPoint(int x, int y) const {
    const auto m = inverseMatrix();
    // Xrender maps pixel centres; event coordinates are integer pixel indices.
    // Xrender 映射像素中心；事件坐标为整数像素下标。
    return {m[0] * x + m[1] * y + m[2] - (m[0] < 0 || m[1] < 0 ? 1 : 0),
            m[3] * x + m[4] * y + m[5] - (m[3] < 0 || m[4] < 0 ? 1 : 0)};
}
Rect Rect::intersect(Rect b) const {
    const int left = std::max(x, b.x), top = std::max(y, b.y);
    return {left, top, std::max(0, std::min(x + w, b.x + b.w) - left), std::max(0, std::min(y + h, b.y + b.h) - top)};
}
void Scene::box(Rect bounds, uint32_t color, int radius) { Draw d; d.kind = Draw::Box; d.bounds = bounds; d.clip = clip; d.color = color; d.radius = radius; draws.push_back(std::move(d)); }
void Scene::line(int x1, int y1, int x2, int y2, uint32_t color, int width) {
    Draw d; d.kind = Draw::Line; d.bounds = {x1,y1,x2-x1,y2-y1}; d.clip = clip; d.color = color; d.line_width = std::max(1,width); draws.push_back(std::move(d));
}
void Scene::dot(int x, int y, int size, uint32_t color) {
    Draw d; d.kind = Draw::Dot; d.bounds = {x-size/2,y-size/2,size,size}; d.clip = clip; d.color = color; draws.push_back(std::move(d));
}
size_t Scene::batchEnd(size_t begin) const {
    const auto& first = draws.at(begin);
    if (first.kind != Draw::Line && first.kind != Draw::Dot && !(first.kind == Draw::Box && !first.radius)) return begin + 1;
    size_t end = begin + 1;
    for (; end < std::min(draws.size(), begin + 256); ++end) {
        const auto& next = draws[end];
        if (next.kind != first.kind || next.color != first.color || next.radius != first.radius || next.line_width != first.line_width ||
            next.clip.x != first.clip.x || next.clip.y != first.clip.y || next.clip.w != first.clip.w || next.clip.h != first.clip.h) break;
    }
    return end;
}
void Scene::text(int x, int y, std::string value, uint32_t color, Font font, int width) {
    Draw d; d.kind = Draw::Text; d.bounds = {x, y, width, 0}; d.clip = clip; d.text = std::move(value); d.color = color; d.font = font; d.text_width = width; draws.push_back(std::move(d));
}
void Scene::image(Rect bounds, const uint8_t* pixels, int width, int height, bool mirror, bool x11_bgrx) {
    Draw d; d.kind = Draw::Image; d.bounds = bounds; d.clip = clip; d.pixels = pixels; d.source_width = width; d.source_height = height; d.mirror = mirror; d.x11_bgrx = x11_bgrx; draws.push_back(std::move(d));
}
const MouseCursorBitmap& mouseCursorBitmap(int clockwise_rotation) {
    if (clockwise_rotation < 0 || clockwise_rotation > 270 || clockwise_rotation % 90)
        throw std::invalid_argument("Cursor rotation must be 0, 90, 180 or 270");
    static const std::array<MouseCursorBitmap, 4> bitmaps = [] {
        const char* rows[] = {
            "#...............", "##..............", "#o#.............", "#oo#............",
            "#ooo#...........", "#oooo#..........", "#ooooo#.........", "#oooooo#........",
            "#ooooooo#.......", "#oooooooo#......", "#ooooooooo#.....", "#oooooooooo#....",
            "#ooooooooooo#...", "#oooooooooooo#..", "#oooooooo#####..", "#oooo#ooo#......",
            "#ooo#.#ooo#.....", "#oo#..#ooo#.....", "#o#....#ooo#....", "##.....#ooo#....",
            "#.......#ooo#...", "........#ooo#...", ".........###....", "................"
        };
        std::array<MouseCursorBitmap, 4> result{};
        auto& base = result[0];
        base.hotspot_x = base.arrow_width - 1;
        for (int y = 0; y < base.height; ++y) for (int x = 0; x < base.arrow_width; ++x) {
            const size_t byte = y * base.stride + x / 8;
            const uint8_t bit = uint8_t(1u << (x % 8));
            const char pixel = rows[y][base.arrow_width - 1 - x];
            if (pixel != '.') base.mask[byte] |= bit;
            if (pixel == 'o') base.source[byte] |= bit;
        }
        for (size_t turn = 1; turn < result.size(); ++turn) {
            const auto& previous = result[turn - 1];
            auto& rotated = result[turn];
            rotated.hotspot_x = previous.height - 1 - previous.hotspot_y;
            rotated.hotspot_y = previous.hotspot_x;
            for (int y = 0; y < previous.height; ++y) for (int x = 0; x < previous.width; ++x) {
                const size_t from = y * previous.stride + x / 8;
                const uint8_t from_bit = uint8_t(1u << (x % 8));
                const int rx = previous.height - 1 - y, ry = x;
                const size_t to = ry * rotated.stride + rx / 8;
                const uint8_t to_bit = uint8_t(1u << (rx % 8));
                if (previous.mask[from] & from_bit) rotated.mask[to] |= to_bit;
                if (previous.source[from] & from_bit) rotated.source[to] |= to_bit;
            }
        }
        return result;
    }();
    return bitmaps[clockwise_rotation / 90];
}
void Scene::mouseCursor(int x, int y, int clockwise_rotation) {
    const auto& bitmap = mouseCursorBitmap(clockwise_rotation);
    x -= bitmap.hotspot_x;
    y -= bitmap.hotspot_y;
    const Rect viewport = clip.intersect({0, 0, width, height});
    for (int row = 0; row < bitmap.height; ++row) {
        auto pixel = [&](int column) {
            const size_t byte = row * bitmap.stride + column / 8;
            const uint8_t bit = uint8_t(1u << (column % 8));
            return !(bitmap.mask[byte] & bit) ? -1 : bitmap.source[byte] & bit ? 1 : 0;
        };
        for (int column = 0; column < bitmap.width;) {
            const int color = pixel(column), start = column++;
            while (column < bitmap.width && pixel(column) == color) ++column;
            if (color < 0) continue;
            const Rect bounds = Rect{x + start, y + row, column - start, 1}.intersect(viewport);
            if (bounds.w && bounds.h) box(bounds, color ? 0xffffff : 0x000000);
        }
    }
}
namespace {
std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) { if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '"') out += "&quot;"; else out += c; }
    return out;
}
std::string color(uint32_t rgb) { std::ostringstream out; out << '#' << std::hex << std::setw(6) << std::setfill('0') << rgb; return out.str(); }
void be32(std::vector<uint8_t>& out, uint32_t value) { for (int i = 24; i >= 0; i -= 8) out.push_back(uint8_t(value >> i)); }
uint32_t crc(const uint8_t* bytes, size_t count) {
    uint32_t sum = ~0u;
    for (size_t i = 0; i < count; ++i) { sum ^= bytes[i]; for (int bit = 0; bit < 8; ++bit) sum = (sum >> 1) ^ (0xedb88320u & (0u - (sum & 1u))); }
    return ~sum;
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, data.size()); const auto begin = out.size(); out.insert(out.end(), type, type + 4); out.insert(out.end(), data.begin(), data.end()); be32(out, crc(out.data() + begin, 4 + data.size()));
}
std::string png(const Draw& d) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < d.source_height; ++y) {
        raw.push_back(0);
        const auto* row = d.pixels + size_t(y) * d.source_width * 4;
        if (!d.x11_bgrx) raw.insert(raw.end(), row, row + size_t(d.source_width) * 4);
        else for (int x = 0; x < d.source_width; ++x) {
            const auto* pixel = row + size_t(x) * 4;
            raw.insert(raw.end(), {pixel[2], pixel[1], pixel[0], uint8_t(255)});
        }
    }
    std::vector<uint8_t> compressed{0x78, 0x01};
    for (size_t offset = 0; offset < raw.size();) {
        const size_t length = std::min<size_t>(65535, raw.size() - offset);
        compressed.push_back(offset + length == raw.size() ? 1 : 0);
        compressed.push_back(uint8_t(length)); compressed.push_back(uint8_t(length >> 8));
        compressed.push_back(uint8_t(~length)); compressed.push_back(uint8_t((~length) >> 8));
        compressed.insert(compressed.end(), raw.begin() + offset, raw.begin() + offset + length); offset += length;
    }
    uint32_t a = 1, b = 0; for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; } be32(compressed, (b << 16) | a);
    std::vector<uint8_t> bytes{137,80,78,71,13,10,26,10}, header;
    be32(header, d.source_width); be32(header, d.source_height); header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(bytes, "IHDR", header); chunk(bytes, "IDAT", compressed); chunk(bytes, "IEND", {});
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string encoded;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const auto value = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        encoded += alphabet[(value >> 18) & 63]; encoded += alphabet[(value >> 12) & 63]; encoded += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '='; encoded += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return encoded;
}
}
void writeSceneSvg(const Scene& scene, const std::string& file) {
    std::ofstream out(file);
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" << scene.width << "\" height=\"" << scene.height << "\" viewBox=\"0 0 " << scene.width << ' ' << scene.height << "\">\n";
    size_t index = 0;
    for (const auto& d : scene.draws) {
        const auto r = d.bounds, c = d.clip;
        out << "<defs><clipPath id=\"c" << index << "\"><rect x=\"" << c.x << "\" y=\"" << c.y << "\" width=\"" << c.w << "\" height=\"" << c.h << "\"/></clipPath></defs><g clip-path=\"url(#c" << index++ << ")\">";
        if (d.kind == Draw::Box) out << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << std::max(0, r.w) << "\" height=\"" << std::max(0, r.h) << "\" rx=\"" << d.radius << "\" fill=\"" << color(d.color) << "\"/>";
        else if (d.kind == Draw::Dot) out << "<circle cx=\"" << r.x+r.w/2.0 << "\" cy=\"" << r.y+r.h/2.0 << "\" r=\"" << r.w/2.0 << "\" fill=\"" << color(d.color) << "\"/>";
        else if (d.kind == Draw::Line) out << "<line x1=\"" << r.x << "\" y1=\"" << r.y << "\" x2=\"" << r.x+r.w << "\" y2=\"" << r.y+r.h << "\" stroke=\"" << color(d.color) << "\" stroke-width=\"" << d.line_width << "\" stroke-linecap=\"round\"/>";
        else if (d.kind == Draw::Text && !d.text.empty()) out << "<text x=\"" << r.x << "\" y=\"" << r.y << "\" font-family=\"Arial, sans-serif\" font-size=\"" << (d.font == Font::Title ? 22 : d.font == Font::Large ? 18 : d.font == Font::Caption ? 15 : d.font == Font::Small ? 12 : 14) << "\" fill=\"" << color(d.color) << "\" textLength=\"" << d.text_width << "\" lengthAdjust=\"spacingAndGlyphs\">" << escape(d.text) << "</text>";
        else if (d.kind == Draw::Image && d.pixels) {
            if (d.mirror) out << "<g transform=\"translate(" << 2 * r.x + r.w << " 0) scale(-1 1)\">";
            out << "<image x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.w << "\" height=\"" << r.h << "\" preserveAspectRatio=\"none\" xlink:href=\"data:image/png;base64," << png(d) << "\"/>";
            if (d.mirror) out << "</g>";
        }
        out << "</g>\n";
    }
    out << "</svg>\n"; out.close(); if (!out) throw std::runtime_error("Cannot write UI preview: " + file);
}
}

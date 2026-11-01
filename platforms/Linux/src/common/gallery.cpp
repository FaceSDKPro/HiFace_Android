#include "gallery.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <set>
#include <sstream>

namespace hiface_linux_demo {
namespace {
// Small bounded JSON reader for the public HF1 exchange format. It does not
// implement HF1 or touch the database schema; both remain owned by Core.
// 用于公开 HF1 交换格式的有界轻量 JSON 读取器；不实现 HF1，也不访问数据库结构，两者仍由 Core 管理。
class JsonReader {
    const std::string& input_;
    size_t cursor_ = 0;
    unsigned depth_ = 0;
    void space() { while (cursor_ < input_.size() && std::string(" \t\r\n").find(input_[cursor_]) != std::string::npos) ++cursor_; }
    uint32_t hex() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            require(cursor_ < input_.size(), "Truncated JSON escape");
            const auto c = input_[cursor_++];
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            require(digit >= 0, "Invalid JSON unicode escape"); value = value * 16 + digit;
        }
        return value;
    }
    static void utf8(std::string& output, uint32_t cp) {
        if (cp < 128) output += char(cp);
        else if (cp < 2048) { output += char(0xc0 | (cp >> 6)); output += char(0x80 | (cp & 63)); }
        else if (cp < 65536) { output += char(0xe0 | (cp >> 12)); output += char(0x80 | ((cp >> 6) & 63)); output += char(0x80 | (cp & 63)); }
        else { output += char(0xf0 | (cp >> 18)); output += char(0x80 | ((cp >> 12) & 63)); output += char(0x80 | ((cp >> 6) & 63)); output += char(0x80 | (cp & 63)); }
    }
public:
    explicit JsonReader(const std::string& input) : input_(input) {
        if (input_.compare(0, 3, "\xef\xbb\xbf") == 0) cursor_ = 3;
    }
    bool take(char c) { space(); if (cursor_ == input_.size() || input_[cursor_] != c) return false; ++cursor_; return true; }
    void expect(char c) { require(take(c), std::string("Expected JSON '") + c + "'"); }
    std::string string() {
        expect('"'); std::string value;
        while (cursor_ < input_.size()) {
            auto c = static_cast<unsigned char>(input_[cursor_++]);
            if (c == '"') return value;
            require(c >= 32 && value.size() < 16384, "Invalid or oversized JSON string");
            if (c != '\\') {
                value += char(c);
                if (c >= 128) {
                    const unsigned continuation = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
                    require(continuation != 0, "Invalid JSON UTF-8");
                    uint32_t cp = c & (continuation == 1 ? 31 : continuation == 2 ? 15 : 7);
                    for (unsigned i = 0; i < continuation; ++i) {
                        require(cursor_ < input_.size(), "Truncated JSON UTF-8");
                        const auto next = static_cast<unsigned char>(input_[cursor_++]);
                        require((next & 0xc0) == 0x80, "Invalid JSON UTF-8 continuation");
                        cp = (cp << 6) | (next & 63); value += char(next);
                    }
                    require(cp >= (continuation == 1 ? 128u : continuation == 2 ? 2048u : 65536u)
                        && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff), "Invalid JSON UTF-8 codepoint");
                }
                continue;
            }
            require(cursor_ < input_.size(), "Truncated JSON escape");
            c = input_[cursor_++];
            switch (c) {
                case '"': case '\\': case '/': value += char(c); break;
                case 'b': value += '\b'; break; case 'f': value += '\f'; break;
                case 'n': value += '\n'; break; case 'r': value += '\r'; break; case 't': value += '\t'; break;
                case 'u': {
                    auto cp = hex();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        require(cursor_ + 2 <= input_.size() && input_[cursor_] == '\\' && input_[cursor_ + 1] == 'u', "Missing JSON low surrogate");
                        cursor_ += 2; const auto low = hex();
                        require(low >= 0xdc00 && low <= 0xdfff, "Invalid JSON low surrogate");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else require(cp < 0xdc00 || cp > 0xdfff, "Unpaired JSON surrogate");
                    require(cp != 0, "JSON text must not contain NUL"); utf8(value, cp); break;
                }
                default: throw DemoError{"Invalid JSON escape"};
            }
        }
        throw DemoError{"Unterminated JSON string"};
    }
    std::string nullableString() {
        space();
        if (input_.compare(cursor_, 4, "null") == 0) { cursor_ += 4; return {}; }
        return string();
    }
    int64_t integer() {
        space(); const bool negative = take('-');
        require(cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9', "Expected JSON integer");
        const bool zero = input_[cursor_] == '0'; uint64_t value = 0; size_t digits = 0;
        while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9') {
            const unsigned digit = input_[cursor_++] - '0';
            require(++digits <= 19 && (!zero || digits == 1) && value <= (uint64_t(INT64_MAX) - digit) / 10, "Invalid JSON integer");
            value = value * 10 + digit;
        }
        return negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
    }
    template<class F> void object(F field) {
        require(++depth_ <= 16, "JSON nesting too deep"); expect('{');
        std::set<std::string> keys;
        if (!take('}')) do {
            const auto key = string(); require(keys.size() < 64 && keys.insert(key).second, "Too many or duplicate JSON fields");
            expect(':'); field(key);
            if (take('}')) break;
            expect(',');
        } while (true);
        --depth_;
    }
    template<class F> void array(F item) {
        require(++depth_ <= 16, "JSON nesting too deep"); expect('[');
        size_t count = 0;
        if (!take(']')) do {
            require(++count <= HIFACE_SEARCH_MAX_ENTRIES, "Too many JSON entries"); item();
            if (take(']')) break;
            expect(',');
        } while (true);
        --depth_;
    }
    void skip() {
        space(); require(cursor_ < input_.size(), "Truncated JSON value");
        if (input_[cursor_] == '"') { string(); return; }
        if (input_[cursor_] == '{') { object([&](const std::string&) { skip(); }); return; }
        if (input_[cursor_] == '[') { array([&] { skip(); }); return; }
        for (const char* literal : {"true", "false", "null"}) {
            const std::string token = literal;
            if (input_.compare(cursor_, token.size(), token) == 0) { cursor_ += token.size(); return; }
        }
        // Unknown JSON numeric metadata may be fractional or larger than int64.
        // 未知 JSON 数值元数据可能带小数或超出 int64 范围。
        const auto begin = cursor_;
        if (input_[cursor_] == '-') ++cursor_;
        require(cursor_ < input_.size(), "Truncated JSON number");
        if (input_[cursor_] == '0') ++cursor_;
        else {
            require(input_[cursor_] >= '1' && input_[cursor_] <= '9', "Invalid JSON value");
            do { ++cursor_; } while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9');
        }
        auto digits = [&] {
            const auto start = cursor_;
            while (cursor_ < input_.size() && input_[cursor_] >= '0' && input_[cursor_] <= '9') ++cursor_;
            require(cursor_ != start, "Invalid JSON number");
        };
        if (cursor_ < input_.size() && input_[cursor_] == '.') { ++cursor_; digits(); }
        if (cursor_ < input_.size() && (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            ++cursor_; if (cursor_ < input_.size() && (input_[cursor_] == '+' || input_[cursor_] == '-')) ++cursor_; digits();
        }
        require(cursor_ - begin <= 128, "JSON number too long");
    }
    void end() { space(); require(cursor_ == input_.size(), "Unexpected trailing JSON content"); }
};
}

std::string jsonQuote(const std::string& value) {
    std::string output = "\"";
    const char* digits = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { output += '\\'; output += char(c); }
        else if (c < 32) { output += "\\u00"; output += digits[c >> 4]; output += digits[c & 15]; }
        else output += char(c);
    }
    return output + '"';
}

std::vector<GalleryEntry> readResults(HiFaceSearchResultsHandle results) {
    std::vector<GalleryEntry> entries;
    for (uint32_t i = 0; i < hiface_search_results_count_v1(results); ++i) {
        HiFaceSearchEntryV1 row{}; row.struct_size = sizeof(row); float score = 0;
        check(hiface_search_results_get_v1(results, i, &row, &score), "read gallery entry");
        entries.push_back({row.face_id, row.feature_text ? std::string(row.feature_text, row.feature_text_length) : "",
            row.group ? row.group : "", row.tag ? row.tag : "", row.update_time_ms, score});
    }
    return entries;
}
std::vector<GalleryEntry> listGallery(Database& db, const std::string& group, const std::string& tag,
        uint32_t offset, uint32_t limit, bool features) {
    HiFaceSearchResultsHandle raw = nullptr;
    const auto fn = features ? hiface_search_list_features_v1 : hiface_search_list_v1;
    check(fn(db.handle, group.c_str(), tag.c_str(), offset, limit, &raw), "list gallery");
    SearchResults owned(raw, hiface_search_results_destroy_v1); return readResults(raw);
}
GalleryEntry getEntry(Database& db, const std::string& id) {
    HiFaceSearchResultsHandle raw = nullptr;
    check(hiface_search_get_v1(db.handle, id.c_str(), &raw), "get face ID");
    SearchResults owned(raw, hiface_search_results_destroy_v1);
    return readResults(raw).at(0);
}
void saveEntries(Database& db, const std::vector<GalleryEntry>& rows, size_t offset, size_t count, bool overwrite) {
    require(count && count <= HIFACE_SEARCH_MAX_BATCH_ENTRIES && offset <= rows.size() && count <= rows.size() - offset, "Invalid import batch");
    std::vector<HiFaceSearchFeatureEntryV1> entries; entries.reserve(count);
    for (size_t i = offset; i < offset + count; ++i) {
        const auto& row = rows[i];
        entries.push_back({sizeof(HiFaceSearchFeatureEntryV1), row.id.c_str(), row.tag.c_str(), row.group.c_str(), row.feature.c_str(),
            static_cast<uint32_t>(row.feature.size()), row.updated, HIFACE_FEATURE_SPACE_ID});
    }
    std::vector<const HiFaceSearchFeatureEntryV1*> pointers;
    for (const auto& row : entries) pointers.push_back(&row);
    check((overwrite ? hiface_search_upsert_v1 : hiface_search_insert_v1)(db.handle, pointers.data(), pointers.size()), "save gallery batch");
}
void saveEntry(Database& db, const GalleryEntry& row, bool overwrite) { saveEntries(db, {row}, 0, 1, overwrite); }

std::vector<GalleryEntry> readFeatureFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    require(file.is_open(), "Cannot open feature JSON: " + path);
    const auto length = file.tellg(); require(length > 0 && length <= 64 * 1024 * 1024, "Feature JSON must be 1 byte..64 MiB");
    std::string contents(static_cast<size_t>(length), '\0'); file.seekg(0);
    require(bool(file.read(contents.data(), contents.size())), "Cannot read feature JSON");
    JsonReader json(contents);
    int64_t version = -1, expected_count = -1; std::string format, space; bool found_entries = false;
    std::vector<GalleryEntry> entries;
    json.object([&](const std::string& key) {
        if (key == "schemaVersion") version = json.integer();
        else if (key == "featureFormat") format = json.string();
        else if (key == "featureSpaceId") space = json.string();
        else if (key == "entryCount") expected_count = json.integer();
        else if (key == "entries") {
            found_entries = true;
            json.array([&] {
                GalleryEntry row;
                json.object([&](const std::string& name) {
                    if (name == "faceId") row.id = json.string();
                    else if (name == "feature") row.feature = json.string();
                    else if (name == "group") row.group = json.nullableString();
                    else if (name == "tag") row.tag = json.nullableString();
                    else if (name == "updateTimeMillis") row.updated = json.integer();
                    else json.skip();
                });
                require(!row.id.empty() && row.id.size() <= HIFACE_SEARCH_MAX_TEXT_BYTES && row.id.find('\0') == std::string::npos
                    && row.group.size() <= HIFACE_SEARCH_MAX_TEXT_BYTES && row.tag.size() <= HIFACE_SEARCH_MAX_TEXT_BYTES
                    && row.feature.size() == HIFACE_FEATURE_TEXT_LENGTH && row.updated >= 0, "Invalid feature JSON entry");
                entries.push_back(std::move(row));
            });
        } else json.skip();
    });
    json.end();
    require(version == 1 && format == "HF1" && found_entries, "Feature JSON must use schemaVersion 1, HF1 and entries");
    require(space == HIFACE_FEATURE_SPACE_ID, "Feature space mismatch: expected " HIFACE_FEATURE_SPACE_ID);
    require(expected_count >= 0 && static_cast<uint64_t>(expected_count) == entries.size(), "Feature JSON entryCount does not match entries");
    return entries;
}

void exportFeatureFile(Database& db, const std::string& path, const std::string& group, const std::string& tag) {
    // The caller serializes database mutations for a consistent paged export.
    // 调用方串行修改数据库，以保持分页导出一致。
    std::ostringstream body; uint32_t total = 0;
    for (uint32_t offset = 0;;) {
        const auto rows = listGallery(db, group, tag, offset, 500, true);
        for (const auto& row : rows) {
            if (total++) body << ",\n";
            body << "    {\"faceId\":" << jsonQuote(row.id) << ",\"feature\":" << jsonQuote(row.feature)
                << ",\"group\":" << jsonQuote(row.group) << ",\"tag\":" << jsonQuote(row.tag)
                << ",\"updateTimeMillis\":" << row.updated << '}';
        }
        offset += rows.size(); if (rows.size() < 500) break;
    }
    atomicWrite(path, "{\n  \"schemaVersion\":1,\n  \"featureFormat\":\"HF1\",\n  \"featureSpaceId\":" + jsonQuote(HIFACE_FEATURE_SPACE_ID)
        + ",\n  \"entryCount\":" + std::to_string(total) + ",\n  \"entries\":[\n" + body.str() + "\n  ]\n}\n");
}
}

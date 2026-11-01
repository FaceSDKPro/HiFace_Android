#pragma once
#include "sdk_helpers.h"
#include "file_io.h"
#include <functional>
#include <memory>

namespace hiface_linux_demo {
struct GalleryEntry {
    std::string id, feature, group, tag;
    int64_t updated = 0;
    float similarity = 0;
};
using SearchResults = std::unique_ptr<HiFaceSearchResultsOpaque, decltype(&hiface_search_results_destroy_v1)>;
std::vector<GalleryEntry> readResults(HiFaceSearchResultsHandle results);
std::vector<GalleryEntry> listGallery(Database& db, const std::string& group, const std::string& tag,
    uint32_t offset, uint32_t limit, bool features = false);
GalleryEntry getEntry(Database& db, const std::string& id);
void saveEntries(Database& db, const std::vector<GalleryEntry>& entries, size_t offset, size_t count, bool overwrite);
void saveEntry(Database& db, const GalleryEntry& entry, bool overwrite = false);
std::vector<GalleryEntry> readFeatureFile(const std::string& path);
void exportFeatureFile(Database& db, const std::string& path, const std::string& group = {}, const std::string& tag = {});
std::string jsonQuote(const std::string& value);
}

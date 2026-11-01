#pragma once
#include "types.h"
#include "image_search.h"
namespace hiface_demo {
struct Update : hiface::FrameUpdate {
    std::string message;
    bool enrolling = false, saved = false;
};
bool searchFaceAccepted(const Update& update, const HiFaceSearchCameraFaceV1& face);
std::string searchFaceText(const Update& update, const HiFaceSearchCameraFaceV1& face);
std::string bestSearchMatchCaption(const Update& update, bool include_liveness);
std::string searchStatusText(const Update& update);
// Worker-thread workflow for SDK requests, enrollment identity, and persistence.
// 工作线程中的应用流程：SDK 请求、录入身份与持久化。
class DemoService {
public:
    explicit DemoService(const std::filesystem::path& database);
    void beginFlow(const FlowOptions& options);
    void beginEnrollment(const std::string& id, const std::string& group = {}, const std::string& tag = {});
    void cancelEnrollment();
    bool enrolling() const { return client_.flowActive(); }
    Update process(const CameraFrame& frame, const std::function<bool()>& is_current = {});
    Update detectDetailed(const Image& image, uint32_t maximum, bool detailed);
    std::optional<ImageSearchResult> searchImage(const Image& image, const std::function<bool()>& current = {}) {
        return searchPhoto(client_, database_, image, current);
    }
    void loadAllModels() { client_.loadAllModels(); }
    void setLiveness(bool enabled) { client_.setLiveness(enabled); }
    void configureSearch(const SearchOptions& options) { client_.configureSearch(options); }
    void pauseSearch(bool paused) { client_.pauseSearch(paused); }
    void releaseMemory() { database_.releaseMemory(); client_.pauseSearch(true); }
    void configureDetection(uint32_t maximum, bool detailed) { client_.configureDetection(maximum, detailed); }
    void useSearch(bool enabled) { client_.useSearch(enabled); }
    std::vector<HiFaceFaceV1> detect(const Image& image) { return client_.detect(image); }
    Feature capture(const Image& image) { return client_.capture(image); }
    CaptureResult captureWithPreview(const Image& image) { return client_.captureWithPreview(image); }
    CaptureResult captureEnrollmentImage(const Image& image);
    void insertFeature(const std::string& id, const Feature& feature) { client_.insertFeature(id, feature); }
    std::vector<std::pair<std::string, float>> query(const Feature& feature) { return client_.query(feature); }
    uint64_t count() const { return client_.count(); }
    HiFaceSessionHandle nativeHandle() const { return client_.nativeHandle(); }
private:
    hiface::Client client_;
    Gallery database_;
    std::string enrollment_id_, enrollment_group_, enrollment_tag_;
};
void runSelfTest();
void runDatasetSelfTest(const std::filesystem::path& dataset);
}

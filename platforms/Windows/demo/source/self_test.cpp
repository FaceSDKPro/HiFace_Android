#include "demo_service.h"
#include "data_exchange.h"
#include "enrollment_draft.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>

namespace hiface_demo {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path()
        / std::filesystem::u8path("HiFace Windows 测试 " + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}
void runSelfTest() {
    {
        Update update; update.searching = true;
        HiFaceSearchCameraFaceV1 face{};
        face.has_similarity = face.threshold_passed = 1; face.similarity = .9f;
        std::copy_n("person-001", 11, face.face_id);
        update.faces = {face};
        require(!searchFaceAccepted(update, face), "A candidate must not be presented as a confirmed identity");
        require(bestSearchMatchCaption(update, true).empty(), "Pending samples must not enter the result caption");
        update.matched = true; update.end_reason = HIFACE_SEARCH_END_MATCH_FOUND;
        require(searchFaceAccepted(update, face) && searchFaceText(update, face).find("person-001") != std::string::npos,
            "A completed match must identify the accepted person");
        update.faces.push_back(face); update.faces.back().threshold_passed = 0;
        require(!searchFaceAccepted(update, update.faces.back()) && searchStatusText(update).find("1 / 2") != std::string::npos,
            "M:N must report per-face acceptance, not mark every detected face matched");
        auto best = face;
        std::fill(std::begin(best.face_id), std::end(best.face_id), '\0');
        std::copy_n("best-person", 11, best.face_id); best.similarity = .95f;
        update.faces.push_back(best); update.has_liveness = true; update.liveness_score = .98f;
        require(bestSearchMatchCaption(update, true) == "best-person, Simi=0.95, Liveness=0.98",
            "Caption must select the highest confirmed match and keep both scores together");
        require(bestSearchMatchCaption(update, false) == "best-person, Simi=0.95",
            "Disabled liveness must not appear in the result caption");
        update.liveness_score = 0;
        require(bestSearchMatchCaption(update, true) == "best-person, Simi=0.95, Liveness=0.00",
            "A real zero liveness score must not disappear");
        update.end_reason = HIFACE_SEARCH_END_SPOOF_DETECTED;
        require(bestSearchMatchCaption(update, true).empty(), "Spoof rejection must not display a matched caption");
        require(!searchFaceAccepted(update, face) && searchFaceText(update, face).find(u8"不确认身份") != std::string::npos,
            "Liveness rejection must suppress identity confirmation despite a high similarity");
        update = {}; update.searching = true; update.search_progress = HIFACE_SEARCH_STAGE_EMPTY_GALLERY;
        require(searchStatusText(update).find(u8"范围") != std::string::npos, "Empty scoped gallery must explain the active range");
        update.search_progress = HIFACE_SEARCH_STAGE_FACE_TOO_SMALL;
        require(searchStatusText(update).find(u8"靠近") != std::string::npos, "Small-face progress must give actionable guidance");
        update.paused = true;
        require(searchStatusText(update).find(u8"暂停") != std::string::npos, "Paused search must not claim continued recognition");
        EnrollmentDraft::requireSingleFace(1);
        for (size_t count : {size_t{0}, size_t{2}, size_t{10}}) {
            bool rejected = false; try { EnrollmentDraft::requireSingleFace(count); } catch (const std::exception&) { rejected = true; }
            require(rejected, "Photo enrollment must reject zero or multiple faces");
        }
    }
    TemporaryDirectory temporary;
    const auto database = temporary.path / std::filesystem::u8path("人脸库.db");
    Feature feature{};
    {
        DemoService sdk(database);
        sdk.loadAllModels();
        require(sdk.count() == 0, "Expected empty new gallery");
        Image blank{112, 112, std::vector<uint8_t>(112 * 112 * 4, 128)};
        require(sdk.detect(blank).empty(), "Expected no face in blank image");
        const auto image_search = sdk.searchImage(blank);
        require(image_search && image_search->faces.empty() && image_search->matched == 0 && image_search->empty_library,
            "Empty image search must not fabricate faces or matches");
        require(!sdk.searchImage(blank, [] { return false; }), "Cancelled image search must not return a stale result");
        sdk.releaseMemory();
        require(sdk.searchImage(blank)->faces.empty(), "Released image search cache must rebuild without closing models");
        bool no_face = false;
        try { sdk.capture(blank); }
        catch (const SdkError& error) { no_face = error.status == HIFACE_STATUS_IMAGE_NO_FACE; }
        require(no_face, "Blank capture must report IMAGE_NO_FACE");
        no_face = false;
        try { sdk.captureEnrollmentImage(blank); }
        catch (const SdkError& error) { no_face = error.status == HIFACE_STATUS_IMAGE_NO_FACE; }
        require(no_face && sdk.count() == 0, "Photo enrollment must reject no-face images without creating a person");
        std::vector<uint8_t> pixels(112 * 112 * 3);
        uint32_t state = 17;
        for (auto& value : pixels) { state = state * 1664525u + 1013904223u; value = state >> 24; }
        const float points[] = {38.2946f, 51.6963f, 73.5318f, 51.5014f,
            56.0252f, 71.7366f, 41.5493f, 92.3655f, 70.7299f, 92.2041f};
        check(hiface_session_extract_feature_v1(sdk.nativeHandle(), pixels.data(), pixels.size(),
            112, 112, HIFACE_IMAGE_RGB, points, 10, feature.data(), feature.size()), "extract synthetic feature");
        double norm = 0;
        for (float value : feature) { require(std::isfinite(value), "Nonfinite feature"); norm += double(value) * value; }
        require(std::abs(norm - 1) < 1e-5, "Feature must be normalized");
        sdk.insertFeature(u8"测试人员_001", feature);
        bool duplicate = false;
        try { sdk.insertFeature(u8"测试人员_001", feature); }
        catch (const SdkError& error) { duplicate = error.status == HIFACE_STATUS_ALREADY_EXISTS; }
        require(duplicate && sdk.count() == 1, "Duplicate enrollment must preserve gallery");
        CameraFrame camera{112, 112, HIFACE_YUV_RANGE_VIDEO, HIFACE_YUV_MATRIX_BT601,
            std::vector<uint8_t>(112 * 112 * 3 / 2, 128)};
        require(sdk.process(camera).faces.empty(), "Expected no face in synthetic NV12");
        sdk.beginEnrollment("cancelled");
        require(sdk.enrolling(), "Guided enrollment did not start");
        require(sdk.process(camera).enrolling, "No-face frame should keep guided capture active");
        sdk.cancelEnrollment(); sdk.cancelEnrollment();
        require(!sdk.enrolling() && sdk.count() == 1, "Cancellation must not insert a person");
        sdk.beginEnrollment("closed-request");
        require(!sdk.process(camera, [] { return false; }).enrolling && sdk.count() == 1,
            "Closed request must release enrollment admission without writing a person");
        sdk.setLiveness(true); sdk.process(camera); sdk.setLiveness(false);
        auto capture_config = flowConfig(FlowOptions{});
        require(capture_config.frontal_hold_ms == HIFACE_CAPTURE_FRONTAL_HOLD_MS && capture_config.final_capture_timeout_ms == 0
            && capture_config.face_acquisition_timeout_ms == 0 && capture_config.action_count == 0, "Capture defaults drifted from Android");
        FlowOptions flow; flow.kind = FlowKind::Liveness;
        require(flowConfig(flow).mode == HIFACE_FLOW_ACTION_LIVENESS && flowConfig(flow).final_capture_timeout_ms == 3000,
            "Zero-action liveness must retain the Core silent-liveness final stage");
        sdk.useSearch(false); sdk.configureDetection(10, true);
        sdk.beginFlow(flow); require(sdk.process(camera).enrolling, "Silent liveness must accept camera frames"); sdk.cancelEnrollment();
        flow.actions = {HIFACE_ACTION_BLINK, HIFACE_ACTION_BLINK, HIFACE_ACTION_NOD_HEAD, HIFACE_ACTION_MOUTH_OPEN};
        sdk.beginFlow(flow); sdk.process(camera); sdk.cancelEnrollment();
        flow.kind = FlowKind::Verification; flow.reference = encodeFeature(feature);
        require(flowConfig(flow, &feature).mode == HIFACE_FLOW_FACE_AUTHENTICATION, "Verification with liveness mode mismatch");
        sdk.beginFlow(flow); sdk.process(camera); sdk.cancelEnrollment();
        flow.silent_liveness = false;
        require(flowConfig(flow, &feature).mode == HIFACE_FLOW_FACE_VERIFICATION, "Verification without liveness mode mismatch");
        sdk.beginFlow(flow); require(!sdk.process(camera, [] { return false; }).enrolling, "Cancelled verification stayed active");
        bool invalid = false;
        flow.actions.push_back(HIFACE_ACTION_BLINK);
        try { sdk.beginFlow(flow); } catch (const std::exception&) { invalid = true; }
        require(invalid && !sdk.enrolling(), "More than four actions must fail before acquiring the flow slot");
        SearchOptions search; search.multi_face = true;
        invalid = false; try { sdk.configureSearch(search); } catch (const std::exception&) { invalid = true; }
        require(invalid, "M:N with liveness must be rejected");
        search.liveness = false; search.max_faces = 5;
        sdk.configureSearch(search); sdk.useSearch(true); sdk.pauseSearch(true); sdk.process(camera);
        sdk.pauseSearch(false); sdk.process(camera);
        require(sdk.count() == 1, "Flow switches and pause/resume changed gallery records");
    }
    // Open a new Session/connection against the on-disk, Unicode path. This
    // exercises the persistence contract used by the camera Demo after restart.
    // 对磁盘上的 Unicode 路径重新打开 Session 和连接，验证相机 Demo 重启后使用的持久化契约。
    {
        DemoService reopened(database);
        const auto matches = reopened.query(feature);
        require(reopened.count() == 1 && matches.size() == 1 && matches[0].first == u8"测试人员_001"
            && matches[0].second > .999f, "Persisted HF1 self-match failed");
    }
    {
        Gallery gallery(temporary.path / std::filesystem::u8path("管理库.db"));
        const auto raw = encodeFeature(feature);
        require(compareFeatureText(raw, raw) > .999f, "HF1 comparison must use the shared Q7 decoder and cosine");
        gallery.save({{u8"人员😀", "group_A", "tag", raw, 100}}, false);
        require(gallery.get(u8"人员😀").feature == raw, "Gallery must return the original HF1 text");
        bool duplicate = false;
        try { gallery.save({{"should-rollback", "", "", raw, 100}, {u8"人员😀", "", "", raw, 100}}, false); }
        catch (const SdkError& error) { duplicate = error.status == HIFACE_STATUS_ALREADY_EXISTS; }
        require(duplicate && gallery.count() == 1, "Duplicate batch must roll back atomically");
        gallery.updateMetadata(u8"人员😀", "group_B", "");
        require(gallery.count("group_A") == 0 && gallery.count("group_B") == 1 && gallery.get(u8"人员😀").tag.empty(), "Metadata filter/clear mismatch");
        const auto interchange = temporary.path / std::filesystem::u8path("Android 互通.json");
        require(exportDataset(gallery, interchange) == 1, "Feature export count mismatch");
        Gallery imported(temporary.path / "imported.db");
        const auto imported_result = importDataset(imported, interchange);
        require(imported_result.saved == 1 && imported.get(u8"人员😀").feature == raw, "JSON roundtrip must preserve raw HF1");
        const auto fixture = temporary.path / "fixture.json";
        auto write = [&](const std::string& body) { std::ofstream file(fixture, std::ios::binary); file << body; };
        const std::string prefix = std::string("{\"schemaVersion\":1,\"featureFormat\":\"HF1\",\"featureSpaceId\":\"") + HIFACE_FEATURE_SPACE_ID + "\",\"entryCount\":";
        write(prefix + "2,\"entries\":[{\"faceId\":\"\\u6d4b\\u8bd5\\ud83d\\ude00\",\"feature\":\"" + raw + "\",\"group\":\"quoted\\\"group\",\"updateTimeMillis\":200},{\"faceId\":\"bad\",\"feature\":\"bad\"}]}");
        const auto escaped_result = importDataset(imported, fixture);
        require(escaped_result.saved == 1 && escaped_result.failed == 1 && imported.get(u8"测试😀").group == "quoted\"group", "Escaped Unicode import or invalid-row accounting failed");
        require(imported.list(1, 1).size() == 1 && imported.list(0, 1, "", "", false, true)[0].id == u8"人员😀", "Pagination/recent order mismatch");
        write("{\"schemaVersion\":1,\"featureFormat\":\"HF1\",\"featureSpaceId\":\"wrong-space\",\"entryCount\":0,\"entries\":[]}");
        bool rejected = false; try { importDataset(imported, fixture); } catch (const std::exception&) { rejected = true; }
        require(rejected && imported.count() == 2, "Wrong feature space must fail without changing records");
        write("{\"schemaVersion\":1,\"schemaVersion\":1}");
        rejected = false; try { importDataset(imported, fixture); } catch (const std::exception&) { rejected = true; }
        require(rejected && imported.count() == 2, "Duplicate JSON keys must be rejected");
        std::ofstream large(fixture, std::ios::binary); large << prefix << "1002,\"entries\":[";
        for (int i = 0; i < 1002; ++i) {
            if (i) large << ',';
            large << "{\"faceId\":\"batch-" << i << "\",\"feature\":\"" << raw << "\"}";
        }
        large << "]}"; large.close();
        const auto stopped = importDataset(imported, fixture, [](const ImportProgress& progress) { return progress.processed < 1001; });
        require(stopped.stopped && stopped.saved == 1001 && imported.count() == 1003, "Stopped import must preserve completed rows across batch boundaries");
        require(imported.remove(u8"人员😀") == 1 && imported.remove(u8"人员😀") == 0, "Idempotent deletion mismatch");
        imported.releaseMemory(); require(imported.get(u8"测试😀").feature == raw, "Cache release changed persisted feature");
    }
    {
        Gallery gallery(temporary.path / "confirmed-enrollment.db");
        EnrollmentDraft draft;
        FlowResult result; result.feature = encodeFeature(feature);
        result.preview = {2, 2, std::vector<uint8_t>(16, 128)};
        result.metadata.outcome = HIFACE_FLOW_OUTCOME_SPOOF_DETECTED;
        bool rejected = false; try { draft.stage(result); } catch (const std::exception&) { rejected = true; }
        require(rejected && !draft.pending() && gallery.count() == 0, "Rejected capture must not admit enrollment");
        result.metadata.outcome = HIFACE_FLOW_OUTCOME_PASSED; draft.stage(result);
        require(draft.pending() && gallery.count() == 0, "A capture must wait for explicit confirmation");
        rejected = false; try { draft.save(gallery, " ", "", ""); } catch (const std::exception&) { rejected = true; }
        require(rejected && draft.pending() && gallery.count() == 0, "Invalid ID must retain the draft without writing");
        for (const auto& id : {std::string(1025, 'x'), std::string("bad\0id", 6)}) {
            rejected = false; try { draft.save(gallery, id, "", ""); } catch (const std::exception&) { rejected = true; }
            require(rejected && draft.pending() && gallery.count() == 0, "Invalid metadata must preserve the editable capture and not write");
        }
        draft.save(gallery, "confirmed", "group", "tag");
        require(!draft.pending() && gallery.count() == 1 && gallery.get("confirmed").group == "group", "Confirmation must save exactly once");
        draft.stage(result); rejected = false;
        try { draft.save(gallery, "confirmed", "other", ""); } catch (const SdkError& error) { rejected = error.status == HIFACE_STATUS_ALREADY_EXISTS; }
        require(rejected && draft.pending() && gallery.get("confirmed").group == "group", "Duplicate ID must preserve both record and editable draft");
        draft.save(gallery, u8"修正后的人员", u8"中文分组", u8"标签");
        require(!draft.pending() && gallery.count() == 2 && gallery.get(u8"修正后的人员").tag == u8"标签",
            "A duplicate failure must allow saving the same draft with a corrected Unicode ID");
        draft.stage(result);
        draft.clear(); require(!draft.pending() && gallery.count() == 2, "Cancel or leave must discard the unsaved capture");
        rejected = false; try { draft.save(gallery, "never-captured", "", ""); } catch (const std::exception&) { rejected = true; }
        require(rejected && gallery.count() == 2, "An empty draft must never be committed");
        CaptureResult photo; photo.text = result.feature; photo.preview = result.preview;
        draft.stage(photo); require(draft.pending() && gallery.count() == 2, "Photo enrollment must also wait for confirmation");
        draft.clear();
    }
    std::cout << "Self-test passed: explicit enrollment confirmation/cancel/duplicate protection, five real CPU models, RGBA/NV12, guided-capture cancellation, "
        "four-action/silent liveness and verification modes, M:N settings/pause/resume, HF1, duplicate rollback, "
        "Unicode SQLite persistence, gallery CRUD/filter/page and Android JSON interchange/cancel. Synthetic pixels only.\n";
}
void runDatasetSelfTest(const std::filesystem::path& dataset) {
    TemporaryDirectory temporary;
    Gallery gallery(temporary.path / "android-dataset.db");
    const auto result = importDataset(gallery, dataset);
    require(result.saved == 10000 && result.failed == 0 && gallery.count() == 10000, "Android 10000-entry feature import failed");
    const auto first = gallery.get("0");
    require(first.group == "group_test", "Android dataset metadata differs");
    const auto exported = temporary.path / "roundtrip.json";
    require(exportDataset(gallery, exported) == 10000, "Android dataset export count differs");
    Gallery reopened(temporary.path / "roundtrip.db");
    require(importDataset(reopened, exported).saved == 10000 && reopened.get("0").feature == first.feature,
        "Android dataset raw HF1 roundtrip failed");
    std::cout << "Android dataset self-test passed: 10000 HF1 entries, metadata and raw-text roundtrip.\n";
}
}

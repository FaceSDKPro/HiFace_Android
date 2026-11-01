#pragma once
#include "app.h"
#include "scene.h"
#include "navigation.h"
#include <functional>
#include <map>
namespace hiface_linux_demo {
struct Control {
    Rect bounds;
    std::string action, field;
    bool enabled = true;
    size_t text_start = 0;
    std::string label;
    std::string id() const { return field.empty() ? action : "field:" + field; }
};
enum class Key { Text, Escape, Enter, Backspace, Delete, Left, Right, Up, Down, Home, End, PreviousPage, NextPage, Tab, SelectAll, Page1, Page2, Page3, Page4, Page5 };
class DemoUi {
public:
    using Measure = std::function<int(const std::string&, Font)>;
    explicit DemoUi(Measure measure = {});
    const Scene& render(const ViewState& state, int width, int height);
    void click(int x, int y, const ViewState& state);
    void key(Key key, const std::string& text, bool shift, const ViewState& state);
    void scroll(int steps, int x, int y, const ViewState& state);
    void drag(int x, int y, const ViewState& state);
    void releasePointer() { library_drag_active_ = false; }
    void pointer(int x, int y) { pointer_x_ = x; pointer_y_ = y; }
    void paste(const std::string& text, const std::string& field);
    std::string selectedText() const;
    const std::string& focusedField() const { return focused_; }
    std::vector<Command> takeCommands();
    bool open() const { return running_; }
    void requestClose(const ViewState& state);
    void pageForSmoke(unsigned index);
    void reportError(const std::string& message) { notice_ = message; }
    void setSoftKeyboardEnabled(bool enabled) { soft_keyboard_enabled_ = enabled; if (!enabled) soft_keyboard_open_ = false; }
    void rejectSettings(const std::string& message) { settings_pending_ = false; reportError(message); }
    const std::vector<Control>& controls() const { return controls_; }
private:
    void text(int x, int y, std::string value, uint32_t color, int max_width, Font font = Font::Body);
    int wrap(int x, int y, const std::string& value, uint32_t color, int width, int max_lines = 2, Font font = Font::Body);
    void card(Rect rect);
    void button(Rect rect, const std::string& label, const std::string& action, int style = 0, bool enabled = true, Font font = Font::Body);
    void field(Rect rect, const std::string& key, const std::string& label, const std::string& initial = "", bool enabled = true);
    void toggle(int x, int y, int width, const std::string& key, const std::string& label, bool enabled = true);
    void picture(Rect rect, const Image* image, const CameraFrame* camera, bool mirror, const std::vector<Box>& boxes);
    void empty(Rect rect, const std::string& title, const std::string& hint);
    void home(const ViewState& state, Rect body);
    void startupFailure(const ViewState& state);
    void camera(const ViewState& state, Rect body);
    void searchResults(const ViewState& state, Rect panel);
    void photos(const ViewState& state, Rect body);
    void imageSearch(const ViewState& state, Rect body);
    void library(const ViewState& state, Rect body);
    void references(const ViewState& state, Rect body);
    void settings(const ViewState& state, Rect body);
    void modal(const ViewState& state);
    void browser();
    void handle(const std::string& action, const ViewState& state);
    void emit(std::string type, std::vector<std::string> values = {});
    void page(const std::string& next, const ViewState& state, bool force = false);
    void back(const ViewState& state);
    void pageHeader(const ViewState& state, const std::string& title, bool camera);
    void selectImageFace(int index, const ViewState& state);
    void clearLibrarySelection();
    void loadMoreGallery(const ViewState& state);
    void softKeyboard();
    void softKeyboardKey(const std::string& action, const ViewState& state);
    void submitField(const ViewState& state);
    void beginOverlay();
    void dismissOverlay();
    void confirm(Command pending, const std::string& title, const std::string& message);
    void browse(const std::string& target);
    void readDirectory();
    bool settingsDirty(const ViewState& state) const;
    bool settingVisible(const std::string& key) const;
    void resetDraft(const DemoSettings& settings);
    void edit(const std::string& text, const std::string& field);
    Scene scene_;
    Measure measure_;
    std::vector<Control> controls_;
    std::vector<Command> commands_;
    std::map<std::string, std::string> values_;
    std::string page_ = "home", settings_tab_ = "detection", library_tab_ = "people", selected_task_ = "S";
    std::vector<std::string> page_history_;
    std::string focused_, keyboard_focus_, notice_, invalid_field_;
    int home_scroll_ = 0, home_max_scroll_ = 0;
    int gallery_rows_ = 10;
    int library_scroll_ = 0;
    Rect library_area_;
    bool library_more_pending_ = false, library_clear_pending_ = false;
    bool library_drag_active_ = false;
    int library_drag_y_ = 0;
    uint64_t library_attempt_ = 0, library_revision_ = 0;
    bool metadata_dialog_ = false, metadata_pending_ = false, metadata_error_ = false;
    uint64_t metadata_attempt_ = 0;
    uint64_t enrollment_generation_ = 0;
    uint64_t saved_capture_generation_ = 0, flow_result_generation_ = 0;
    int reference_offset_ = 0;
    bool result_dialog_ = false, replacement_declined_ = false;
    bool sdk_advanced_ = false, search_advanced_ = false;
    bool library_menu_ = false;
    bool soft_keyboard_enabled_ = false, soft_keyboard_open_ = false;
    bool soft_keyboard_upper_ = false, soft_keyboard_symbols_ = false;
    std::string soft_keyboard_target_, soft_keyboard_label_;
    bool enrollment_saving_ = false;
    uint64_t enrollment_attempt_ = 0;
    int selected_image_face_ = 0, image_face_offset_ = 0, image_face_rows_ = 3;
    Rect image_result_area_;
    uint64_t image_search_generation_ = 0;
    int pointer_x_ = -1, pointer_y_ = -1;
    size_t cursor_ = 0;
    bool running_ = true, initialized_ = false, select_all_ = false, content_enabled_ = true;
    bool confirming_ = false, browsing_ = false, enrolling_ = false;
    bool settings_pending_ = false, camera_menu_ = false;
    uint64_t settings_attempt_ = 0;
    Command pending_;
    std::string overlay_return_focus_, browser_error_;
    std::string confirm_title_, confirm_message_, browser_target_, browser_directory_;
    int browser_offset_ = 0;
    std::vector<std::filesystem::directory_entry> browser_entries_;
};
}

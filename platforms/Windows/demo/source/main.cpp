#include "app.h"
#include "camera_preferences.h"
#include "win_util.h"
#include "theme.h"
#include "image_search_view.h"
#include "enrollment_draft.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <utility>
#include <windowsx.h>

namespace {
using namespace hiface_demo;
std::wstring aboutTitle() {
    const bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    return std::wstring(chinese ? L"关于 HelloFace SDK " : L"About HelloFace SDK ") + widen(HIFACE_DEMO_SDK_VERSION);
}
enum Control {
    Device = 100, RefreshDevices, Open, Close, Mode, Resolution, Rotation, Mirror, Apply, Logs, Help,
    PersonId, Group, Tag, FilterGroup, FilterTag, MaxFaces, Details, SearchMode, Silent, Threshold, Interval, Timeout,
    Actions, CustomActions, Multiple, StartFlow, Enroll, Cancel, Pause, PickA, PickB, Compare,
    ImageReference, GalleryReference, ImportReference, ExportFeature, ExportPreview, ImageEnroll,
    RefreshGallery, PreviousPage, NextPage, DeletePerson, UpdatePerson, ImportJson, ExportJson,
    ImportImages, PickSearchImage, SearchImageAgain, GoGallery, SearchGuide, SaveEnrollment, ReleaseMemory, People, Status, Metrics, Primary,
    SearchResults, SearchCaption, CopySearch, Back = kBackControl, Parameters = kParametersControl, HomePrevious = kHomePreviousControl, HomeNext = kHomeNextControl,
    ParameterPane = 5000, NavBase = 6000, HomeBase = 7000
};
constexpr int kLabelOffset = 1000;
RECT workingArea(HWND window) {
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor)) return monitor.rcWork;
    RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0); return area;
}
struct Ui {
    struct Draft {
        std::wstring max_faces = L"3", threshold = L"0.80", actions = L"2,1,0", interval = L"0", timeout = L"3000";
        std::wstring id = L"person-001", group, tag, filter_group, filter_tag;
        int action_preset = 1, search_mode = 0;
        bool silent = true, multiple = true, detailed = true;
    };
    App app;
    Theme theme;
    ImageSearchView image_search;
    HWND pane = nullptr;
    RECT canvas{}, parameters{};
    int scroll = 0, content_height = 0, view_height = 0;
    uint64_t pending_command = 0, search_request_generation = 0;
    bool creating = true, dirty = false, rendering_list = false;
    Page page = Page::Home;
    PageHistory history;
    bool parameters_open = false;
    int home_scroll = 0;
    uint64_t return_after_save = 0, return_after_reference = 0;
    unsigned initial_camera = 0;
    uint64_t rendered_gallery = UINT64_MAX;
    uint32_t rendered_offset = UINT32_MAX;
    std::vector<Person> rendered_people;
    std::array<Draft, kPageCount> drafts;
    std::wstring parameter_notice;
    std::wstring last_search_results;
    int card_top = 160, main_left = 24, parameter_width = 296;
};
HWND child(HWND window, int id) {
    if (const auto direct = GetDlgItem(window, id)) return direct;
    return GetDlgItem(GetDlgItem(window, ParameterPane), id);
}
LRESULT send(HWND window, int id, UINT message, WPARAM wparam, LPARAM lparam) { return SendMessageW(child(window, id), message, wparam, lparam); }
std::wstring text(HWND window, int id) {
    HWND control = child(window, id);
    const auto length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1); value.resize(length); return value;
}
void setText(HWND window, int id, const std::wstring& value) { SetWindowTextW(child(window, id), value.c_str()); }
bool checked(HWND window, int id) { return send(window, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void checkBox(HWND window, int id, bool value) { send(window, id, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); }
int selection(HWND window, int id) { return static_cast<int>(send(window, id, CB_GETCURSEL, 0, 0)); }
unsigned integer(HWND window, int id, unsigned minimum, unsigned maximum) {
    try {
        const auto value = text(window, id); size_t end = 0;
        const auto parsed = std::stoul(value, &end);
        if (end == value.size() && parsed >= minimum && parsed <= maximum) return static_cast<unsigned>(parsed);
    } catch (const std::exception&) {}
    throw std::runtime_error(utf8(text(window, id + kLabelOffset) + L"：请输入 " + std::to_wstring(minimum) + L"–" + std::to_wstring(maximum) + L" 范围内的整数。"));
}
float threshold(HWND window) {
    try {
        const auto value = text(window, Threshold); size_t end = 0;
        const float parsed = std::stof(value, &end);
        if (end == value.size() && std::isfinite(parsed) && parsed >= 0 && parsed <= 1) return parsed;
    } catch (const std::exception&) {}
    throw std::runtime_error(u8"相似度阈值：请输入 0–1 范围内的数字。");
}
HWND control(Ui& ui, const wchar_t* type, const wchar_t* label, int id, DWORD style = 0) {
    if (std::wstring(type) == L"STATIC") style |= SS_NOPREFIX;
    HWND value = CreateWindowExW(std::wstring(type) == L"EDIT" || std::wstring(type) == L"LISTBOX" ? WS_EX_CLIENTEDGE : 0,
        type, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, ui.app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (!value) throw std::runtime_error("Cannot create Demo controls");
    ui.theme.style(value); return value;
}
void field(Ui& ui, int id, const wchar_t* label, const wchar_t* initial) {
    control(ui, L"STATIC", label, id + kLabelOffset);
    const auto edit = control(ui, L"EDIT", initial, id, ES_AUTOHSCROLL | WS_TABSTOP);
    SendMessageW(edit, EM_SETLIMITTEXT, id == PersonId || id == Group || id == Tag || id == FilterGroup || id == FilterTag ? 256 : 32, 0);
}
void combo(Ui& ui, int id, std::initializer_list<const wchar_t*> items) {
    const auto box = control(ui, L"COMBOBOX", L"", id, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    for (const auto item : items) SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(box, CB_SETCURSEL, 0, 0);
}
void button(Ui& ui, int id, const wchar_t* label) { control(ui, L"BUTTON", label, id, BS_OWNERDRAW | WS_TABSTOP); }
std::vector<std::filesystem::path> choose(HWND owner, bool save, bool multiple, const wchar_t* filter, const wchar_t* extension = nullptr) {
    std::vector<wchar_t> buffer(65536, 0);
    const auto local = executableDirectory() / L"data";
    const auto directory = std::filesystem::is_directory(local) ? local : executableDirectory().parent_path() / L"data";
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter; dialog.lpstrFile = buffer.data(); dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = extension; dialog.lpstrInitialDir = directory.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST)
        | (multiple ? OFN_ALLOWMULTISELECT : 0);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        if (CommDlgExtendedError()) throw std::runtime_error("Windows file picker failed; choose fewer files at once");
        return {};
    }
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path first(buffer.data());
    const wchar_t* next = buffer.data() + wcslen(buffer.data()) + 1;
    if (!multiple || !*next) paths.push_back(first);
    else while (*next) { paths.push_back(first / next); next += wcslen(next) + 1; }
    return paths;
}
constexpr wchar_t kImages[] = L"Images (JPEG/PNG/BMP/TIFF)\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
constexpr wchar_t kJson[] = L"HiFace HF1 dataset (*.json)\0*.json\0";
constexpr wchar_t kFeature[] = L"HiFace HF1 feature (*.hf1;*.txt)\0*.hf1;*.txt\0";
Settings readSettings(Ui& ui) {
    const auto window = ui.app.window;
    Settings settings; settings.page = ui.page;
    settings.id = utf8(text(window, PersonId)); settings.group = utf8(text(window, Group)); settings.tag = utf8(text(window, Tag));
    settings.filter_group = utf8(text(window, FilterGroup)); settings.filter_tag = utf8(text(window, FilterTag));
    const int device = selection(window, Device);
    if (device >= 0 && static_cast<size_t>(device) < ui.app.cameras.size()) {
        settings.camera_index = static_cast<unsigned>(device); settings.camera_link = ui.app.cameras[static_cast<size_t>(device)].symbolic_link;
    }
    const int resolution = selection(window, Resolution);
    if (resolution == 1) { settings.width = 1280; settings.height = 720; }
    else if (resolution == 2) { settings.width = 1920; settings.height = 1080; }
    settings.rotation = std::max(0, selection(window, Rotation)) * 90; settings.mirror = checked(window, Mirror);
    settings.detailed = checked(window, Details);
    if (ui.page == Page::Detection) settings.detection_max = integer(window, MaxFaces, 1, 10);
    settings.flow.kind = ui.page == Page::Liveness ? FlowKind::Liveness : ui.page == Page::Verification ? FlowKind::Verification : FlowKind::Capture;
    settings.flow.silent_liveness = checked(window, Silent);
    settings.flow.allow_multiple = (ui.page == Page::Verification || ui.page == Page::Liveness) && checked(window, Multiple);
    if (ui.page == Page::Verification || ui.page == Page::Comparison) settings.flow.threshold = threshold(window);
    if (ui.page == Page::Verification || ui.page == Page::Liveness) {
        const int preset = selection(window, Actions);
        if (preset == 1) {
            settings.flow.actions = {0, 1, 2, 3};
            std::mt19937 random(GetTickCount()); std::shuffle(settings.flow.actions.begin(), settings.flow.actions.end(), random);
            settings.flow.actions.resize(3);
        } else if (preset >= 2 && preset <= 5) settings.flow.actions = {preset == 2 ? HIFACE_ACTION_BLINK : preset == 3 ? HIFACE_ACTION_NOD_HEAD : preset == 4 ? HIFACE_ACTION_SHAKE_HEAD : HIFACE_ACTION_MOUTH_OPEN};
        else if (preset == 6) settings.flow.actions = {HIFACE_ACTION_BLINK, HIFACE_ACTION_NOD_HEAD};
        else if (preset == 7) {
            std::wistringstream values(text(window, CustomActions)); std::wstring token;
            while (std::getline(values, token, L',')) {
                size_t end = 0; auto action = std::stoi(token, &end);
                if (end != token.size() || action < 0 || action > 3 || settings.flow.actions.size() >= 4)
                    throw std::runtime_error("Custom actions: 0=shake, 1=nod, 2=blink, 3=mouth; at most four comma-separated steps");
                settings.flow.actions.push_back(action);
            }
        }
    }
    if (ui.page == Page::Search) {
        settings.search.multi_face = selection(window, SearchMode) == 1;
        settings.search.liveness = !settings.search.multi_face && checked(window, Silent);
        settings.search.max_faces = settings.search.multi_face ? integer(window, MaxFaces, 1, 5) : 1;
        settings.search.threshold = threshold(window);
        settings.search.interval_ms = integer(window, Interval, 0, 3000); settings.search.timeout_ms = integer(window, Timeout, 1000, 3000);
        settings.search.group = settings.group; settings.search.tag = settings.tag;
        if (settings.search.threshold < .75f || settings.search.threshold > .85f) throw std::runtime_error(u8"搜索相似度阈值：请输入 0.75–0.85 范围内的数字。");
    }
    return settings;
}
void submit(Ui& ui, Action action, std::vector<std::filesystem::path> files = {}) {
    const bool independent = action == Action::Cancel || action == Action::CloseCamera || action == Action::Pause
        || action == Action::PreviousPage || action == Action::NextPage || action == Action::ReleaseMemory
        || action == Action::ExportPreview || action == Action::ExportFeature;
    auto settings = independent ? ui.app.current()->settings : readSettings(ui);
    if (action == Action::Pause && !ui.app.current()->paused && ui.dirty)
        ui.parameter_notice = L"搜索已暂停；修改后的参数仍需点击应用修改。";
    if (action == Action::Apply || action == Action::OpenCamera)
        saveCameraPreferences(demoDataDirectory() / L"camera-settings.txt", {settings.width, settings.height, settings.rotation, settings.mirror});
    ui.pending_command = ui.app.enqueue({action, std::move(settings), std::move(files), 0});
    if (action != Action::Pause) ui.parameter_notice.clear();
    if (action == Action::Apply || action == Action::OpenCamera) ui.dirty = false;
}
void refreshDevices(Ui& ui) {
    const auto index = selection(ui.app.window, Device);
    const auto old = index >= 0 && static_cast<size_t>(index) < ui.app.cameras.size() ? ui.app.cameras[static_cast<size_t>(index)].symbolic_link : std::wstring();
    ui.app.cameras = Camera::devices(); const auto box = child(ui.app.window, Device);
    SendMessageW(box, CB_RESETCONTENT, 0, 0);
    size_t selected = ui.app.cameras.empty() ? 0 : std::min(static_cast<size_t>(ui.initial_camera), ui.app.cameras.size() - 1);
    for (size_t i = 0; i < ui.app.cameras.size(); ++i) {
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ui.app.cameras[i].name.c_str()));
        if (ui.app.cameras[i].symbolic_link == old) selected = i;
    }
    if (!ui.app.cameras.empty()) SendMessageW(box, CB_SETCURSEL, selected, 0);
    writeLog("Enumerated cameras=" + std::to_string(ui.app.cameras.size()));
}
int primaryAction(Page page) {
    switch (page) {
        case Page::Reference: case Page::Verification: case Page::Liveness: case Page::Enrollment: return StartFlow;
        case Page::Search: return Pause;
        case Page::ImageSearch: return PickSearchImage;
        case Page::Comparison: return Compare;
        case Page::Gallery: return RefreshGallery;
        default: return PickA;
    }
}
const wchar_t* primaryTitle(Page page) {
    switch (page) {
        case Page::Reference: return L"开始采集基准";
        case Page::Verification: return L"开始 1:1 验证";
        case Page::Liveness: return L"开始活体检测";
        case Page::Search: return L"暂停搜索";
        case Page::Enrollment: return L"开始 / 重新采集";
        case Page::ImageSearch: return L"选择图片并搜索";
        case Page::Comparison: return L"开始图片比对";
        case Page::Gallery: return L"筛选并刷新人脸库";
        default: return L"选择图片并检测";
    }
}
HomeLayout homeGeometry(const Ui& ui) {
    RECT client{}; GetClientRect(ui.app.window,&client);
    return {MulDiv(client.right,96,ui.theme.dpi),MulDiv(client.bottom,96,ui.theme.dpi),kFeatureCount};
}
void layout(Ui& ui) {
    if (!ui.pane) return;
    const auto window=ui.app.window; RECT client{};GetClientRect(window,&client);
    const int width=MulDiv(client.right,96,ui.theme.dpi),height=MulDiv(client.bottom,96,ui.theme.dpi);
    auto px=[&](int value){return ui.theme.px(value);};
    auto rect=[&](int x,int y,int w,int h){return RECT{px(x),px(y),px(x+w),px(y+h)};};
    auto move=[&](int id,int x,int y,int w,int h=34){MoveWindow(child(window,id),px(x),px(y),px(w),px(h),TRUE);};
    ui.main_left=24;ui.parameter_width=width<1100?260:296;
    const bool home=ui.page==Page::Home,camera_settings=ui.page==Page::CameraSettings;
    const bool pane_visible=!home&&!camera_settings&&ui.page!=Page::ImageSearch&&(!hasFeatureParameters(ui.page)||ui.parameters_open);
    move(Back,24,22,86);ShowWindow(child(window,Back),home?SW_HIDE:SW_SHOW);
    move(Logs,width-370,22,90);move(Help,width-270,22,250);
    ShowWindow(ui.pane,pane_visible?SW_SHOW:SW_HIDE);
    ShowWindow(child(window,Primary),home||camera_settings?SW_HIDE:SW_SHOW);
    ShowWindow(child(window,Cancel),home||camera_settings?SW_HIDE:SW_SHOW);
    for(int id=PersonId;id<=ReleaseMemory;++id){ShowWindow(child(window,id),SW_HIDE);ShowWindow(child(window,id+kLabelOffset),SW_HIDE);}
    for(int id:{Device,RefreshDevices,Open,Close,Resolution,Rotation,Mirror,Apply,People,Enroll,GoGallery,Parameters,SearchImageAgain,HomePrevious,HomeNext,SearchResults,SearchCaption,CopySearch})ShowWindow(child(window,id),SW_HIDE);
    for(int i=0;i<kFeatureCount;++i)ShowWindow(child(window,HomeBase+i),SW_HIDE);
    move(Status,ui.main_left+8,height-67,width-ui.main_left-32,32);
    move(Metrics,ui.main_left+8,height-32,width-ui.main_left-32,24);
    if(home){
        const auto view=homeGeometry(ui);ui.home_scroll=view.clampScroll(ui.home_scroll);
        for(int i=0;i<kFeatureCount;++i){
            const auto area=view.card(i,ui.home_scroll),visible=view.visible(i,ui.home_scroll);
            move(HomeBase+i,area.left,area.top,area.width(),area.height());
            const auto handle=child(window,HomeBase+i);
            if(visible.height()){
                const auto region=CreateRectRgn(0,px(visible.top-area.top),px(area.width()),px(visible.bottom-area.top));
                if(!SetWindowRgn(handle,region,TRUE))DeleteObject(region);
                ShowWindow(handle,SW_SHOW);
            }
        }
        if(view.clampScroll(100000)>0){
            move(HomePrevious,32,height-148,100);move(HomeNext,144,height-148,100);
            ShowWindow(child(window,HomePrevious),SW_SHOW);ShowWindow(child(window,HomeNext),SW_SHOW);
        }
        EnableWindow(child(window,HomePrevious),ui.home_scroll>0);
        EnableWindow(child(window,HomeNext),ui.home_scroll<view.clampScroll(100000));
        ui.image_search.layout({},false);return;
    }
    int header_x=ui.main_left+8;
    auto header=[&](int id,int w){move(id,header_x,82,w);header_x+=w+8;ShowWindow(child(window,id),SW_SHOW);};
    if(ui.page==Page::Search)header(Enroll,136);
    if(ui.page==Page::Search||ui.page==Page::ImageSearch||ui.page==Page::Verification)header(GoGallery,136);
    if(hasFeatureParameters(ui.page))header(Parameters,136);
    setText(window,GoGallery,ui.page==Page::Verification?L"选择基准":L"人脸库");
    const bool camera_page=usesCamera(ui.page);
    ToolbarLayout toolbar(ui.main_left+8,width-20,130);
    auto toolbarControl=[&](int id,int w){const auto r=toolbar.place(w);move(id,r.left,r.top,r.width(),id==Device||id==Resolution||id==Rotation?260:34);ShowWindow(child(window,id),SW_SHOW);};
    if(camera_page){
        toolbarControl(Device,width<1100?180:228);toolbarControl(RefreshDevices,56);toolbarControl(Open,108);toolbarControl(Close,108);
        if(camera_settings){toolbarControl(Resolution,110);toolbarControl(Rotation,104);toolbarControl(Mirror,104);}
        if(camera_settings||ui.parameters_open)toolbarControl(Apply,112);
    }
    ui.card_top=camera_page?toolbar.y+80:ui.page==Page::ImageSearch?156:110;
    const int preview_left=pane_visible?ui.main_left+ui.parameter_width+14:ui.main_left;
    ui.parameters=rect(ui.main_left,ui.card_top,ui.parameter_width,std::max(1,height-80-ui.card_top));
    const int canvas_bottom=pane_visible||camera_settings?height-80:height-164;
    ui.canvas=rect(preview_left,ui.card_top,std::max(1,width-preview_left-20),std::max(1,canvas_bottom-ui.card_top));
    if(ui.page==Page::Search){
        const int details_height=68;
        const int caption_width=std::max(1,width-preview_left-116);
        move(SearchCaption,preview_left+8,canvas_bottom-details_height+4,caption_width,28);
        move(SearchResults,preview_left+8,canvas_bottom-details_height+34,std::max(1,width-preview_left-36),28);
        move(CopySearch,width-96,canvas_bottom-details_height+4,68,28);
        for (int id : {SearchCaption, SearchResults, CopySearch}) ShowWindow(child(window,id),SW_SHOW);
        for (int id : {SearchCaption, SearchResults})
            SendMessageW(child(window,id),WM_SETFONT,reinterpret_cast<WPARAM>(ui.theme.result),TRUE);
        ui.canvas.bottom-=px(details_height);
    }
    ui.view_height=std::max(1,height-194-ui.card_top);
    MoveWindow(ui.pane,px(ui.main_left+10),px(ui.card_top+40),px(ui.parameter_width-20),px(ui.view_height),TRUE);
    int y=8;
    auto row=[&](int id,bool labelled=true){
        if(id==primaryAction(ui.page) && ui.page!=Page::Enrollment)return;
        ShowWindow(child(window,id),SW_SHOW);
        if(labelled&&child(window,id+kLabelOffset)){
            ShowWindow(child(window,id+kLabelOffset),SW_SHOW);
            move(id+kLabelOffset,8,y-ui.scroll,ui.parameter_width-58,22);
            move(id,8,y+25-ui.scroll,ui.parameter_width-58,34);y+=76;
        }else{move(id,8,y-ui.scroll,ui.parameter_width-58,id==SearchMode||id==Actions?280:36);y+=46;}
    };
    if(ui.page==Page::Enrollment){
        ShowWindow(child(window,SearchGuide),SW_SHOW);
        move(SearchGuide,8,y-ui.scroll,ui.parameter_width-58,100);y+=110;
    }
    if(ui.page==Page::Gallery){row(FilterGroup);row(FilterTag);}
    if(ui.page==Page::Reference||ui.page==Page::Verification||ui.page==Page::Enrollment||ui.page==Page::Gallery)row(PersonId);
    if(ui.page==Page::Search||ui.page==Page::Enrollment||ui.page==Page::Gallery){row(Group);row(Tag);}
    switch(ui.page){
        case Page::Detection:row(MaxFaces);row(Details,false);break;
        case Page::Reference:row(PickA,false);row(ImageReference,false);row(GalleryReference,false);row(ImportReference,false);row(ExportFeature,false);row(ExportPreview,false);break;
        case Page::Verification:row(ImportReference,false);row(Threshold);row(Actions,false);row(CustomActions);row(Silent,false);row(Multiple,false);break;
        case Page::Liveness:row(Actions,false);row(CustomActions);row(Multiple,false);row(ExportPreview,false);break;
        case Page::Search:row(SearchMode,false);row(MaxFaces);row(Silent,false);row(Threshold);row(Interval);row(Timeout);break;
        case Page::Enrollment:row(SaveEnrollment,false);row(StartFlow,false);row(PickA,false);row(ImageEnroll,false);break;
        case Page::Comparison:row(Threshold);row(PickA,false);row(PickB,false);break;
        case Page::Gallery:
            row(UpdatePerson,false);row(DeletePerson,false);
            if(ui.history.parent(ui.page)==Page::Verification)row(GalleryReference,false);
            row(ImportJson,false);row(ImportImages,false);row(ExportJson,false);row(PreviousPage,false);row(NextPage,false);row(ReleaseMemory,false);break;
        case Page::Home:case Page::CameraSettings:case Page::ImageSearch:break;
    }
    ui.content_height=y;
    const int offset=std::clamp(ui.scroll,0,std::max(0,y-ui.view_height));
    if(offset!=ui.scroll){ui.scroll=offset;layout(ui);return;}
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,y-1),static_cast<UINT>(ui.view_height),ui.scroll,0};SetScrollInfo(ui.pane,SB_VERT,&info,TRUE);
    if(pane_visible){move(Primary,ui.main_left+18,height-148,ui.parameter_width-36,36);move(Cancel,ui.main_left+18,height-108,ui.parameter_width-36,24);}
    else{move(Primary,ui.main_left+8,height-148,220,36);move(Cancel,ui.main_left+240,height-148,180,36);}
    if(ui.page==Page::ImageSearch){
        const int slot=std::max(1,std::min(220,(width-ui.main_left-48)/3));
        move(Primary,ui.main_left+8,height-148,slot,36);
        move(SearchImageAgain,ui.main_left+slot+20,height-148,slot,36);ShowWindow(child(window,SearchImageAgain),SW_SHOW);
        move(Cancel,ui.main_left+slot*2+32,height-148,slot,36);
    }
    ShowWindow(child(window,Cancel),camera_settings?SW_HIDE:SW_SHOW);
    move(People,preview_left+14,ui.card_top+50,std::max(1,width-preview_left-48),std::max(1,height-ui.card_top-148));ShowWindow(child(window,People),ui.page==Page::Gallery?SW_SHOW:SW_HIDE);
    const int column_widths[]={210,140,160,160};for(int i=0;i<4;++i)ListView_SetColumnWidth(child(window,People),i,px(column_widths[i]));
    setText(window,MaxFaces+kLabelOffset,ui.page==Page::Search?L"最多人脸（1–5）":L"最多人脸（1–10）");
    setText(window,Group+kLabelOffset,ui.page==Page::Search?L"搜索范围分组（留空为全部）":L"人员分组");
    setText(window,Tag+kLabelOffset,ui.page==Page::Search?L"搜索范围标签（留空为全部）":L"人员标签");
    setText(window,Threshold+kLabelOffset,ui.page==Page::Search?L"相似度阈值（0.75–0.85）":L"相似度阈值（0–1）");
    setText(window,Primary,primaryTitle(ui.page));setText(window,StartFlow,primaryTitle(ui.page));
    setText(window,PickA,ui.page==Page::Detection?L"选择图片并检测":ui.page==Page::Enrollment?L"选择待录入图片":L"选择图片 A / 基准图片");
    ui.image_search.layout(ui.canvas,ui.page==Page::ImageSearch);InvalidateRect(ui.pane,nullptr,TRUE);
}
void scrollHome(Ui& ui,int position){ui.home_scroll=homeGeometry(ui).clampScroll(position);layout(ui);InvalidateRect(ui.app.window,nullptr,TRUE);}
std::wstring updatedTime(int64_t milliseconds) {
    if (milliseconds <= 0 || milliseconds > INT64_C(253402300799999)) return L"—";
    ULARGE_INTEGER ticks{}; ticks.QuadPart = (static_cast<ULONGLONG>(milliseconds) + 11644473600000ULL) * 10000ULL;
    FILETIME file{ticks.LowPart, ticks.HighPart}; SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"—";
    std::wostringstream value; value << std::setfill(L'0') << local.wYear << L"-" << std::setw(2) << local.wMonth << L"-" << std::setw(2) << local.wDay
        << L" " << std::setw(2) << local.wHour << L":" << std::setw(2) << local.wMinute << L":" << std::setw(2) << local.wSecond;
    return value.str();
}
void update(Ui& ui) {
    const auto state = ui.app.current(); const auto window = ui.app.window;
    setText(window, Status, ui.parameter_notice.empty() ? widen(state->update.message) : ui.parameter_notice);
    ShowWindow(child(window,Metrics),ui.page==Page::Search?SW_HIDE:SW_SHOW);
    ShowWindow(child(window,Status),ui.page==Page::Search?SW_HIDE:SW_SHOW);
    std::wostringstream metrics;
    metrics << L"人脸库 " << state->total << L" 条 | 1:1 基准: " << (state->has_reference ? L"已保存" : L"未设置");
    if (ui.page == Page::Search) metrics << L" | 当前搜索范围 " << state->search_population << L" 条";
    if (state->camera_open) metrics << std::fixed << std::setprecision(1) << L" | 采集 " << state->capture_fps << L" FPS | 分析 " << state->analysis_fps << L" FPS | 推理 " << state->inference_ms << L" ms";
    if (ui.page == Page::Gallery) metrics << L" | 当前筛选 " << state->filtered << L" 条 | " << state->offset + (state->filtered ? 1 : 0) << L"-" << state->offset + state->people.size();
    setText(window, Metrics, metrics.str());
    if (state->completed_command >= ui.pending_command) ui.pending_command = 0;
    const bool ready = !state->busy && !ui.pending_command && !state->flow_active && !state->fatal;
    for (int id = PersonId; id <= ReleaseMemory; ++id) EnableWindow(child(window, id), ready);
    EnableWindow(child(window, Open), ready && !state->camera_open && !ui.app.cameras.empty() && usesCamera(ui.page));
    EnableWindow(child(window, Close), state->camera_open || state->busy);
    EnableWindow(child(window, StartFlow), ready && state->camera_open && (ui.page != Page::Verification || state->has_reference));
    EnableWindow(child(window, Enroll), ready);
    EnableWindow(child(window, Pause), ready && state->camera_open);
    EnableWindow(child(window, Cancel), state->flow_active || state->busy);
    EnableWindow(child(window, Device), ready && !state->camera_open);
    EnableWindow(child(window, RefreshDevices), ready && !state->camera_open);
    EnableWindow(child(window, Resolution), ready && !state->camera_open);
    const bool can_navigate = (!state->busy && !ui.pending_command) || ui.page == Page::ImageSearch;
    for (int i = 0; i < kFeatureCount; ++i) EnableWindow(child(window, HomeBase + i), can_navigate && !state->fatal);
    EnableWindow(child(window, Back), TRUE);
    EnableWindow(child(window, Parameters), can_navigate && !state->fatal);
    EnableWindow(child(window, HomePrevious), ui.home_scroll > 0);
    EnableWindow(child(window, HomeNext), ui.home_scroll < homeGeometry(ui).clampScroll(100000));
    EnableWindow(child(window, Apply), ready);
    EnableWindow(child(window, Silent), ready && (ui.page != Page::Search || selection(window, SearchMode) == 0));
    EnableWindow(child(window, MaxFaces), ready && (ui.page != Page::Search || selection(window, SearchMode) == 1));
    EnableWindow(child(window, CustomActions), ready && selection(window, Actions) == 7);
    EnableWindow(child(window, Compare), ready && state->image_a && state->image_b);
    EnableWindow(child(window, ImageReference), ready && state->image_a);
    EnableWindow(child(window, ImageEnroll), ready && state->image_a);
    EnableWindow(child(window, SaveEnrollment), ready && state->pending_enrollment);
    EnableWindow(child(window, ExportFeature), ready && state->has_reference);
    EnableWindow(child(window, ExportPreview), ready && state->captured);
    EnableWindow(child(window, PreviousPage), ready && state->offset > 0);
    EnableWindow(child(window, NextPage), ready && state->offset + state->people.size() < state->filtered);
    EnableWindow(child(window, SearchImageAgain), ready && state->search_photo);
    EnableWindow(child(window, PickSearchImage), !state->fatal && ui.page == Page::ImageSearch);
    EnableWindow(child(window, GoGallery), !state->fatal);
    EnableWindow(child(window, Rotation), ready && ui.page != Page::ImageSearch);
    EnableWindow(child(window, Mirror), ready && ui.page != Page::ImageSearch);
    const bool current_photo = state->settings.page == ui.page && (!ui.search_request_generation || state->search_generation == ui.search_request_generation);
    if (ui.page == Page::ImageSearch) {
        ui.image_search.setState(state->busy || ui.pending_command, state->error || state->fatal,
            current_photo ? widen(state->update.message) : L"正在读取图片并检测人脸…", current_photo ? state->search_file.filename().wstring() : L"");
        if (current_photo) ui.image_search.setContent(state->search_photo, state->image_search);
        else if (!ui.pending_command) ui.image_search.setContent(nullptr, nullptr);
    }
    EnableWindow(child(window, Primary), IsWindowEnabled(child(window, primaryAction(ui.page))));
    EnableWindow(child(window, Cancel), state->flow_active || state->busy || ui.pending_command || state->pending_enrollment
        || (ui.page == Page::Search && state->camera_open && !state->paused));
    setText(window, Apply, ui.dirty ? L"应用修改 *" : L"应用参数");
    setText(window, Parameters, ui.parameters_open ? L"收起参数" : ui.dirty ? L"参数设置 *" : L"参数设置");
    for (auto* returning : {&ui.return_after_save, &ui.return_after_reference}) {
        if (*returning && state->completed_command >= *returning) {
            const bool leave = !state->error && !state->fatal &&
                (returning == &ui.return_after_save ? ui.page == Page::Enrollment && !state->pending_enrollment : ui.page == Page::Gallery);
            *returning = 0;
            if (leave) PostMessageW(window, WM_COMMAND, MAKEWPARAM(Back, BN_CLICKED), 0);
        }
    }
    setText(window, Pause, state->paused ? L"恢复搜索" : L"暂停搜索 / 释放缓存");
    if (ui.page == Page::Search) setText(window, Primary, state->paused ? L"恢复搜索" : L"暂停搜索");
    if (ui.page == Page::Enrollment) {
        setText(window, SearchGuide, state->pending_enrollment
            ? L"第 2 步 / 共 2 步\r\n核对预览与人员信息，再确认保存。\r\n重复 ID 不会覆盖已有人员。"
            : L"第 1 步 / 共 2 步\r\n打开相机后开始采集，或选择单人图片。\r\n采集后确认保存才会入库。");
        setText(window, Primary, state->pending_enrollment ? L"确认预览并保存人员" : L"开始 / 重新采集");
        EnableWindow(child(window, Primary), ready && (state->pending_enrollment || state->camera_open));
        setText(window, Cancel, state->pending_enrollment ? L"丢弃本次预览" : L"取消采集");
    } else setText(window, Cancel, ui.page == Page::Search ? L"停止搜索" : L"取消流程 / 停止导入");
    if (ui.page == Page::Search && state->settings.page == Page::Search) {
        const auto& options = state->settings.search;
        std::wostringstream mode, detail;
        mode << (options.multi_face ? L"M:N · 不检测活体" : options.liveness ? L"1:N · 活体已开启" : L"1:N · 活体未开启");
        if (state->error || state->fatal) detail << widen(state->update.message);
        else if (!ui.parameter_notice.empty()) detail << ui.parameter_notice;
        else if (ui.dirty) detail << L"参数有未应用修改，请先应用参数。";
        else if (state->paused || !state->camera_open || state->search_population == 0)
            detail << (state->paused ? L"已暂停；恢复后重新识别。" : !state->search_population
                ? L"当前范围没有人员；请录入或检查分组/标签。" : L"打开摄像头开始搜索。");
        else if (state->update.faces.empty()) detail << widen(state->update.message);
        else {
            const auto best = bestSearchMatchCaption(state->update, !options.multi_face && options.liveness);
            detail << widen(best.empty() ? searchStatusText(state->update) : best);
        }
        const auto full=mode.str()+L"\r\n"+detail.str();
        if(full!=ui.last_search_results){
            ui.last_search_results=full;
            auto line=detail.str();std::replace(line.begin(),line.end(),L'\r',L' ');std::replace(line.begin(),line.end(),L'\n',L' ');
            setText(window,SearchCaption,mode.str());setText(window,SearchResults,line);
        }
    }

    bool changed = state->total != ui.rendered_gallery || state->offset != ui.rendered_offset || state->people.size() != ui.rendered_people.size();
    if (!changed) for (size_t i = 0; i < state->people.size(); ++i) {
        const auto& a = state->people[i]; const auto& b = ui.rendered_people[i];
        if (a.id != b.id || a.group != b.group || a.tag != b.tag || a.updated != b.updated) { changed = true; break; }
    }
    if (changed) {
        const auto list = child(window, People);
        const auto selected_id = text(window, PersonId);
        ui.rendering_list = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(list);
        for (size_t index = 0; index < state->people.size(); ++index) {
            const auto& person = state->people[index]; const auto id = widen(person.id);
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(index); item.pszText = const_cast<wchar_t*>(id.c_str());
            ListView_InsertItem(list, &item);
            auto group = widen(person.group), tag = widen(person.tag), updated = updatedTime(person.updated);
            ListView_SetItemText(list, item.iItem, 1, group.data()); ListView_SetItemText(list, item.iItem, 2, tag.data()); ListView_SetItemText(list, item.iItem, 3, updated.data());
            if (id == selected_id) ListView_SetItemState(list, item.iItem, LVIS_SELECTED, LVIS_SELECTED);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
        ui.rendered_people = state->people; ui.rendered_gallery = state->total; ui.rendered_offset = state->offset; ui.rendering_list = false;
    }
    InvalidateRect(window, nullptr, FALSE);
}
struct Fit { int x = 0, y = 0, width = 0, height = 0; double scale = 0; };
Fit bitmap(HDC dc, const Image& image, const RECT& area) {
    Fit fit; if (image.width <= 0 || image.height <= 0 || image.rgba.empty()) return fit;
    fit.scale = std::min(double(area.right - area.left) / image.width, double(area.bottom - area.top) / image.height);
    fit.width = static_cast<int>(image.width * fit.scale); fit.height = static_cast<int>(image.height * fit.scale);
    fit.x = area.left + (area.right - area.left - fit.width) / 2; fit.y = area.top + (area.bottom - area.top - fit.height) / 2;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = image.width; info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, fit.x, fit.y, fit.width, fit.height, 0, 0, image.width, image.height, image.rgba.data(), &info, DIB_RGB_COLORS, SRCCOPY);
    return fit;
}
void paint(Ui& ui) {
    PAINTSTRUCT paint{}; HDC target = BeginPaint(ui.app.window, &paint); RECT client{}; GetClientRect(ui.app.window, &client);
    HDC dc = CreateCompatibleDC(target); const auto buffer = CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
    const auto old_bitmap = SelectObject(dc, buffer);
    auto px = [&](int value) { return ui.theme.px(value); };
    auto rect = [&](int x, int y, int w, int h) { return RECT{px(x), px(y), px(x + w), px(y + h)}; };
    fill(dc, client, colors::background);
    const int logical_width = MulDiv(client.right, 96, ui.theme.dpi);
    const int title_left = ui.page == Page::Home ? 32 : 128;
    label(dc, ui.page == Page::Home ? L"HelloFace SDK" : page_info[static_cast<int>(ui.page)].title, rect(title_left, 18, std::max(1, logical_width - title_left - 388), 38), ui.theme.title, colors::ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    label(dc, ui.page == Page::Home ? L"Offline | Private | Fast" : page_info[static_cast<int>(ui.page)].description, rect(ui.main_left + 8, 56, std::max(1, logical_width - ui.main_left - 30), 20), ui.theme.small, colors::muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (IsWindowVisible(ui.pane)) {
        card(dc, ui.parameters, colors::surface, colors::line, px(14));
        label(dc, L"参数与操作", rect(ui.main_left + 18, ui.card_top + 8, ui.parameter_width - 36, 26), ui.theme.body, colors::ink);
    }
    if (ui.page == Page::Home) {
        label(dc, L"选择一个独立功能；录入与人脸库从人脸搜索页进入。", rect(32, 78, logical_width - 56, 22), ui.theme.small, colors::muted);
        const auto home = homeGeometry(ui);
        if (home.clampScroll(100000)) label(dc, L"滚动查看八个功能入口", rect(260, MulDiv(client.bottom, 96, ui.theme.dpi) - 148, logical_width - 284, 34), ui.theme.small, colors::muted);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, old_bitmap);
        DeleteObject(buffer); DeleteDC(dc); EndPaint(ui.app.window, &paint); return;
    }
    const auto state = ui.app.current();
    const wchar_t* badge = state->fatal || state->error ? L"发生错误 · 查看下方提示" : state->busy || ui.pending_command ? L"处理中，请稍候…" : state->flow_active ? L"流程进行中" : ui.page == Page::Enrollment && state->pending_enrollment ? L"待确认录入 · 尚未写入人脸库" : ui.page == Page::CameraSettings ? L"相机预览 · 不执行人脸分析" : ui.page == Page::ImageSearch ? L"图片搜索 · 无需摄像头" : state->camera_open ? state->paused ? L"摄像头已连接 · 搜索暂停" : L"摄像头已连接" : L"摄像头未打开";
    RECT badge_area{ui.canvas.left + px(10), ui.canvas.top - px(32), ui.canvas.right, ui.canvas.top - px(6)};
    label(dc, badge, badge_area, ui.theme.small, state->error || state->fatal ? colors::error : colors::muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT area = ui.canvas;
    card(dc, area, ui.page == Page::Gallery ? colors::surface : colors::canvas, colors::line, px(14));
    if (ui.page == Page::Gallery) {
        RECT title = area; title.left += px(18); title.top += px(10); title.bottom = title.top + px(32);
        label(dc, L"人员列表  ·  选择记录后在左侧编辑", title, ui.theme.body, colors::ink);
    }

    if (state->settings.page == ui.page && ui.page != Page::Gallery && ui.page != Page::ImageSearch) {
        InflateRect(&area, -px(2), -px(2));
        const auto saved = SaveDC(dc); IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
        SelectObject(dc, ui.theme.body); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(240, 240, 240));
        if (ui.page == Page::Comparison) {
            const LONG middle = (area.left + area.right) / 2;
            RECT left{area.left, area.top + 30, middle - 5, area.bottom}, right{middle + 5, area.top + 30, area.right, area.bottom};
            auto a = state->aligned_a ? state->aligned_a : state->image_a, b = state->aligned_b ? state->aligned_b : state->image_b;
            if (a) bitmap(dc, *bgraImage(*a), left); else label(dc, L"选择图片 A", left, ui.theme.body, RGB(157,176,204), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (b) bitmap(dc, *bgraImage(*b), right); else label(dc, L"选择图片 B", right, ui.theme.body, RGB(157,176,204), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            TextOutW(dc, area.left + 12, area.top + 8, L"图片 A", 4); TextOutW(dc, middle + 12, area.top + 8, L"图片 B", 4);
        } else if (ui.page == Page::Enrollment && state->captured && state->pending_enrollment) {
            bitmap(dc, *bgraImage(*state->captured), area);
        } else if (!state->preview) {
            RECT title = area; title.left += px(24); title.right -= px(24);
            title.top = (area.top + area.bottom) / 2 - px(38); title.bottom = title.top + px(34);
            label(dc, L"准备开始", title, ui.theme.title, RGB(230, 237, 250), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            title.top += px(42); title.bottom = title.top + px(70);
            const wchar_t* hint = ui.page == Page::Detection ? L"打开摄像头查看实时结果，或选择图片检测"
                : ui.page == Page::Verification ? L"先建立 1:1 基准，再打开摄像头开始验证"
                : ui.page == Page::CameraSettings ? L"选择设备并打开以检查画面；修改后点击应用参数，保存 SDK 参数"
                : ui.page == Page::Enrollment ? L"相机采集或选择图片，核对预览后确认保存人员"
                : ui.page == Page::Search ? L"打开摄像头开始搜索；录入人员请进入独立录入页"
                : L"选择并打开摄像头，然后点击下方开始按钮";
            label(dc, hint, title, ui.theme.body, RGB(157, 176, 204), DT_CENTER | DT_WORDBREAK);
        } else if (state->preview) {
            const auto fit = bitmap(dc, *state->preview, area);
            const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            for (size_t i = 0; i < state->update.faces.size(); ++i) {
                const auto& face = state->update.faces[i];
                const bool passed = ui.page == Page::Search ? searchFaceAccepted(state->update, face) : state->update.matched;
                const auto color = passed ? RGB(40, 220, 90) : RGB(255, 190, 40);
                const auto pen = CreatePen(PS_SOLID, px(2), color); const auto previous_pen = SelectObject(dc, pen);
                auto x = [&](float value) { return fit.x + static_cast<int>((state->settings.mirror ? state->preview->width - value : value) * fit.scale); };
                auto y = [&](float value) { return fit.y + static_cast<int>(value * fit.scale); };
                int left = x(face.x), right = x(face.x + face.width); if (left > right) std::swap(left, right);
                Rectangle(dc, left, y(face.y), right, y(face.y + face.height));
                for (size_t point = 0; point < HIFACE_MAX_LANDMARKS; ++point) {
                    const auto px = x(face.landmarks[point * 2]), py = y(face.landmarks[point * 2 + 1]); Ellipse(dc, px - 2, py - 2, px + 3, py + 3);
                }
                if (i < state->update.details.size()) {
                    const auto& detail = state->update.details[i];
                    for (const auto& point : detail.mesh) SetPixelV(dc, x(point.x), y(point.y), RGB(100, 210, 255));
                    if (detail.face.pose_valid) {
                        std::wostringstream pose; pose << std::fixed << std::setprecision(1) << L"pitch=" << detail.face.pitch << L" yaw=" << detail.face.yaw << L" roll=" << detail.face.roll;
                        if (detail.face.metrics_valid) pose << L" EAR=" << detail.face.left_ear << L"/" << detail.face.right_ear << L" MAR=" << detail.face.mar;
                        const auto value = pose.str(); TextOutW(dc, left, y(face.y + face.height) + 2, value.c_str(), static_cast<int>(value.size()));
                    }
                }
                if (face.has_similarity) {
                    const auto label = widen(ui.page == Page::Search ? searchFaceText(state->update, face) : std::string(face.face_id) + " " + std::to_string(face.similarity));
                    TextOutW(dc, left, y(face.y) - px(22), label.c_str(), static_cast<int>(label.size()));
                }
                SelectObject(dc, previous_pen); DeleteObject(pen);
            }
            SelectObject(dc, old_brush);
        }
        if (state->captured && ui.page != Page::Comparison && ui.page != Page::Detection && ui.page != Page::Enrollment) {
            RECT thumb{area.right - px(170), area.bottom - px(196), area.right - px(6), area.bottom - px(6)};
            FillRect(dc, &thumb, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); bitmap(dc, *bgraImage(*state->captured), thumb);
            TextOutW(dc, thumb.left, thumb.top - px(20), L"最近对齐抓拍", 6);
        }
        RestoreDC(dc, saved);
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap); DeleteObject(buffer); DeleteDC(dc);
    EndPaint(ui.app.window, &paint);
}
void layout(Ui& ui);
LRESULT CALLBACK paneProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto parent = GetParent(window); auto* ui = reinterpret_cast<Ui*>(GetWindowLongPtrW(parent, GWLP_USERDATA));
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    if (message == WM_COMMAND || message == WM_DRAWITEM || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORBTN)
        return SendMessageW(parent, message, wparam, lparam);
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) { PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); FillRect(dc, &paint.rcPaint, ui->theme.surface); EndPaint(window, &paint); return 0; }
    if (message == kRevealFocusedControl) {
        RECT focused{}; GetWindowRect(reinterpret_cast<HWND>(lparam), &focused);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&focused), 2);
        const int top = MulDiv(focused.top, 96, ui->theme.dpi), bottom = MulDiv(focused.bottom, 96, ui->theme.dpi);
        if (top < 0) ui->scroll += top - 8;
        else if (bottom > ui->view_height) ui->scroll += bottom - ui->view_height + 8;
        ui->scroll = std::clamp(ui->scroll, 0, std::max(0, ui->content_height - ui->view_height)); layout(*ui); return 0;
    }
    if (message == WM_VSCROLL || message == WM_MOUSEWHEEL) {
        int next = ui->scroll;
        if (message == WM_MOUSEWHEEL) next -= GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 84;
        else {
            SCROLLINFO info{}; info.cbSize = sizeof(info); info.fMask = SIF_TRACKPOS; GetScrollInfo(window, SB_VERT, &info);
            switch (LOWORD(wparam)) {
                case SB_LINEUP: next -= 28; break; case SB_LINEDOWN: next += 28; break;
                case SB_PAGEUP: next -= ui->view_height; break; case SB_PAGEDOWN: next += ui->view_height; break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: next = info.nTrackPos; break;
                case SB_TOP: next = 0; break; case SB_BOTTOM: next = ui->content_height; break;
            }
        }
        ui->scroll = std::clamp(next, 0, std::max(0, ui->content_height - ui->view_height)); layout(*ui); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void create(Ui& ui) {
    const auto window = ui.app.window;
    ui.theme.setDpi(GetDpiForWindow(window));
    WNDCLASSW panel{}; panel.lpfnWndProc = paneProcedure; panel.hInstance = GetModuleHandleW(nullptr);
    panel.lpszClassName = L"HiFaceParameterPane"; panel.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&panel);
    ui.pane = CreateWindowExW(WS_EX_CONTROLPARENT, panel.lpszClassName, L"参数与操作", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 10, 10, window, reinterpret_cast<HMENU>(ParameterPane), panel.hInstance, nullptr);
    if (!ui.pane) throw std::runtime_error("Cannot create parameter panel");
    for (int i = 0; i < kFeatureCount; ++i) button(ui, HomeBase + i, page_info[static_cast<int>(feature_pages[i])].title);
    button(ui, Back, L"返回"); button(ui, Parameters, L"参数设置");
    button(ui, HomePrevious, L"上一屏"); button(ui, HomeNext, L"下一屏");
    button(ui, Primary, L"开始");
    combo(ui, Device, {}); button(ui, RefreshDevices, L"刷新"); button(ui, Open, L"打开摄像头"); button(ui, Close, L"关闭摄像头"); button(ui, Logs, L"打开日志"); button(ui, Help, aboutTitle().c_str());
    combo(ui, Resolution, {L"640×480", L"1280×720", L"1920×1080"}); combo(ui, Rotation, {L"旋转 0°", L"旋转 90°", L"旋转 180°", L"旋转 270°"});
    control(ui, L"BUTTON", L"镜像预览", Mirror, BS_AUTOCHECKBOX | WS_TABSTOP); button(ui, Apply, L"应用参数");
    field(ui, PersonId, L"人员 ID", L"person-001"); field(ui, Group, L"分组", L""); field(ui, Tag, L"标签", L"");
    field(ui, FilterGroup, L"筛选分组（留空为全部）", L""); field(ui, FilterTag, L"筛选标签（留空为全部）", L"");
    field(ui, MaxFaces, L"最多人脸", L"3"); field(ui, Threshold, L"相似度阈值", L"0.80"); field(ui, Interval, L"成功间隔 ms", L"0"); field(ui, Timeout, L"搜索超时 ms", L"3000");
    field(ui, CustomActions, L"动作序列", L"2,1,0");
    control(ui, L"BUTTON", L"468 点 / 头姿 / EAR / MAR", Details, BS_AUTOCHECKBOX | WS_TABSTOP); checkBox(window, Details, true);
    control(ui, L"BUTTON", L"启用静默活体", Silent, BS_AUTOCHECKBOX | WS_TABSTOP); checkBox(window, Silent, true);
    control(ui, L"BUTTON", L"允许多人（处理主目标）", Multiple, BS_AUTOCHECKBOX | WS_TABSTOP); checkBox(window, Multiple, true);
    combo(ui, SearchMode, {L"1:N 单人搜索", L"M:N 多人搜索（无活体）"});
    combo(ui, Actions, {L"不要求动作", L"随机 3 个不重复动作", L"眨眼", L"点头", L"摇头", L"张嘴", L"眨眼 → 点头", L"自定义：0摇头 1点头 2眨眼 3张嘴"}); send(window, Actions, CB_SETCURSEL, 1, 0);
    button(ui, StartFlow, L"开始本轮"); button(ui, Enroll, L"人脸录入");
    button(ui, SaveEnrollment, L"确认预览并保存人员"); button(ui, Cancel, L"取消流程 / 停止导入"); button(ui, Pause, L"暂停搜索 / 释放缓存");
    button(ui, PickA, L"选择图片 A"); button(ui, PickB, L"选择图片 B"); button(ui, Compare, L"比对两张图片（最大人脸）");
    button(ui, ImageReference, L"图片 A 设为 1:1 基准"); button(ui, GalleryReference, L"该 ID 设为 1:1 基准"); button(ui, ImportReference, L"导入 HF1 基准文件");
    button(ui, ExportFeature, L"导出 1:1 基准 HF1"); button(ui, ExportPreview, L"导出最近对齐抓拍 PNG"); button(ui, ImageEnroll, L"从图片采集待确认人脸");
    button(ui, RefreshGallery, L"按分组 / 标签筛选并刷新"); button(ui, PreviousPage, L"上一页（100 条）"); button(ui, NextPage, L"下一页（100 条）"); button(ui, DeletePerson, L"删除该人员 ID"); button(ui, UpdatePerson, L"保存人员分组 / 标签");
    button(ui, ImportJson, L"批量导入 Android HF1 JSON"); button(ui, ExportJson, L"导出筛选结果 HF1 JSON"); button(ui, ImportImages, L"多选本地图片提取并导入"); button(ui, ReleaseMemory, L"释放数据库内存缓存");
    button(ui, PickSearchImage, L"选择图片并搜索"); button(ui, SearchImageAgain, L"重新搜索当前图片"); button(ui, GoGallery, L"打开人脸库");
    control(ui, L"STATIC", L"最多 10 张脸 · 阈值 0.80", SearchGuide);
    // Two fixed single-line controls; copying keeps the full, unabridged result.
    // 固定两个单行控件，复制时保留未缩略的完整结果。
    control(ui, L"EDIT", L"", SearchCaption, ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP);
    control(ui, L"EDIT", L"", SearchResults, ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP);
    control(ui, L"BUTTON", L"复制", CopySearch, WS_TABSTOP);
    const auto list = control(ui, WC_LISTVIEWW, L"", People, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const wchar_t* columns[] = {L"人员 ID", L"分组", L"标签", L"更新时间"};
    for (int i = 0; i < 4; ++i) { LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = ui.theme.px(160); column.pszText = const_cast<wchar_t*>(columns[i]); ListView_InsertColumn(list, i, &column); }
    control(ui, L"STATIC", L"正在打开人脸库...", Status); control(ui, L"STATIC", L"", Metrics);
    for (int id = PersonId; id <= ReleaseMemory; ++id) {
        if (id == Cancel || id == Enroll || id == GoGallery || id == SearchImageAgain) continue;
        if (const auto field = child(window, id)) SetParent(field, ui.pane);
        if (const auto label = child(window, id + kLabelOffset)) SetParent(label, ui.pane);
    }
    HWND after = HWND_TOP;
    for (const int id : {Back, HomePrevious, HomeNext}) {
        const auto field = child(window, id);
        SetWindowPos(field, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); after = field;
    }
    for (int i = 0; i < kFeatureCount; ++i) {
        const auto field = child(window, HomeBase+i);
        SetWindowPos(field, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); after = field;
    }
    const int tab_order[] = {Enroll, GoGallery, Parameters, Device, RefreshDevices, Open, Close, Resolution, Rotation, Mirror, Apply, ParameterPane, Primary, SearchImageAgain, Cancel, SearchResults, People, Logs, Help};
    for (const int id : tab_order) {
        const auto field = child(window, id); SetWindowPos(field, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); after = field;
    }
    const auto preferences = loadCameraPreferences(demoDataDirectory() / L"camera-settings.txt");
    send(window, Resolution, CB_SETCURSEL, preferences.width == 1920 ? 2 : preferences.width == 1280 ? 1 : 0, 0);
    send(window, Rotation, CB_SETCURSEL, preferences.rotation / 90, 0); checkBox(window, Mirror, preferences.mirror);
    ui.image_search.create(window, ui.theme);
    ui.creating = false; layout(ui);
    try { refreshDevices(ui); } catch (const std::exception& error) { writeLog(error.what()); setText(window, Status, widen(error.what())); }
    ui.app.start();
}
void action(Ui& ui, int id, int notification) {
    const auto window = ui.app.window;
    if (ui.creating) return;
    if (id == HomePrevious || id == HomeNext) {
        scrollHome(ui, ui.home_scroll + (id == HomePrevious ? -1 : 1) * std::max(96, homeGeometry(ui).viewport.height() - 20)); return;
    }
    if (id == Parameters || (id == Back && ui.parameters_open)) {
        ui.parameters_open = !ui.parameters_open;
        layout(ui); update(ui); InvalidateRect(window, nullptr, TRUE); SetFocus(child(window, ui.parameters_open ? Apply : Back)); return;
    }
    if (id == Back) {
        if (ui.page == Page::Home) return;
        if (ui.app.current()->importing && MessageBoxW(window, L"停止导入并返回上一级？已保存的批次会保留。", L"停止导入并返回", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        id = NavBase + static_cast<int>(ui.history.parent(ui.page));
    }
    if (id >= HomeBase && id < HomeBase + kFeatureCount) id = NavBase + static_cast<int>(feature_pages[id - HomeBase]);
    if (id >= NavBase && id < NavBase + kPageCount) {
        if (ui.page == static_cast<Page>(id - NavBase)) return;
        auto& previous = ui.drafts[static_cast<size_t>(ui.page)];
        previous.max_faces = text(window, MaxFaces); previous.threshold = text(window, Threshold);
        previous.actions = text(window, CustomActions); previous.action_preset = selection(window, Actions);
        previous.silent = checked(window, Silent); previous.multiple = checked(window, Multiple); previous.detailed = checked(window, Details);
        previous.interval = text(window, Interval); previous.timeout = text(window, Timeout); previous.search_mode = selection(window, SearchMode);
        previous.id = text(window, PersonId); previous.group = text(window, Group); previous.tag = text(window, Tag);
        previous.filter_group = text(window, FilterGroup); previous.filter_tag = text(window, FilterTag);
        const auto next_page = static_cast<Page>(id - NavBase), old_page = ui.page;
        if ((old_page == Page::ImageSearch && next_page != Page::Gallery) || (old_page == Page::Gallery && next_page != Page::ImageSearch))
            ui.image_search.setContent(nullptr, nullptr);
        ui.history.moved(old_page, next_page);
        ++ui.app.generation; ui.page = next_page; ui.scroll = 0; ui.dirty = false; ui.search_request_generation = 0; ui.parameters_open = false;
        ui.return_after_save = ui.return_after_reference = 0;
        const auto& next = ui.drafts[static_cast<size_t>(ui.page)];
        ui.creating = true;
        setText(window, MaxFaces, next.max_faces); setText(window, Threshold, next.threshold); setText(window, CustomActions, next.actions);
        send(window, Actions, CB_SETCURSEL, next.action_preset, 0); checkBox(window, Silent, next.silent); checkBox(window, Multiple, next.multiple);
        setText(window, Interval, next.interval); setText(window, Timeout, next.timeout); send(window, SearchMode, CB_SETCURSEL, next.search_mode, 0);
        checkBox(window, Details, next.detailed); setText(window, PersonId, next.id); setText(window, Group, next.group); setText(window, Tag, next.tag);
        setText(window, FilterGroup, next.filter_group); setText(window, FilterTag, next.filter_tag);
        ui.creating = false; ui.parameter_notice.clear();
        Settings settings;
        try { settings = readSettings(ui); }
        catch (const std::exception&) {
            // A saved invalid draft must not trap navigation or silently change the text.
            // 已保存的无效草稿不能阻止导航，也不能静默修改输入文字。
            settings = ui.app.current()->settings; settings.page = ui.page; ui.dirty = true;
            ui.parameter_notice = L"已保留未应用的参数，请修正后再执行操作。";
        }
        layout(ui);
        ui.pending_command = ui.app.current()->fatal ? 0 : ui.app.enqueue({Action::Apply, std::move(settings), {}, 0});
        update(ui);
        if (ui.page == Page::Home) SetFocus(child(window, HomeBase + std::max(0, homeIndex(old_page))));
        else SetFocus(child(window, Back));
        InvalidateRect(window, nullptr, TRUE); return;
    }
    if (id == Primary) { if (!IsWindowEnabled(child(window, Primary))) return;
        id = ui.page == Page::Enrollment && ui.app.current()->pending_enrollment ? SaveEnrollment : primaryAction(ui.page); }
    if ((notification == EN_CHANGE && id >= PersonId && id <= CustomActions)
        || (notification == CBN_SELCHANGE && (id == Resolution || id == Rotation || id == SearchMode || id == Actions))
        || (notification == BN_CLICKED && (id == Mirror || id == Details || id == Silent || id == Multiple))) {
        ui.dirty = true; ui.parameter_notice.clear(); setText(window, Apply, L"应用修改 *");
    }
    switch (id) {
        case SearchMode:
            if (notification == CBN_SELCHANGE) { checkBox(window, Silent, selection(window, SearchMode) == 0); update(ui); }
            break;
        case Actions: if (notification == CBN_SELCHANGE) update(ui); break;
        case RefreshDevices: refreshDevices(ui); update(ui); break;
        case Open:
            if (selection(window, Device) < 0) throw std::runtime_error("Connect a camera and allow desktop camera access in Windows Settings, then refresh");
            submit(ui, Action::OpenCamera); break;
        case Close: ++ui.app.generation; submit(ui, Action::CloseCamera); break;
        case Apply: ++ui.app.generation; submit(ui, Action::Apply); break;
        case Logs:
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", logDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("Cannot open log directory");
            break;
        case Help: MessageBoxW(window,
            L"首页仅保留 Android 的八个纵向入口；录入、人脸库从人脸搜索页进入。\n"
            L"各页顶部“返回”回到上一级；参数设置在本功能内打开。\n\n"
            L"人脸录入先采集、核对预览和 ID，再确认保存；重拍、取消或离页不会写入人脸库。\n"
            L"先运行 self_test.bat；相机功能先选择设备并打开，再开始本轮流程。更改参数后点“应用参数”。\n"
            L"1:1 验证先用相机、图片或人脸库 ID 建立基准；基准会在本机保存。\n"
            L"图片人脸搜索支持最多 10 张脸、全库 Top-1 / 0.80、不执行活体；点击缩略图或人脸框可选择。\n"
            L"图片搜索快捷键：Ctrl+O 选图、F5 重新搜索、Esc 返回上一级；结果详情可选中复制，Ctrl+A 全选。\n"
            L"人脸库支持分组/标签、每页100条、删除、原始HF1 JSON导入导出、本地图片批量提取。\n"
            L"图片导入使用文件名（不含扩展名）作ID；JSON和批量图片按ID覆盖，取消保留已保存数据。\n"
            L"分辨率为协商目标；镜像只影响显示。应用参数后保存分辨率、旋转和镜像，重启仍可使用。\n"
            L"SDK参数设置只预览，不执行人脸分析；切换独立页面会关闭相机并取消当前流程。\n\n"
            L"数据与日志：%LOCALAPPDATA%\\HiFaceDemo\n"
            L"Windows 10/11 x64、CPU 开发候选版；真人识别和摄像头兼容性需要真机确认。", aboutTitle().c_str(), MB_OK); break;
        case StartFlow:
            if (ui.page == Page::Reference && ui.app.current()->has_reference && MessageBoxW(window, L"本次成功采集后将替换当前 1:1 基准，继续？", L"替换基准", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            ++ui.app.generation; submit(ui, Action::StartFlow); break;
        case Enroll: action(ui, NavBase + static_cast<int>(Page::Enrollment), BN_CLICKED); break;
        case SaveEnrollment:
            EnrollmentDraft::validateMetadata(utf8(text(window, PersonId)), utf8(text(window, Group)), utf8(text(window, Tag)));
            if (ui.app.current()->pending_enrollment && MessageBoxW(window,
                (L"确认将当前预览保存为人员 ID: " + text(window, PersonId) + L"？重复 ID 会拒绝保存。").c_str(),
                L"确认人脸录入", MB_YESNO | MB_ICONQUESTION) == IDYES) { submit(ui, Action::SaveEnrollment); ui.return_after_save = ui.pending_command; }
            break;
        case Cancel: ++ui.app.generation; submit(ui, Action::Cancel); break;
        case Pause:
            if (ui.app.current()->paused && ui.dirty) { ui.parameter_notice = L"请先应用修改，再恢复搜索。"; break; }
            submit(ui, Action::Pause); break;
        case PickA: case PickB: {
            auto files = choose(window, false, false, kImages); if (!files.empty()) submit(ui, id == PickA ? Action::LoadA : Action::LoadB, std::move(files)); break;
        }
        case PickSearchImage: {
            auto files = choose(window, false, false, kImages);
            if (!files.empty()) { ui.search_request_generation = ++ui.app.generation; submit(ui, Action::SearchPhoto, std::move(files)); }
            break;
        }
        case SearchImageAgain: ui.search_request_generation = ++ui.app.generation; submit(ui, Action::SearchPhotoAgain); break;
        case CopySearch: {
            if (!OpenClipboard(window)) break;
            const auto bytes=(ui.last_search_results.size()+1)*sizeof(wchar_t);
            HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
            if (memory) {
                auto data=static_cast<wchar_t*>(GlobalLock(memory));
                if (data) {
                    std::copy(ui.last_search_results.c_str(),ui.last_search_results.c_str()+ui.last_search_results.size()+1,data);
                    GlobalUnlock(memory);
                    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory);
                } else GlobalFree(memory);
            }
            CloseClipboard();break;
        }
        case GoGallery: action(ui, NavBase + static_cast<int>(Page::Gallery), BN_CLICKED); break;
        case Compare: submit(ui, Action::Compare); break;
        case ImageReference: submit(ui, Action::SetImageReference); break;
        case GalleryReference:
            submit(ui, Action::SetGalleryReference);
            if (ui.page == Page::Gallery && ui.history.parent(ui.page) == Page::Verification) ui.return_after_reference = ui.pending_command;
            break;
        case ImportReference: {
            auto files = choose(window, false, false, kFeature); if (!files.empty()) submit(ui, Action::ImportReference, std::move(files)); break;
        }
        case ExportFeature: {
            auto files = choose(window, true, false, kFeature, L"hf1"); if (!files.empty()) submit(ui, Action::ExportFeature, std::move(files)); break;
        }
        case ExportPreview: {
            auto files = choose(window, true, false, L"PNG image\0*.png\0", L"png"); if (!files.empty()) submit(ui, Action::ExportPreview, std::move(files)); break;
        }
        case ImageEnroll: submit(ui, Action::ImageEnroll); break;
        case RefreshGallery: submit(ui, Action::RefreshGallery); break;
        case PreviousPage: submit(ui, Action::PreviousPage); break;
        case NextPage: submit(ui, Action::NextPage); break;
        case DeletePerson:
            if (MessageBoxW(window, (L"确认删除 ID: " + text(window, PersonId) + L"？此操作不能撤销。").c_str(), L"删除人员", MB_YESNO | MB_ICONWARNING) == IDYES) submit(ui, Action::DeletePerson);
            break;
        case UpdatePerson: submit(ui, Action::UpdatePerson); break;
        case ImportJson: case ImportImages: {
            auto files = choose(window, false, id == ImportImages, id == ImportImages ? kImages : kJson);
            if (!files.empty() && MessageBoxW(window, L"将按 Face ID 新增或覆盖记录。JSON 必须匹配当前 HF1 特征空间；图片以文件名为ID。\n取消会保留已保存数据，是否开始？", L"批量导入", MB_YESNO | MB_ICONQUESTION) == IDYES)
                submit(ui, id == ImportImages ? Action::ImportImages : Action::ImportJson, std::move(files));
            break;
        }
        case ExportJson: {
            auto files = choose(window, true, false, kJson, L"json"); if (!files.empty()) submit(ui, Action::ExportJson, std::move(files)); break;
        }
        case ReleaseMemory: submit(ui, Action::ReleaseMemory); break;

    }
    update(ui);
}
bool shortcut(Ui& ui, const MSG& message) {
    if (message.message != WM_KEYDOWN || GetKeyState(VK_MENU) < 0) return false;
    if (ui.page == Page::Home && (message.wParam == VK_PRIOR || message.wParam == VK_NEXT || message.wParam == VK_HOME || message.wParam == VK_END)) {
        const int delta = std::max(96, homeGeometry(ui).viewport.height() - 20);
        scrollHome(ui, message.wParam == VK_HOME ? 0 : message.wParam == VK_END ? 100000 : ui.home_scroll + (message.wParam == VK_PRIOR ? -delta : delta)); return true;
    }
    int id = 0;
    if (message.wParam == VK_ESCAPE) id = Back;
    else if (ui.page == Page::ImageSearch) {
        if (message.wParam == 'O' && GetKeyState(VK_CONTROL) < 0) id = PickSearchImage;
        else if (message.wParam == VK_F5 && GetKeyState(VK_CONTROL) >= 0) id = SearchImageAgain;
    }
    if (!id) return false;
    if (!(message.lParam & (LPARAM(1) << 30)) && IsWindowEnabled(child(ui.app.window, id))) {
        try { action(ui, id, BN_CLICKED); }
        catch (const std::exception& error) {
            writeLog(error.what()); MessageBoxW(ui.app.window, widen(error.what()).c_str(), L"HiFace", MB_OK | MB_ICONERROR);
        }
    }
    return true;
}
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* ui = reinterpret_cast<Ui*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        ui = static_cast<Ui*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); ui->app.window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
    }
    if (!ui) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
            case WM_CREATE: create(*ui); return 0;
            case WM_SIZE: layout(*ui); InvalidateRect(window, nullptr, TRUE); return 0;
            case WM_GETMINMAXINFO: {
                const auto work = workingArea(window);
                reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {std::min<LONG>(ui->theme.px(1000), work.right - work.left), std::min<LONG>(ui->theme.px(600), work.bottom - work.top)}; return 0;
            }
            case WM_DPICHANGED: {
                ui->theme.setDpi(HIWORD(wparam));
                EnumChildWindows(window, [](HWND child, LPARAM context) -> BOOL { reinterpret_cast<Ui*>(context)->theme.style(child); return TRUE; }, reinterpret_cast<LPARAM>(ui));
                const auto* bounds = reinterpret_cast<RECT*>(lparam); SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE); layout(*ui); return 0;
            }
            case WM_ERASEBKGND: return 1;
            case kRevealFocusedControl: {
                const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
                if (ui->page == Page::Home && id >= HomeBase && id < HomeBase + kFeatureCount)
                    scrollHome(*ui, homeGeometry(*ui).reveal(id - HomeBase, ui->home_scroll));
                return 0;
            }
            case WM_MOUSEWHEEL: {
                if (ui->page == Page::Home) { scrollHome(*ui, ui->home_scroll - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 84); return 0; }
                const POINT pointer{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const auto hover = WindowFromPoint(pointer);
                const auto photo = ui->image_search.window();
                if (ui->page == Page::ImageSearch && (hover == photo || IsChild(photo, hover))) return SendMessageW(photo, message, wparam, lparam);
                return IsWindowVisible(ui->pane) ? SendMessageW(ui->pane, message, wparam, lparam) : 0;
            }
            case WM_DRAWITEM: {
                const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if (item.CtlID >= HomeBase && item.CtlID < HomeBase + kFeatureCount) {
                    const auto& feature = page_info[static_cast<int>(feature_pages[item.CtlID - HomeBase])];
                    const bool enabled = !(item.itemState & ODS_DISABLED);
                    card(item.hDC, item.rcItem, colors::surface, colors::line, ui->theme.px(12));
                    RECT title = item.rcItem; InflateRect(&title, -ui->theme.px(16), -ui->theme.px(8));
                    title.bottom = title.top + ui->theme.px(24);
                    label(item.hDC, feature.title, title, ui->theme.body, enabled ? colors::primary : colors::muted,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT detail = title; detail.top = title.bottom + ui->theme.px(4); detail.bottom = item.rcItem.bottom - ui->theme.px(8);
                    label(item.hDC, feature.description, detail, ui->theme.small, colors::muted, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
                    if (item.itemState & ODS_FOCUS) { RECT focus = item.rcItem; InflateRect(&focus, -4, -4); DrawFocusRect(item.hDC, &focus); }
                    return TRUE;
                }
                ui->theme.button(item, item.CtlID == Primary || item.CtlID == Open, item.CtlID == Parameters && ui->parameters_open, false); return TRUE;
            }
            case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: {
                const auto dc = reinterpret_cast<HDC>(wparam); const auto control = reinterpret_cast<HWND>(lparam);
                const auto state = ui->app.current();
                SetTextColor(dc, GetDlgCtrlID(control) == Status && (state->error || state->fatal) ? colors::error : GetDlgCtrlID(control) == Metrics ? colors::muted : colors::ink);
                const bool panel = GetParent(control) == ui->pane;
                SetBkColor(dc, panel || message == WM_CTLCOLOREDIT ? colors::surface : colors::background);
                return reinterpret_cast<LRESULT>(panel || message == WM_CTLCOLOREDIT ? ui->theme.surface : ui->theme.background);
            }
            case WM_NOTIFY: {
                const auto* event = reinterpret_cast<NMHDR*>(lparam);
                if (event->idFrom == People && event->code == LVN_ITEMCHANGED && !ui->rendering_list && !ui->app.current()->busy) {
                    const int index = ListView_GetNextItem(child(window, People), -1, LVNI_SELECTED);
                    if (index >= 0 && static_cast<size_t>(index) < ui->rendered_people.size()) {
                        const auto& person = ui->rendered_people[static_cast<size_t>(index)];
                        setText(window, PersonId, widen(person.id)); setText(window, Group, widen(person.group)); setText(window, Tag, widen(person.tag));
                    }
                }
                return 0;
            }
            case WM_COMMAND: action(*ui, LOWORD(wparam), HIWORD(wparam)); return 0;
            case kFrameReady: ui->app.notification_pending = false; update(*ui); return 0;
            case WM_PAINT: paint(*ui); return 0;
            case WM_CLOSE: ui->app.stop(); DestroyWindow(window); return 0;
            case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        writeLog(error.what()); MessageBoxW(window, widen(error.what()).c_str(), L"HiFace", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        ComApartment com(true); initializeLog("demo");
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
        Ui ui; ui.app.database = demoDataDirectory() / L"hiface-search.db";
        int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw std::runtime_error("Cannot parse command line");
        struct Arguments { wchar_t** values; ~Arguments() { LocalFree(values); } } arguments{argv};
        for (int i = 1; i < argc; ++i) {
            const std::wstring option = argv[i];
            if (i + 1 >= argc) throw std::runtime_error("Usage: hiface_demo [--database FILE] [--camera INDEX]");
            if (option == L"--database") ui.app.database = argv[++i];
            else if (option == L"--camera") {
                const std::wstring value = argv[++i]; size_t end = 0;
                const auto index = std::stoul(value, &end);
                if (end != value.size() || index > 32) throw std::runtime_error("Invalid camera index");
                ui.initial_camera = static_cast<unsigned>(index);
            }
            else throw std::runtime_error("Unknown Demo option");
        }
        WNDCLASSW type{}; type.lpfnWndProc = windowProcedure; type.hInstance = instance;
        type.lpszClassName = L"HiFaceWindowsDemo"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassW(&type)) throw std::runtime_error("Cannot register Demo window");
        const auto work = workingArea(nullptr);
        const int width = std::max(1, std::min(1440, static_cast<int>(work.right - work.left) - 16));
        const int height = std::max(1, std::min(920, static_cast<int>(work.bottom - work.top) - 16));
        HWND window = CreateWindowExW(0, type.lpszClassName, L"HiFace Windows Demo",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, &ui);
        if (!window) throw std::runtime_error("Cannot create Demo window");
        ShowWindow(window, show); UpdateWindow(window);
        MSG message{}; BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!shortcut(ui, message) && !IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if (result == -1) throw std::runtime_error("Windows message loop failed");
        return 0;
    } catch (const std::exception& error) {
        writeLog(error.what()); MessageBoxW(nullptr, widen(error.what()).c_str(), L"HiFace startup failed", MB_OK | MB_ICONERROR); return 1;
    }
}

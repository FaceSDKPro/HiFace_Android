#include "window.h"
#include <chrono>
#include <csignal>
#include <dlfcn.h>
#include <iostream>
#include <unistd.h>
using namespace hiface_linux_demo;
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: hiface_startup MODEL_DIR DATABASE [CAMERA_DEVICE|--smoke-test]\n";
        return 0;
    }
    if (argc < 3 || argc > 4) return 2;
    std::error_code error;
    auto self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) self = std::filesystem::absolute(argv[0]);
    const auto executable = self.parent_path() / "hiface_desktop";
    const auto sdk = self.parent_path().parent_path() / "sdk/lib/libhiface.so.1";
    std::signal(SIGTERM, interrupt); std::signal(SIGINT, interrupt);
    try {
        for (;;) {
            // This executable has no SDK dependency. Even an ELF loader failure
            // can therefore render the same mouse/keyboard-accessible error UI.
            // 启动器不链接 SDK；即使 ELF 加载失败，也能显示同一套可用鼠标/键盘操作的错误页。
            ViewState state; state.failed = true; state.runtime.sdk_loaded = false;
            std::unique_ptr<void, decltype(&dlclose)> library(dlopen(sdk.c_str(), RTLD_NOW | RTLD_LOCAL), dlclose);
            if (library) {
                execv(executable.c_str(), argv);
                state.runtime.sdk_loaded = true;
                state.system_error = errno;
                state.status = "Cannot start Demo executable";
            } else state.status = "SDK could not be loaded";
            DesktopWindow window;
            bool retry = false;
            while (window.open() && !interrupted) {
                for (const auto& command : window.events(state)) if (command.type == "restart") retry = true;
                if (retry) break;
                window.paint(state);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            if (!retry || interrupted) return interrupted ? 0 : 1;
        }
    } catch (...) {
        std::cerr << "Demo unavailable. Demo " << HIFACE_DEMO_VERSION << "; expected SDK "
                  << HIFACE_DEMO_SDK_VERSION << ". " << supportEmail << '\n';
        return 1;
    }
}

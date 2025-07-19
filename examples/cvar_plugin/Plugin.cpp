#include <memory>
#include "uevr/Plugin.hpp"

using namespace uevr;

class CVarPlugin : public Plugin {
public:
    void on_initialize() override {
        API::get()->log_info("CVarPlugin initialized");
        // Example: disable motion blur using the console manager
        auto cm = API::get()->get_console_manager();
        if (cm != nullptr) {
            if (auto cvar = cm->find_variable(L"r.DefaultFeature.MotionBlur")) {
                cvar->set(0);
                API::get()->log_info("Set r.DefaultFeature.MotionBlur to 0");
            } else {
                API::get()->log_error("Failed to find r.DefaultFeature.MotionBlur");
            }
        }
    }
};

extern "C" __declspec(dllexport) void uevr_set_cvar_int(const wchar_t* name, int value) {
    auto cm = API::get()->get_console_manager();
    if (cm != nullptr && name != nullptr) {
        if (auto cvar = cm->find_variable(name)) {
            cvar->set(value);
        }
    }
}

std::unique_ptr<CVarPlugin> g_plugin{new CVarPlugin()};

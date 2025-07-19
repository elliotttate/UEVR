#include <uevr/Plugin.hpp>
#include <openxr/openxr.h>

using namespace uevr;

class HZBOcclusionPlugin : public Plugin {
public:
    HZBOcclusionPlugin() = default;

    void on_initialize() override {
        // Re-enable engine HZB occlusion if UEVR disabled it
        API::VR::set_mod_value("VR_DisableHZBOcclusion", false);
        auto set_cvar = API::get()->param()->sdk->functions->set_cvar_int;
        set_cvar("Renderer", "r.HZBOcclusion", 1);

        if (API::VR::is_openxr()) {
            auto xr_data = API::get()->param()->openxr;
            XrInstance instance = xr_data->get_xr_instance();
            PFN_xrGetVisibilityMaskKHR getMask{nullptr};
            if (XR_SUCCEEDED(xrGetInstanceProcAddr(instance, "xrGetVisibilityMaskKHR",
                                                  reinterpret_cast<PFN_xrVoidFunction*>(&getMask))) && getMask) {
                API::get()->log_info("OpenXR visibility mask extension available");
            } else {
                API::get()->log_info("OpenXR visibility mask extension not available");
            }
        } else {
            API::get()->log_info("Runtime is not OpenXR");
        }
    }
};

std::unique_ptr<HZBOcclusionPlugin> g_plugin{ new HZBOcclusionPlugin() };

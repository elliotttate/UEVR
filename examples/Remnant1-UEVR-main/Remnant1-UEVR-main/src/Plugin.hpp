#pragma once

#include <windows.h>

#include <memory>
#include "uevr/API.hpp"
#include "uevr/Plugin.hpp"
#include <glm/glm.hpp>
#include <utility/PointerHook.hpp>

#define PLUGIN_LOG_ONCE(...) { \
    static bool _logged_ = false; \
    if (!_logged_) { \
        _logged_ = true; \
        API::get()->log_info(__VA_ARGS__); \
    } }

#define PLUGIN_LOG_ONCE_ERROR(...) { \
    static bool _logged_ = false; \
    if (!_logged_) { \
        _logged_ = true; \
        API::get()->log_error(__VA_ARGS__); \
    } }

// Global accessor for our plugin.
class RemnantPlugin;
extern std::unique_ptr<RemnantPlugin> g_plugin;

class RemnantPlugin : public uevr::Plugin {
public:
    RemnantPlugin() = default;
    virtual ~RemnantPlugin();

    void on_initialize() override;
    void on_pre_engine_tick(uevr::API::UGameEngine* engine, float delta) override;

private:
    void hook_onfire_fn();
    
    void SetGunAdjustValuesFromConfig(const std::filesystem::path& ConfigFile);
    static void* on_get_onfire_internal(uevr::API::UObject* weapon, UEVR_Vector3f* from_vec, UEVR_Vector3f* to_vec, float WeaponSpread);
    static void* on_get_onfire(uevr::API::UObject* weapon, UEVR_Vector3f* from_vec, UEVR_Vector3f* to_vec, float WeaponSpread) {
        return on_get_onfire_internal(weapon, from_vec, to_vec, WeaponSpread );
    }

    bool m_hooked{false};
    int32_t m_onfire_hook_id{};
    using OnFireFn = void*(*)(uevr::API::UObject*, UEVR_Vector3f*, UEVR_Vector3f*, float);
    inline static OnFireFn m_onfire_hook_fn{};
};


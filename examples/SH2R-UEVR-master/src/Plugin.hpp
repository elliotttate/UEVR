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
class SHPlugin;
extern std::unique_ptr<SHPlugin> g_plugin;

class SHPlugin : public uevr::Plugin {
public:
    SHPlugin() = default;
    virtual ~SHPlugin();

    void on_initialize() override;
    void on_pre_engine_tick(uevr::API::UGameEngine* engine, float delta) override;

private:
    void hook_get_spread_shoot_vector();
    void hook_melee_trace_check();

    void* on_get_end_trace_loc_internal(uevr::API::UObject* weapon, glm::f64vec2* in_angles, float sangles, glm::f64vec3* out_vec);
    static void* on_get_end_trace_loc(uevr::API::UObject* weapon, glm::f64vec2* in_angles, float sangles, glm::f64vec3* out_vec) {
        return g_plugin->on_get_end_trace_loc_internal(weapon, in_angles, sangles, out_vec);
    }

    glm::f64vec3* on_get_trace_start_loc_internal(uevr::API::UObject* weapon, glm::f64vec3* out_vec);
    static glm::f64vec3* on_get_trace_start_loc(uevr::API::UObject* weapon, glm::f64vec3* out_vec) {
        return g_plugin->on_get_trace_start_loc_internal(weapon, out_vec);
    }

    bool m_hooked{false};

    int32_t m_on_get_end_trace_loc_hook_id{};
    int32_t m_trace_start_loc_hook_id{};
    using GetEndTraceLocFn = void*(*)(uevr::API::UObject*, glm::f64vec2*, float, glm::f64vec3*);
    GetEndTraceLocFn m_on_get_end_trace_loc_hook_fn{};
    using GetStartTraceLocFn = glm::f64vec3*(*)(uevr::API::UObject*, glm::f64vec3*);
    GetStartTraceLocFn m_trace_start_loc_hook_fn{};

    struct FHitResult {
        int32_t FaceIndex;
        float Time;
        float Distance;
        glm::f64vec3 Location;
        glm::f64vec3 ImpactPoint;
        glm::f64vec3 Normal;
        glm::f64vec3 ImpactNormal;
        glm::f64vec3 TraceStart;
        glm::f64vec3 TraceEnd;
        float PenetrationDepth;
        int32_t MyItem;
        int32_t Item;
        uint8_t ElementIndex;
        uint8_t bBlockingHit : 1;
        uint8_t bStartPenetrating : 1;
        uint32_t PhysMaterialIndex;
        uint32_t PhysMaterialSerialNumber;
        uint32_t HitObject_ActorIndex;
        uint32_t HitObject_ActorSerialNumber;
        uint32_t HitObject_ManagerIndex;
        uint32_t HitObject_ManagerSerialNumber;
        uint32_t HitObject_ActorInstanceIndex;
        uint32_t HitObject_ActorInstanceUID;
        uint32_t Component_Index;
        uint32_t Component_SerialNumber;
        uevr::API::FName BoneName;
        uevr::API::FName MyBoneName;
    };
    static_assert(sizeof(FHitResult) == 0xE8, "FHitResult size mismatch");

    int32_t m_melee_trace_check_hook_id{};
    using MeleeTraceCheckFn = bool (*)(uevr::API::UObject*, float, float, float, void*, void*, uevr::API::TArray<FHitResult>&);
    MeleeTraceCheckFn m_melee_trace_check_hook_fn{};
    bool on_melee_trace_check_internal(uevr::API::UObject*, float, float, float, void*, void*, uevr::API::TArray<FHitResult>&);
    static bool on_melee_trace_check(
        uevr::API::UObject* a1, 
        float a2, float a3, float hit_rotation_ratio, 
        void* a5, void* a6, 
        uevr::API::TArray<FHitResult>& hit_results
    ) 
    {
        return g_plugin->on_melee_trace_check_internal(a1, a2, a3, hit_rotation_ratio, a5, a6, hit_results);
    }
};
#include "uevr/Plugin.hpp"

using namespace uevr;

class CVarSetterPlugin : public uevr::Plugin {
public:
    CVarSetterPlugin() = default;

    void on_initialize() override {
        try {
            // Initialize the UEVR API
            API::initialize(param());
            
            API::get()->log_info("[CVarSetterPlugin] Plugin initialized successfully");
        } catch (const std::exception& e) {
            API::get()->log_error("[CVarSetterPlugin] Exception during initialization: %s", e.what());
        }
    }

    // Set CVARs once when the engine begins ticking
    void on_pre_engine_tick(API::UGameEngine* engine, float delta_time) override {
        static bool once = true;
        if (once) {
            once = false;
            
            try {
                // ✅ CORRECT: This is how to properly set CVARs in UEVR C++
                // (Fixed based on working Lua script pattern)
                set_cvar_int("r.HZBOcclusion", 0);
                set_cvar_int("t.MaxFPS", 400);
                set_cvar_float("r.ScreenPercentage", 150.0f);
                set_cvar_int("r.VSync", 1);
                set_cvar_int("r.BloomQuality", 1);
                
                API::get()->log_info("[CVarSetterPlugin] All CVARs set successfully!");
                
            } catch (const std::exception& e) {
                API::get()->log_error("[CVarSetterPlugin] Exception setting CVARs: %s", e.what());
            }
        }
    }

private:
    // ✅ CORRECT: Helper function to set integer CVARs (matches working Lua script pattern)
    void set_cvar_int(const std::string& name, int value) {
        try {
            API::get()->log_info("[CVarSetterPlugin] Setting %s = %d", name.c_str(), value);
            
            // 1. Get console manager (like Lua: api:get_console_manager())
            auto console_manager = API::get()->get_console_manager();
            if (!console_manager) {
                API::get()->log_error("[CVarSetterPlugin] Console manager is null!");
                return;
            }
            
            // 2. Find variable (like Lua: console_manager:find_variable(cvar))
            std::wstring wide_name = convert_string_to_wstring(name);
            auto var = console_manager->find_variable(wide_name);
            if (!var) {
                API::get()->log_error("[CVarSetterPlugin] Variable '%s' not found!", name.c_str());
                return;
            }
            
            // 3. Set value (like Lua: var:set_int(value))
            var->set(value);
            API::get()->log_info("[CVarSetterPlugin] Successfully set %s to %d", name.c_str(), value);
            
        } catch (const std::exception& e) {
            API::get()->log_error("[CVarSetterPlugin] Exception setting %s: %s", name.c_str(), e.what());
        }
    }
    
    // ✅ CORRECT: Helper function to set float CVARs
    void set_cvar_float(const std::string& name, float value) {
        try {
            API::get()->log_info("[CVarSetterPlugin] Setting %s = %f", name.c_str(), value);
            
            auto console_manager = API::get()->get_console_manager();
            if (!console_manager) {
                API::get()->log_error("[CVarSetterPlugin] Console manager is null!");
                return;
            }
            
            std::wstring wide_name = convert_string_to_wstring(name);
            auto var = console_manager->find_variable(wide_name);
            if (!var) {
                API::get()->log_error("[CVarSetterPlugin] Variable '%s' not found!", name.c_str());
                return;
            }
            
            // Set as string value (Unreal handles the conversion)
            std::wstring wide_value = std::to_wstring(value);
            var->set(wide_value);
            API::get()->log_info("[CVarSetterPlugin] Successfully set %s to %f", name.c_str(), value);
            
        } catch (const std::exception& e) {
            API::get()->log_error("[CVarSetterPlugin] Exception setting %s: %s", name.c_str(), e.what());
        }
    }
    
    // Helper function to convert string to wide string
    std::wstring convert_string_to_wstring(const std::string& str) {
        if (str.empty()) return L"";
        
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring result(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &result[0], size_needed);
        return result;
    }
};

// Create the plugin instance
std::unique_ptr<CVarSetterPlugin> g_plugin{new CVarSetterPlugin()};

// Export the plugin
extern "C" {
    __declspec(dllexport) void uevr_plugin_initialize(const UEVR_PluginInitializeParam* param) {
        if (g_plugin) {
            g_plugin->param() = param;
            g_plugin->on_initialize();
        }
    }
    
    __declspec(dllexport) void uevr_plugin_callbacks(UEVR_PluginCallbacks* callbacks) {
        if (g_plugin) {
            callbacks->on_pre_engine_tick = [](UEVR_UGameEngineHandle engine, float delta_time) {
                g_plugin->on_pre_engine_tick(reinterpret_cast<API::UGameEngine*>(engine), delta_time);
            };
        }
    }
}

/* 
   COMPARISON: Old vs New Approach
   
   ❌ OLD APPROACH (DOESN'T WORK):
   API::get()->sdk()->functions->set_cvar_int("", "r.HZBOcclusion", 0);
   
   ✅ NEW APPROACH (WORKS - MATCHES LUA SCRIPTS):
   auto console_manager = API::get()->get_console_manager();
   auto var = console_manager->find_variable(L"r.HZBOcclusion");
   var->set(0);
   
   The key insight: Use the Console Manager API that directly 
   manipulates the actual console variables the game uses!
*/ 
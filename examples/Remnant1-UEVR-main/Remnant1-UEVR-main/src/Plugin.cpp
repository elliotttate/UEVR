#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

#include <utility/Scan.hpp>
#include <utility/Module.hpp>
#include <utility/String.hpp>

#include "Plugin.hpp"

using namespace uevr;

#define MAX_DEBUG_MESSAGE_SIZE 2048
bool g_DebugEnabled = false;
float g_AdjustValueV = 0;
float g_AdjustValueH = 0;

static void DebugPrint(const char* Format, ...)
{
  char FormattedMessage[MAX_DEBUG_MESSAGE_SIZE];    
  va_list ArgPtr = NULL;  
  
  if(g_DebugEnabled)    
  {        
    /* Generate the formatted debug message. */        
    va_start(ArgPtr, Format);        
    vsprintf(FormattedMessage, Format, ArgPtr);        
    va_end(ArgPtr); 
    
    API::get()->log_info(FormattedMessage);
  }
}
std::unique_ptr<RemnantPlugin> g_plugin = std::make_unique<RemnantPlugin>();

RemnantPlugin::~RemnantPlugin() {

}

void RemnantPlugin::on_initialize() {
    PLUGIN_LOG_ONCE("RemnantPlugin::on_initialize()");

    // Get the persistent directory
    std::filesystem::path config_file = API::get()->get_persistent_dir() / "plugins" / "remnant.txt";
    std::filesystem::path debug_file = API::get()->get_persistent_dir() / "debug";
    
    SetGunAdjustValuesFromConfig(config_file);
    API::get()->log_info("remnant.dll: using config file value for gun adjust v:%f h:%f", g_AdjustValueV, g_AdjustValueH);
    // Check if the file exists
    g_DebugEnabled = std::filesystem::exists(debug_file);
    
    hook_onfire_fn();
}

void RemnantPlugin::on_pre_engine_tick(uevr::API::UGameEngine* engine, float delta) {

}

//Find vtable real function and hook it
int32_t hook_vtable_fn(std::wstring_view class_name, std::wstring_view fn_name, void* destination, void** original) {
    auto obj = (API::UClass*)API::get()->find_uobject(class_name);

    if (obj == nullptr) {
        PLUGIN_LOG_ONCE_ERROR("Failed to find %ls", class_name.data());
        return -1;
    }

    auto fn = obj->find_function(fn_name);

    if (fn == nullptr) {
        PLUGIN_LOG_ONCE_ERROR("Failed to find %ls", fn_name.data());
        return -1;
    }

    auto native = fn->get_native_function();

    if (native == nullptr) {
        PLUGIN_LOG_ONCE_ERROR("Failed to get native function");
        return -1;
    }

    PLUGIN_LOG_ONCE("%ls native: 0x%p", fn_name.data(), native);

    auto default_object = obj->get_class_default_object();

    if (default_object == nullptr) {
        PLUGIN_LOG_ONCE_ERROR("Failed to get default object");
        return -1;
    }

    auto insn = utility::scan_disasm((uintptr_t)native, 0x1000, "FF 90 ? ? ? ?");

    if (!insn) {
        PLUGIN_LOG_ONCE_ERROR("Failed to find the instruction");
        return -1;
    }

    auto offset = *(int32_t*)(*insn + 2);

    auto vtable = *(uintptr_t**)default_object;
    auto real_fn = vtable[offset / sizeof(void*)];

    PLUGIN_LOG_ONCE("Real %ls: 0x%p (index: %d, offset 0x%X)", fn_name.data(), real_fn, offset / sizeof(void*), offset);

    return API::get()->param()->functions->register_inline_hook((void*)real_fn, (void*)destination, original);
}

//Weapon trace hook
void RemnantPlugin::hook_onfire_fn() {
    m_onfire_hook_id = hook_vtable_fn(L"Class /Script/GunfireRuntime.RangedWeapon", L"OnFire", on_get_onfire, (void**)&m_onfire_hook_fn);
}

//Internal weapon trace function
void* RemnantPlugin::on_get_onfire_internal(uevr::API::UObject* weapon, UEVR_Vector3f* from_vec, UEVR_Vector3f* to_vec, float WeaponSpread) {    
    
    auto muzzlepoint_component = weapon->get_property<API::UObject*>(L"MuzzlePoint");
    
    const auto weapon_name = weapon->get_full_name();
    //DebugPrint("Current Weapon: %ls", weapon_name.c_str());
    //printf("Current Weapon: %ls", weapon_name.c_str());

    if (muzzlepoint_component != nullptr) {
        PLUGIN_LOG_ONCE("MuzzlePoint: 0x%p", muzzlepoint_component);

        UEVR_Vector3f location_params;
        muzzlepoint_component->call_function(L"K2_GetComponentLocation", &location_params);
        UEVR_Vector3f from_vec = location_params;


        //DebugPrint("Current from: %f, %f, %f", (float)from_vec.x, (float)from_vec.y, (float)from_vec.z);

        auto root_component = weapon->get_property<API::UObject*>(L"MuzzlePoint_Root");

        if (root_component != nullptr) {
            PLUGIN_LOG_ONCE("Root: 0x%p", root_component);

            UEVR_Vector3f forward_params;
            root_component->call_function(L"GetForwardVector", &forward_params);


            //DebugPrint("MuzzlePoint location: %f, %f, %f", location_params.x, location_params.y, location_params.z);
            //DebugPrint("MuzzlePoint forward: %f, %f, %f", forward_params.x, forward_params.y, forward_params.z);
            
            float scaler = 8192.0;           
            
            to_vec->x = location_params.x + (forward_params.x * scaler);
            to_vec->y = location_params.y + (forward_params.y * scaler) - g_AdjustValueH; // left and right is Y
            to_vec->z = location_params.z + (forward_params.z * scaler) - g_AdjustValueV; // up and down is Z

            //DebugPrint("MuzzlePoint to: %f, %f, %f", to_vec->x, to_vec->y, to_vec->z); 
        }
    }

    //DebugPrint("Hook functional");
    //Call original function with our replaced parameters
    auto result = m_onfire_hook_fn(weapon, from_vec, to_vec, WeaponSpread);
    PLUGIN_LOG_ONCE("Result: 0x%p", result);

    return result;
}


//***************************************************************************************************
// Reads the config file cvars.txt and stores it in a linked list of CVAR_ITEMs.
//***************************************************************************************************
void RemnantPlugin::SetGunAdjustValuesFromConfig(const std::filesystem::path& ConfigFile) {
    // Check if the file exists
    if (!std::filesystem::exists(ConfigFile)) {
        g_AdjustValueV = 0.0f;
        g_AdjustValueH = 0.0f;
        return;
    }

    std::ifstream file(ConfigFile);
    std::string line;

    while (std::getline(file, line)) {
        // Trim leading and trailing spaces
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        // Skip comments and blank lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Look for the "GunAdjustV=" prefix
        if (line.rfind("GunAdjustV=", 0) == 0) {
            std::string value_str = line.substr(11); // Extract the value as a string
            char* end_ptr = nullptr;
            float value = std::strtof(value_str.c_str(), &end_ptr);

            // Validate conversion
            if (end_ptr != value_str.c_str() && *end_ptr == '\0') {
                g_AdjustValueV = value; // Successfully parsed float
            } else {
                g_AdjustValueV = 0.0f; // Invalid float
            }
        }
        
        // Look for the "GunAdjustH=" prefix
        if (line.rfind("GunAdjustH=", 0) == 0) {
            std::string value_str = line.substr(11); // Extract the value as a string
            char* end_ptr = nullptr;
            float value = std::strtof(value_str.c_str(), &end_ptr);

            // Validate conversion
            if (end_ptr != value_str.c_str() && *end_ptr == '\0') {
                g_AdjustValueH = value; // Successfully parsed float
            } else {
                g_AdjustValueH = 0.0f; // Invalid float
            }
        }
    }
}



#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "..\_Script_CoreUObject\Object.hpp"
namespace _Script_Engine {
struct CameraShakePattern;
}
namespace _Script_CoreUObject {
struct Class;
}
namespace _Script_Engine {
struct PlayerCameraManager;
}
namespace _Script_Engine {
#pragma pack(push, 1)
struct CameraShakeBase : public _Script_CoreUObject::Object {
    private: char pad_28[0x88]; public:
    bool get_bSingleInstance();
    void set_bSingleInstance(bool value);
    float& get_ShakeScale();
    _Script_Engine::CameraShakePattern*& get_RootShakePattern();
    _Script_Engine::PlayerCameraManager*& get_CameraManager();
    static _Script_CoreUObject::Class* static_class();
    void SetRootShakePattern(_Script_Engine::CameraShakePattern* InPattern);
    _Script_Engine::CameraShakePattern* GetRootShakePattern();
}; // Size: 0xb0
#pragma pack(pop)
}

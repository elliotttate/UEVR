#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "Actor.hpp"
namespace _Script_Engine {
struct CameraShakeSourceComponent;
}
namespace _Script_CoreUObject {
struct Class;
}
namespace _Script_Engine {
#pragma pack(push, 1)
struct CameraShakeSourceActor : public Actor {
    private: char pad_248[0x8]; public:
    _Script_Engine::CameraShakeSourceComponent*& get_CameraShakeSourceComponent();
    static _Script_CoreUObject::Class* static_class();
}; // Size: 0x250
#pragma pack(pop)
}

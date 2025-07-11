#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
namespace _Script_CoreUObject {
struct Class;
}
namespace _Script_Engine {
#pragma pack(push, 1)
struct CameraShakeUpdateResult {
    private: char pad_0[0x620]; public:
    static _Script_CoreUObject::Class* static_class();
}; // Size: 0x620
#pragma pack(pop)
}

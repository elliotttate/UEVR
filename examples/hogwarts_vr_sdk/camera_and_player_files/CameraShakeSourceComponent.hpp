#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include "..\_Script_CoreUObject\Rotator.hpp"
#include "SceneComponent.hpp"
namespace _Script_CoreUObject {
struct Class;
}
namespace _Script_CoreUObject {
struct Vector;
}
namespace _Script_Engine {
#pragma pack(push, 1)
struct CameraShakeSourceComponent : public SceneComponent {
    private: char pad_220[0x20]; public:
    void* get_Attenuation();
    float& get_InnerAttenuationRadius();
    float& get_OuterAttenuationRadius();
    void* get_CameraShake();
    bool get_bAutoStart();
    void set_bAutoStart(bool value);
    static _Script_CoreUObject::Class* static_class();
    void StopAllCameraShakesOfType(void* InCameraShake, bool bImmediately);
    void StopAllCameraShakes(bool bImmediately);
    void StartCameraShake(void* InCameraShake, float Scale, void* PlaySpace, _Script_CoreUObject::Rotator UserPlaySpaceRot);
    void Start();
    float GetAttenuationFactor(_Script_CoreUObject::Vector& Location);
}; // Size: 0x240
#pragma pack(pop)
}

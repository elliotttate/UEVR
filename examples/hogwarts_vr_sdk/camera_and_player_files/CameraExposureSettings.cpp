#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "CameraExposureSettings.hpp"
#include "CurveFloat.hpp"
#include "Texture.hpp"
void* _Script_Engine::CameraExposureSettings::get_Method() {
    return (void*)((uintptr_t)this + 0x0);
}
float& _Script_Engine::CameraExposureSettings::get_LowPercent() {
    return *(float*)((uintptr_t)this + 0x4);
}
float& _Script_Engine::CameraExposureSettings::get_Bias() {
    return *(float*)((uintptr_t)this + 0x1c);
}
float& _Script_Engine::CameraExposureSettings::get_HighPercent() {
    return *(float*)((uintptr_t)this + 0x8);
}
float& _Script_Engine::CameraExposureSettings::get_SpeedDown() {
    return *(float*)((uintptr_t)this + 0x18);
}
float& _Script_Engine::CameraExposureSettings::get_MinBrightness() {
    return *(float*)((uintptr_t)this + 0xc);
}
float& _Script_Engine::CameraExposureSettings::get_SpeedUp() {
    return *(float*)((uintptr_t)this + 0x14);
}
float& _Script_Engine::CameraExposureSettings::get_MaxBrightness() {
    return *(float*)((uintptr_t)this + 0x10);
}
_Script_Engine::CurveFloat*& _Script_Engine::CameraExposureSettings::get_BiasCurve() {
    return *(_Script_Engine::CurveFloat**)((uintptr_t)this + 0x20);
}
_Script_Engine::Texture*& _Script_Engine::CameraExposureSettings::get_MeterMask() {
    return *(_Script_Engine::Texture**)((uintptr_t)this + 0x28);
}
float& _Script_Engine::CameraExposureSettings::get_HistogramLogMin() {
    return *(float*)((uintptr_t)this + 0x30);
}
float& _Script_Engine::CameraExposureSettings::get_HistogramLogMax() {
    return *(float*)((uintptr_t)this + 0x34);
}
float& _Script_Engine::CameraExposureSettings::get_CalibrationConstant() {
    return *(float*)((uintptr_t)this + 0x38);
}
bool _Script_Engine::CameraExposureSettings::get_ApplyPhysicalCameraExposure() {
    return (*(uint8_t*)((uintptr_t)this + 0x3c + 0)) & 1 != 0;
}
void _Script_Engine::CameraExposureSettings::set_ApplyPhysicalCameraExposure(bool value) {
    const auto cur_value = *(uint8_t*)((uintptr_t)this + 0x3c + 0);
    *(uint8_t*)((uintptr_t)this + 0x3c + 0) = (cur_value & ~1) | (value ? 1 : 0);
}
_Script_CoreUObject::Class* _Script_Engine::CameraExposureSettings::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"ScriptStruct /Script/Engine.CameraExposureSettings");
    return result;
}

#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "..\_Script_CoreUObject\Rotator.hpp"
#include "..\_Script_CoreUObject\Vector.hpp"
#include "CameraShakeSourceComponent.hpp"
#include "SceneComponent.hpp"
float& _Script_Engine::CameraShakeSourceComponent::get_InnerAttenuationRadius() {
    return *(float*)((uintptr_t)this + 0x21c);
}
void* _Script_Engine::CameraShakeSourceComponent::get_Attenuation() {
    return (void*)((uintptr_t)this + 0x218);
}
void _Script_Engine::CameraShakeSourceComponent::set_bAutoStart(bool value) {
    const auto cur_value = *(uint8_t*)((uintptr_t)this + 0x230 + 0);
    *(uint8_t*)((uintptr_t)this + 0x230 + 0) = (cur_value & ~1) | (value ? 1 : 0);
}
float& _Script_Engine::CameraShakeSourceComponent::get_OuterAttenuationRadius() {
    return *(float*)((uintptr_t)this + 0x220);
}
void _Script_Engine::CameraShakeSourceComponent::StopAllCameraShakesOfType(void* InCameraShake, bool bImmediately) {
    return;
}
void* _Script_Engine::CameraShakeSourceComponent::get_CameraShake() {
    return (void*)((uintptr_t)this + 0x228);
}
_Script_CoreUObject::Class* _Script_Engine::CameraShakeSourceComponent::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"Class /Script/Engine.CameraShakeSourceComponent");
    return result;
}
bool _Script_Engine::CameraShakeSourceComponent::get_bAutoStart() {
    return (*(uint8_t*)((uintptr_t)this + 0x230 + 0)) & 1 != 0;
}
void _Script_Engine::CameraShakeSourceComponent::StopAllCameraShakes(bool bImmediately) {
    return;
}
void _Script_Engine::CameraShakeSourceComponent::StartCameraShake(void* InCameraShake, float Scale, void* PlaySpace, _Script_CoreUObject::Rotator UserPlaySpaceRot) {
    return;
}
void _Script_Engine::CameraShakeSourceComponent::Start() {
    return;
}
float _Script_Engine::CameraShakeSourceComponent::GetAttenuationFactor(_Script_CoreUObject::Vector& Location) {
    return;
}

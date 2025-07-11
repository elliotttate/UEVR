#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "CameraShakeInfo.hpp"
float& _Script_Engine::CameraShakeInfo::get_BlendOut() {
    return *(float*)((uintptr_t)this + 0xc);
}
float& _Script_Engine::CameraShakeInfo::get_BlendIn() {
    return *(float*)((uintptr_t)this + 0x8);
}
void* _Script_Engine::CameraShakeInfo::get_Duration() {
    return (void*)((uintptr_t)this + 0x0);
}
_Script_CoreUObject::Class* _Script_Engine::CameraShakeInfo::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"ScriptStruct /Script/Engine.CameraShakeInfo");
    return result;
}

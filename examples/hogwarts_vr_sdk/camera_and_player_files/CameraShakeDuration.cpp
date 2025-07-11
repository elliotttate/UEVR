#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "CameraShakeDuration.hpp"
float& _Script_Engine::CameraShakeDuration::get_Duration() {
    return *(float*)((uintptr_t)this + 0x0);
}
void* _Script_Engine::CameraShakeDuration::get_Type() {
    return (void*)((uintptr_t)this + 0x4);
}
_Script_CoreUObject::Class* _Script_Engine::CameraShakeDuration::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"ScriptStruct /Script/Engine.CameraShakeDuration");
    return result;
}

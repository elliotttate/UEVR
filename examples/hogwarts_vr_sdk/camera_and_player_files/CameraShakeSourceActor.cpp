#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "Actor.hpp"
#include "CameraShakeSourceActor.hpp"
#include "CameraShakeSourceComponent.hpp"
_Script_Engine::CameraShakeSourceComponent*& _Script_Engine::CameraShakeSourceActor::get_CameraShakeSourceComponent() {
    return *(_Script_Engine::CameraShakeSourceComponent**)((uintptr_t)this + 0x248);
}
_Script_CoreUObject::Class* _Script_Engine::CameraShakeSourceActor::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"Class /Script/Engine.CameraShakeSourceActor");
    return result;
}

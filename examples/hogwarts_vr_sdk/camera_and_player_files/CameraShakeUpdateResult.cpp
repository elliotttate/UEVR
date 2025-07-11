#include "..\FUObjectArray.hpp"
#include "..\_Script_CoreUObject\Class.hpp"
#include "CameraShakeUpdateResult.hpp"
_Script_CoreUObject::Class* _Script_Engine::CameraShakeUpdateResult::static_class() {
    static auto result = (_Script_CoreUObject::Class*)FUObjectArray::get()->find_uobject(L"ScriptStruct /Script/Engine.CameraShakeUpdateResult");
    return result;
}

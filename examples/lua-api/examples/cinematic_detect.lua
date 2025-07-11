

local api = uevr.api
local vr = uevr.params.vr
local is_in_cinematic = false


-------------------------------------------------------------------------------
-- hook_function
--
-- Hooks a UEVR function. 
--
-- class_name = the class to find, such as "Class /Script.GunfireRuntime.RangedWeapon"
-- function_name = the function to Hook
-- native = true or false whether or not to set the native function flag.
-- prefn = the function to run if you hook pre. Pass nil to not use
-- postfn = the function to run if you hook post. Pass nil to not use.
-- dbgout = true to print the debug outputs, false to not
--
-- Example:
--    hook_function("Class /Script/GunfireRuntime.RangedWeapon", "OnFireBegin", true, nil, gun_firingbegin_hook, true)
--
-- Returns: true on success, false on failure.
-------------------------------------------------------------------------------
local function hook_function(class_name, function_name, native, prefn, postfn, dbgout)
    local result = false
    local class_obj = uevr.api:find_uobject(class_name)
    if(class_obj ~= nil) then
        if dbgout then print("hook_function: found class obj for", class_name) end
        local class_fn = class_obj:find_function(function_name)
        if(class_fn ~= nil) then 
            if dbgout then print("hook_function: found function", function_name, "for", class_name) end
            if (native == true) then
                class_fn:set_function_flags(class_fn:get_function_flags() | 0x400)
                if dbgout then print("hook_function: set native flag") end
            end
            
            class_fn:hook_ptr(prefn, postfn)
            result = true
            if dbgout then print("hook_function: set function hook for", prefn, "and", postfn) end
        end
    end
    
    return result
end

--void post(fn: UFunction*, obj: UObject*, locals: StructObject*, result: void*)
--void SetCinematicMode(bool bInCinematicMode, bool bHidePlayer, bool bAffectsHUD, bool bAffectsMovement, bool bAffectsTurning);

local function SetCinematicHook(fn, obj, locals, result)
    print("SetCinematicHook obj: ", obj:get_full_name())
    print("In cinematicmode: ", locals.bInCinematicMode)
    
    is_in_cinematic = locals.bInCinematicMode
end

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    local pawn = api:get_local_pawn(0)
    if pawn ~= nil then
        local pawn_name = pawn:get_full_name()
        if(pawn_name ~= nil) then
            --cinema
            --BP_ChronosCameraPawn_C /Game/Maps/Game/EP1/S1/S1_A/EP1_S1_A.EP1_S1_A.PersistentLevel.BP_ChronosCameraPawn_C_2147472473

            --not movie
            --BP_Max_Coat01A_C /Game/Maps/Game/EP1/S1/S1_A/EP1_S1_A_ActionScript.EP1_S1_A_ActionScript.PersistentLevel.Max
            if(string.find(pawn_name, "ChronosCameraPawn_C")) then
                UEVR_UObjectHook.set_disabled(true);
            else
                UEVR_UObjectHook.set_disabled(false);
            end
            
        end
        
    end
end)

--hook_function("Class /Script/Engine.PlayerController", "SetCinematicMode", true, nil, SetCinematicHook, true)


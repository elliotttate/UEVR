
local api = uevr.api
local vr = uevr.params.vr
local is_in_cinematic = false
------------------------------------------------------------------------------------
-- Helper section
------------------------------------------------------------------------------------

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
	if(dbgout) then print("Hook_function for ", class_name, function_name) end
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

-------------------------------------------------------------------------------
-- returns local pawn
-------------------------------------------------------------------------------
local function get_local_pawn()
	return api:get_local_pawn(0)
end

-------------------------------------------------------------------------------
-- returns local player controller
-------------------------------------------------------------------------------
local function get_player_controller()
	return api:get_player_controller(0)
end

-------------------------------------------------------------------------------
-- Logs to the log.txt
-------------------------------------------------------------------------------
local function log_info(message)
	uevr.params.functions.log_info(message)
end

-------------------------------------------------------------------------------
-- Print all instance names of a class to debug console
-------------------------------------------------------------------------------
local function PrintInstanceNames(class_to_search)
	local obj_class = api:find_uobject(class_to_search)
    if obj_class == nil then 
		print(class_to_search, "was not found") 
		return
	end

    local obj_instances = obj_class:get_objects_matching(false)

    for i, instance in ipairs(obj_instances) do
		print(i, instance:get_fname():to_string(), mesh:get_full_name())
	end
end

-------------------------------------------------------------------------------
-- Get first instance of a given class object
-------------------------------------------------------------------------------
local function GetFirstInstance(class_to_search)
	local obj_class = api:find_uobject(class_to_search)
    if obj_class == nil then 
		print(class_to_search, "was not found") 
		return nil
	end

    return obj_class:get_first_object_matching(false)
end

-------------------------------------------------------------------------------
-- Get class object instance matching string
-------------------------------------------------------------------------------
local function GetInstanceMatching(class_to_search, match_string)
	local obj_class = api:find_uobject(class_to_search)
    if obj_class == nil then 
		print(class_to_search, "was not found") 
		return nil
	end

    local obj_instances = obj_class:get_objects_matching(false)

    for i, instance in ipairs(obj_instances) do
        if string.find(instance:get_full_name(), match_string) then
			return instance
		end
	end
end


-------------------------------------------------------------------------------
-- Example hook pre function. Post is same but no return.
-------------------------------------------------------------------------------

local function ShiftVFX_OnStart(fn, obj, locals, result)
    print("Shift beginning: ")
    log_info("LUA: Shift beginning")
    return true
end

local function ShiftVFX_OnEnd(fn, obj, locals, result)
    print("Shift Ending: ")
    log_info("LUA: Shift ending")
    return true
end

--hook_function("BlueprintGeneratedClass /Game/VFX/Power/Shift_New/SEQ_ShiftVFX.SEQ_ShiftVFX_DirectorBP_C", "ShiftVFX_OnStart", false, ShiftVFX_OnStart, nil, true)
--hook_function("BlueprintGeneratedClass /Game/VFX/Power/Shift_New/SEQ_ShiftVFX.SEQ_ShiftVFX_DirectorBP_C", "ShiftVFX_OnEnd", false, ShiftVFX_OnEnd, nil, true)
--hook_function("BlueprintGeneratedClass /Game/Reality/BP_ShiftManager.BP_ShiftManager_C", "OnShiftBegin", false, HookedFunctionPre, nil, true)


------------------------------------------------------------------------------------
-- Add code here
------------------------------------------------------------------------------------


--void post(fn: UFunction*, obj: UObject*, locals: StructObject*, result: void*)
--void SetCinematicMode(bool bInCinematicMode, bool bHidePlayer, bool bAffectsHUD, bool bAffectsMovement, bool bAffectsTurning);

local function OnShiftBegin(fn, obj, locals, result)
    print("Shift beginning : ")
    
    return true
end

local function OnShiftEnd(fn, obj, locals, result)
    print("Shift ended: ")
    
end

local is_shifting = false
local shift_start_count = 0
local shift_release_count = 200

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
    
    if is_shifting == false then
        local ShiftManager = GetFirstInstance("Class /Script/Chronos.ShiftManager")
        if (ShiftManager ~= nil) then
            if ShiftManager:CanShift(false, false) == true then
                if((state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) > 0) then
                    is_shifting = true
                    uevr.params.vr.set_mod_value("VR_ExtremeCompatibilityMode", "true")
                    shift_start_count = 0
                end
            end
        end
    -- we are shifting, a timer is running so when it gets to 120, leave extreme compatibility
    else 
        if((state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) == 0) then
            shift_start_count = shift_start_count + 1
            if (shift_start_count >= shift_release_count) then
                is_shifting = false
                uevr.params.vr.set_mod_value("VR_ExtremeCompatibilityMode", "false")
            end
        end
    end
    
end)

--hook_function("BlueprintGeneratedClass /Game/Reality/BP_ShiftManager.BP_ShiftManager_C", "OnShiftBegin", false, OnShiftBegin, nil, true)
--hook_function("BlueprintGeneratedClass /Game/Reality/BP_ShiftManager.BP_ShiftManager_C", "OnFinished_Event", false,  OnShiftEnd, nil, true)



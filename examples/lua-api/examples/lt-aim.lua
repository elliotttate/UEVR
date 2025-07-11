------------------------------------------------------------------------------------
-- Helper section
------------------------------------------------------------------------------------

local api = uevr.api
local vr = uevr.params.vr

function set_cvar_int(cvar, value)
    local console_manager = api:get_console_manager()
    
    local var = console_manager:find_variable(cvar)
    if(var ~= nil) then
        var:set_int(value)
    end
end

-------------------------------------------------------------------------------
-- xinput helpers
-------------------------------------------------------------------------------
function is_button_pressed(state, button)
    return state.Gamepad.wButtons & button ~= 0
end
function press_button(state, button)
    state.Gamepad.wButtons = state.Gamepad.wButtons | button
end
function clear_button(state, button)
    state.Gamepad.wButtons = state.Gamepad.wButtons & ~(button)
end

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
		print(i, instance:get_fname():to_string())
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

local find_static_class = function(name)
    local c = uevr.api:find_uobject(name)
    if(c ~= nil) then
        return c:get_class_default_object()
    else
        return nil
    end
end

-------------------------------------------------------------------------------
-- Finds an object without relying on api:find_uobject.
-------------------------------------------------------------------------------
local function find_required_object(obj_name)
    local arr = UEVR_FUObjectArray.get()
    if arr == nil then
        print ("cannot get object array")
        return nil
    end
    
    local items = arr:get_object_count()
    for i = 0, items do
        local obj_i = arr:get_object(i)
        
        if(obj_i ~= nil) then
            if obj_i:get_full_name() == obj_name then
                return obj_i
            end
        end
    end    
    
    return nil
    
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
    
    return nil
end


-------------------------------------------------------------------------------
-- Example hook pre function. Post is same but no return.
-------------------------------------------------------------------------------

-- Note if post, do not return a value. 
-- If hooking as native, must return false.
local function HookedFunctionPre(fn, obj, locals, result)
    print("Shift beginning : ")
    
    return true
end

local function PromptVisible(fn, obj, locals, result)
    print("SetPromptVisible", obj:get_full_name(), locals.bIsVisible)
end

--hook_function("Class /Script/MercuryUI.MUMGMapMarker", "SetPromptVisible", false, nil, PromptVisible, true)

-- Class /Script/MercuryUI.MUMGMapMarker


-- Class /Script/MercuryUI.MUMGBelfryMapMarker
-- BelfryMapMarker_UMG_C /Game/UI/UMG/Map/BelfryMapMarker_UMG.WidgetArchetype

-- MUMGBelfryMapMarker /Script/MercuryUI.Default_MUMGBelfryMapMarker <-- exists not in map and only one 

------------------------------------------------------------------------------------
-- Add code here
------------------------------------------------------------------------------------
local Timer = 0
local FailTimer = 0
local MUMGMap_C = api:find_uobject("Class /Script/MercuryUI.MUMGMapMarker")
local CheckInMap = false
local InMap = false

uevr.sdk.callbacks.on_lua_event(function(event_name, event_string)
    local pawn = get_local_pawn()

    if CheckInMap == false and InMap == false then
        if event_name == "LeftTriggerDown" then
            if pawn ~= nil then
                --vr.set_aim_method(1)
                vr.set_mod_value("VR_AimMethod", "1")
                vr.set_mod_value("VR_AimUsePawnControlRotation", "true")
                FailTimer = 20.0
                --pawn.CameraComponent.bUsePawnControlRotation = true
            end
        end
        
        if event_name == "LeftTriggerUp" then
            if pawn ~= nil then
                vr.set_aim_method(0)
                vr.set_mod_value("VR_AimMethod", "0")
                vr.set_mod_value("VR_AimUsePawnControlRotation", "false")
                if pawn.CameraComponent ~= nil then
                    pawn.CameraComponent.bUsePawnControlRotation = false
                    Timer = 1.0
                    FailTimer = 0
                end
                --vr.recenter_view()
            end
        end
    end
        local mumgmap_instances = MUMGMap_C:get_objects_matching(false)
    local mumgmap_count = #mumgmap_instances
    
    print("mumgmap_count", mumgmap_count)
    if mumgmap_count > 5 then 
        InMap = true
        vr.set_mod_value("VR_2DScreenMode","true")
    end

    if event_name == "BackReleased" then
        CheckInMap = true
    end
    
    if event_name == "BPressed" then
        if InMap == true then
            InMap = false
            CheckInMap = false
            vr.set_mod_value("VR_2DScreenMode","false")
        end
    end
            
    
end)

uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
end)

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    if(Timer < 0) then 
        print("Recentering")
        vr.recenter_view() 
        Timer = 0
    end
    if(Timer > 0) then Timer = Timer - delta end
    
    -- Check if we might be in menu by enumerating the menu objects.
    if CheckInMap == true and InMap == false then
        local mumgmap_instances = MUMGMap_C:get_objects_matching(false)
        local mumgmap_count = #mumgmap_instances
        
        print("mumgmap_count", mumgmap_count)
        if mumgmap_count > 5 then 
            InMap = true
            vr.set_mod_value("VR_2DScreenMode","true")
        end
    end
    
    -- if we are stuck in aiming mode for more than FailTimer, force reset it in 0.25 seconds.
    if(FailTimer < 0) then
        local pawn = get_local_pawn()
        if pawn ~= nil then
            vr.set_aim_method(0)
            vr.set_mod_value("VR_AimMethod", "0")
            vr.set_mod_value("VR_AimUsePawnControlRotation", "false")
            if pawn.CameraComponent ~= nil then
                pawn.CameraComponent.bUsePawnControlRotation = false
                Timer = 1.0
                FailTimer = 0
            end
            --vr.recenter_view()
        end
        
    end
    
    if(FailTimer > 0) then FailTimer = FailTimer - delta end
    
    
end)

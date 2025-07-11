------------------------------------------------------------------------------------
-- Helper section
------------------------------------------------------------------------------------

local api = uevr.api
local vr = uevr.params.vr

local DefaultRenderMethod = vr:get_mod_value("VR_RenderingMethod")
local media_player_class = nil
local shrine_class = nil
local end_user_widget_c = api:find_uobject("Class /Script/EndGame.EndUserWidget")
local MEDIA_PLAYER_CLASS = "Class /Script/MediaAssets.MediaPlayer"
local VR_RENDERING_METHOD = "VR_RenderingMethod"
local VR_SYNCED_SEQUENTIAL_METHOD = "VR_SyncedSequentialMethod"
local VR_2D_SCREEN_MODE = "VR_2DScreenMode"

local is_in_2d_mode = false
local media_players = nil -- Cache the media player instances

-- Make sure invert alpha is set for the UI transparency
vr.set_mod_value("UI_InvertAlpha", "true")

function set_cvar_int(cvar, value)
    local console_manager = api:get_console_manager()
    
    local var = console_manager:find_variable(cvar)
    if(var ~= nil) then
        var:set_int(value)
    end
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
		print(i, instance:get_fname():to_string(), mesh:get_full_name())
	end
end

-------------------------------------------------------------------------------
-- Get first instance of a given class object
-------------------------------------------------------------------------------
local function GetFirstInstance(class_to_search, default)
    default = default or false
	local obj_class = api:find_uobject(class_to_search)
    if obj_class == nil then 
		print(class_to_search, "was not found") 
		return nil
	end

    return obj_class:get_first_object_matching(default)
end

-------------------------------------------------------------------------------
-- Get class object instance matching string
-------------------------------------------------------------------------------
local function GetInstanceMatching(class_to_search, match_string)
	local obj_class = api:find_uobject(class_to_search)
    if obj_class == nil then 
		--print(class_to_search, "was not found") 
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

--hook_function("BlueprintGeneratedClass /Game/Reality/BP_ShiftManager.BP_ShiftManager_C", "OnShiftBegin", false, HookedFunctionPre, nil, true)



------------------------------------------------------------------------------------
-- Add code here
------------------------------------------------------------------------------------
local function is_in_summon_shrine_minigame()
    if end_user_widget_c == nil then
        end_user_widget_c = api:find_uobject("Class /Script/EndGame.EndUserWidget")
    end
    if end_user_widget_c == nil then
        return false
    end

    local obj_instances = end_user_widget_c:get_objects_matching(false)

    for i, instance in ipairs(obj_instances) do
        if instance ~= nil and type(instance.get_full_name) == "function" then
        
            --if string.find(instance:get_full_name(), "SummonShrine") then
            --    print("examining ", instance:get_full_name())
            --end
            
            if string.find(instance:get_full_name(), "SummonShrine_Crystal_C") and string.find(instance:get_full_name(), "EndGameInstance_") then
                if instance.Visibility ~= nil and instance.Visibility == 0 then
                    return true
                end
            end
		end
	end

    return false
    --local Shrine = GetFirstInstance("WidgetBlueprintGeneratedClass /Game/Menu/MiniGame/SummonShrine/SummonShrine_Crystal.SummonShrine_Crystal_C", false)
end

local function is_in_menu()
    if GetFirstInstance("WidgetBlueprintGeneratedClass /Game/Menu/Resident/MainMenu/TopList_Cell.TopList_Cell_C", false) ~= nil then
        return true
    else
        return false
    end
end

local menu_counter = 100
-------------------------------------------------------------------------------
-- Get class object instance matching string
-------------------------------------------------------------------------------
local once = true
uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    local is_in_movie = false
    local is_in_shrine = false 
    local should_render_2d_mode = false
        
    if media_player_class == nil then
        media_player_class = api:find_uobject(MEDIA_PLAYER_CLASS)
    end

    if is_in_summon_shrine_minigame() then
        is_in_shrine = true
    end
    
    -- this tanks performance of the game and is not found at first. Might need to move this to c++ where it can be threaded and throttled
    --[[
    if shrine_class == nil then
        shrine_class = api:find_uobject("WidgetBlueprintGeneratedClass /Game/Menu/MiniGame/SummonShrine/SummonShrine_Crystal.SummonShrine_Crystal_C")
        if shrine_class == nil then
            print("shrine not found")
        end
    end
    ]]
    local pawn = get_local_pawn()
    if pawn ~= nil and pawn:IsPlayerControlled() == false  then
        if media_player_class then
            local obj_instances = media_player_class:get_objects_matching(false)
            if obj_instances then
                for _, media_player in ipairs(obj_instances) do
                    if media_player and not string.find(media_player:get_full_name(), "Menu") and not string.find(media_player:get_full_name(), "Chadley") and media_player:IsPlaying() and media_player:CanPause() then
                        is_in_movie = true
                        print(media_player:get_full_name())
                        log_info("In Movie: " .. tostring(media_player:get_full_name()))
                        break -- Found a playing media player, no need to check others
                    end
                end
            end
        end
    end
    --print(pawn:get_full_name())
    
    should_render_2d_mode = (is_in_movie == true or is_in_shrine == true)
    
    if should_render_2d_mode ~= is_in_2d_mode then
        is_in_2d_mode = should_render_2d_mode
        if should_render_2d_mode then
            print("**switching to 2d mode. Cinenatic: ", is_in_movie, " shrine: ", is_in_shrine)
            vr.set_mod_value(VR_RENDERING_METHOD, "1")
            vr.set_mod_value(VR_2D_SCREEN_MODE, "true")
        else
            vr.set_mod_value(VR_RENDERING_METHOD, DefaultRenderMethod)
            vr.set_mod_value(VR_2D_SCREEN_MODE, "false")
        end
    end
end)

uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
end)


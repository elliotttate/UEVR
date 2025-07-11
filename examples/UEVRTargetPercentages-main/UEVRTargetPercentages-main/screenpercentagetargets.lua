local api = uevr.api
local vr = uevr.params.vr

local config_filename = "screenpercentagetargets.txt"
local config_data = nil
local config_changed = false
local base_screen_percentage = 50
local screen_percentages = {}

local function write_config()
	config_data = "base_screen_percentage=" .. tostring(base_screen_percentage) .. "\n"
    
    for key, value in pairs(screen_percentages) do
        config_data = config_data .. "screen_percentage_" .. key .. "=" .. tostring(value) .. "\n"        
    end
                  
    fs.write(config_filename, config_data)
end

local function read_config()
    print("reading config")
    config_data = fs.read(config_filename)
    if config_data then -- Check if file was read successfully
        print("config read")
        for key, value in config_data:gmatch("([^=]+)=([^\n]+)\n?") do
            print("parsing key:", key, "value:", value)            
            if key == "base_screen_percentage" then
                base_screen_percentage = tonumber(value) or 0            
            end

            if key:find("screen_percentage_",1,true)~=nil then
                screen_percentages[string.gsub(key, "^screen_percentage_","")] = value
            end             
        end
    else
        print("Error: Could not read config file.")
    end
end

function set_cvar_int(cvar, value)
    local console_manager = api:get_console_manager()
    
    local var = console_manager:find_variable(cvar)
    if(var ~= nil) then
        var:set_int(value)
    end
end

function get_command(cmd)
    local console_manager = api:get_console_manager()
    
    local var = console_manager:find_command(cmd)

    return tostring(var)
end

function set_cvar_float(cvar, value)
    local console_manager = api:get_console_manager()
    
    local var = console_manager:find_variable(cvar)
    if(var ~= nil) then
        print("setting float", value)
        var:set_float(value)
    else   
        print("cvar does not exist: ", cvar)
    end
end

read_config()

local function disable_object_hooks(state)
    if state ~= obj_hook_disabled then
        obj_hook_disabled = state
        UEVR_UObjectHook.set_disabled(state)
    end
end

if first_person == 0 then
	disable_object_hooks(true)
else
    disable_object_hooks(false)
end


uevr.sdk.callbacks.on_draw_ui(function()    
    imgui.text("Screen Percentage Target Mod Settings")
    imgui.text("Mod by hookman")
    imgui.text("")
    imgui.text("Set screen percentages for multiple view targets to optimise GPU usage throughout")
    imgui.text("(*) indicates the current view target")
    imgui.text("")
    local needs_save = false
    local changed, new_value

    local game_engine       = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)
    local player            = uevr.api:get_player_controller(0)    
    local view_target       = nil

    if player then       
        local currentVT = player:GetViewTarget()                
        if prevViewTarget ~= currentVT or config_changed == true then                        
            view_target = currentVT:get_full_name()
            view_target = string.match(view_target, "^[^%s]+")
        end
    end

    changed, new_value = imgui.slider_int("Base Screen Percentage", base_screen_percentage, 1, 100)
    if changed then
        needs_save = true
        base_screen_percentage = new_value -- Correctly use new_value                
    end     

    if view_target ~= nil then
        local sp = screen_percentages[view_target]
        if sp == nil then
            sp = base_screen_percentage        
        end 
        changed, new_value = imgui.slider_int("(*) " .. view_target, sp, 1, 100)            

        if changed then
            needs_save = true
            screen_percentages[view_target] = new_value
        end                       
    end

    for key, value in pairs(screen_percentages) do
        local name = key
        if name ~= view_target then            
            changed, new_value = imgui.slider_int(name, value, 1, 100)
            if changed then
                needs_save = true
                screen_percentages[key] = new_value -- Correctly use new_value                
            end  
        end
    end        

    if needs_save then
        config_changed = true
        write_config()
    end
end)

local prevViewTarget = nil
local game_engine_class = uevr.api:find_uobject("Class /Script/Engine.GameEngine")

local iterated = false

-- run this every engine tick, *after* the world has been updated
uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)   	    
    local game_engine       = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)
    local player            = uevr.api:get_player_controller(0)
    if player then               
        local currentVT = player:GetViewTarget()                
        if prevViewTarget ~= currentVT or config_changed == true then                        
            local view_target = currentVT:get_full_name()
            print(view_target)
            view_target = string.match(view_target, "^[^%s]+") --get first word
            local target_sp = tonumber(screen_percentages[view_target])
            if target_sp == nil then
                target_sp = base_screen_percentage
            end
            if target_sp > 0 then
                local console_manager = api:get_console_manager()
                uevr.api:execute_command("r.ScreenPercentage " .. tostring(target_sp))
            end          
            
            prevViewTarget = currentVT      
            config_changed = false                      
        end
    end    
end)


--#########################################
-- Preferences
--#########################################
local swapltrb = true
local right_stick_big_vert_deadzone = true

-- Experimental melee using right stick gestures:
-- combo is right to left, left to right, then up to down. Disables RT for swing melee. No power melee.
local melee_swing = false
-- Define the threshold for the melee swipe detection. Bigger is more sensitive
local swipe_threshold = 0.05

--#########################################
-- Dont edit below this line
--#########################################

local api = uevr.api
local vr = uevr.params.vr


-- Initialize variables
local combo_state = 0          -- 0 = no combo, 1 = right-to-left, 2 = left-to-right, 3 = up-to-down
local combo_timer = 0          -- Tracks time elapsed for the combo
local combo_threshold = 60     -- Frames allowed between combo inputs (adjust based on frame rate)
local last_position = nil      -- Tracks the last controller position
local frame_counter = 0        -- Simulated frame counter (incremented each callback)
local swipe_left = false
local is_in_menu = false
local back_down = false
local b_down = false

local function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        error("Cannot find " .. name)
        return nil
    end

    return obj
end

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    -- if we are at main menu, bail out.
    local game_instance_gf = find_required_object("Class /Script/GunfireRuntime.GameInstanceGunfire")
    local game_instance_gf_instance = UEVR_UObjectHook.get_first_object_by_class(game_instance_gf)
    local is_in_gameplay = game_instance_gf_instance:IsInGameplay()
    if is_in_gameplay == false then
        return
    end
    --print(string.format("is_in_gameplay=%d", is_in_gameplay and 1 or 0))

    local uihud = find_required_object("Class /Script/GunfireRuntime.UIHud")
    local uihud_instance = UEVR_UObjectHook.get_first_object_by_class(uihud)
    local is_in_menu = not uihud_instance:IsVisible()
    
--[[
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK > 0) then
        -- only react first time button is noticed down and not yet released.
        if (back_down == false) then
            back_down = true
            if (is_in_menu == false) then
                is_in_menu = true
            else
                is_in_menu = false
            end
        end
    else
        -- clear backbutton state on release.
        back_down = false
    end
    
    -- B can also exit menu.
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B > 0) then
        if (b_down == false) then
            b_down = true
            if (is_in_menu == true) then
                is_in_menu = false
            end
        end
    else
        b_down = false
    end
]]   
    if (is_in_menu == true) then
        return
    end
    
    -- add massive right stick deadzone
    if (right_stick_big_vert_deadzone) then 
        if (state.Gamepad.sThumbRY > -32000 and state.Gamepad.sThumbRY < 32000) then
            state.Gamepad.sThumbRY = 0
        end
    end 
    
    local LTDown = state.Gamepad.bLeftTrigger > 200
    
    if swapltrb then
        local RBDown = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) > 0
        
        -- Swap left trigger with right shoulder button
        state.Gamepad.bLeftTrigger = RBDown and 255 or 0
        state.Gamepad.wButtons = LTDown and (state.Gamepad.wButtons | XINPUT_GAMEPAD_RIGHT_SHOULDER) or (state.Gamepad.wButtons & ~XINPUT_GAMEPAD_RIGHT_SHOULDER)
        LTDown = state.Gamepad.bLeftTrigger > 200
    end


    -- Gesture detection and combo logic
    if (melee_swing) then
        if (LTDown == false) then
            state.Gamepad.bRightTrigger = 0
            
            local right_controller_index = vr.get_right_controller_index()
            if right_controller_index ~= -1 then
                -- Get the current position of the right controller
                local current_position = UEVR_Vector3f.new()
                local right_controller_rotation = UEVR_Quaternionf.new()
                vr.get_pose(right_controller_index, current_position, right_controller_rotation)

                -- Check gestures
                if last_position then
                    local delta_x = current_position.x - last_position.x
                    local delta_y = current_position.y - last_position.y * 0.6 -- lower threshld for vertical
                    -- Determine gesture based on combo state
                    if combo_state == 0 then
                        -- Right-to-Left gesture to start combo
                        if delta_x * -1 >= swipe_threshold then
                            combo_state = 1
                            combo_timer = frame_counter
                            state.Gamepad.bRightTrigger = 255
                        end
                    elseif combo_state == 1 then
                        -- Left-to-Right gesture for second combo hit
                        if frame_counter - combo_timer <= combo_threshold and delta_x >= swipe_threshold then
                            combo_state = 2
                            combo_timer = frame_counter
                            state.Gamepad.bRightTrigger = 255
                        elseif frame_counter - combo_timer > combo_threshold then
                            -- Reset combo if too much time has passed
                            combo_state = 0
                        end
                    elseif combo_state == 2 then
                        -- Up-to-Down gesture for third combo hit
                        if frame_counter - combo_timer <= combo_threshold and delta_y * -1 >= swipe_threshold then
                            combo_state = 0
                            combo_timer = frame_counter
                            state.Gamepad.bRightTrigger = 255
                        elseif frame_counter - combo_timer > combo_threshold then
                            -- Reset combo if too much time has passed
                            combo_state = 0
                        end
                    end
                end

                -- Update last position
                last_position = current_position
            end

            -- Reset combo if LT is held down
            if LTDown then
                combo_state = 0
            end

            -- Increment frame counter (simulating a frame-by-frame environment)
            frame_counter = frame_counter + 1
        else
            combo_state = 0
            frame_counter = 0
        end
    end
end)

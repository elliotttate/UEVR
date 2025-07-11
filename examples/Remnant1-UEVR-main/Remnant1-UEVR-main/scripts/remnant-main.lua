local api = uevr.api
local vr = uevr.params.vr

-- Only use this for one time allocated objects (classes, structs), not things like actors
local function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        error("Cannot find " .. name)
        return nil
    end

    return obj
end

local find_static_class = function(name)
    local c = find_required_object(name)
    return c:get_class_default_object()
end

local kismet_string_library = find_static_class("Class /Script/Engine.KismetStringLibrary")
local kismet_math_library = find_static_class("Class /Script/Engine.KismetMathLibrary")
local kismet_system_library = find_static_class("Class /Script/Engine.KismetSystemLibrary")
local Statics = find_static_class("Class /Script/Engine.GameplayStatics")
local hitresult_c = find_required_object("ScriptStruct /Script/Engine.HitResult")
local empty_hitresult = StructObject.new(hitresult_c)

local game_engine_class = find_required_object("Class /Script/Engine.GameEngine")
local actor_c = find_required_object("Class /Script/Engine.Actor")
local motion_controller_component_c = find_required_object("Class /Script/HeadMountedDisplay.MotionControllerComponent")
local scene_component_c = find_required_object("Class /Script/Engine.SceneComponent")

local is_vignette_enabled = true

local hmd_actor = nil -- The purpose of the HMD actor is to accurately track the HMD's world transform
local left_hand_actor = nil
local right_hand_actor = nil
local left_hand_component = nil
local right_hand_component = nil
local hmd_component = nil
local last_level = nil

local ftransform_c = find_required_object("ScriptStruct /Script/CoreUObject.Transform")
local temp_transform = StructObject.new(ftransform_c)
local temp_vec3f = Vector3f.new(0, 0, 0)

local function spawn_actor(world_context, actor_class, location, collision_method, owner)
    temp_transform.Translation = location
    temp_transform.Rotation.W = 1.0
    temp_transform.Scale3D = Vector3f.new(1.0, 1.0, 1.0)

    local actor = Statics:BeginDeferredActorSpawnFromClass(world_context, actor_class, temp_transform, collision_method, owner)

    if actor == nil then
        print("Failed to spawn actor")
        return nil
    end

    Statics:FinishSpawningActor(actor, temp_transform)
    print("Spawned actor")

    return actor
end



local function reset_hand_actors()
    -- We are using pcall on this because for some reason the actors are not always valid
    -- even if exists returns true
    if left_hand_actor ~= nil and UEVR_UObjectHook.exists(left_hand_actor) then
        pcall(function()
            if left_hand_actor.K2_DestroyActor ~= nil then
                left_hand_actor:K2_DestroyActor()
            end
        end)
    end

    if right_hand_actor ~= nil and UEVR_UObjectHook.exists(right_hand_actor) then
        pcall(function()
            if right_hand_actor.K2_DestroyActor ~= nil then
                right_hand_actor:K2_DestroyActor()
            end
        end)
    end

    if hmd_actor ~= nil and UEVR_UObjectHook.exists(hmd_actor) then
        pcall(function()
            if hmd_actor.K2_DestroyActor ~= nil then
                hmd_actor:K2_DestroyActor()
            end
        end)
    end

    left_hand_actor = nil
    right_hand_actor = nil
    hmd_actor = nil
end

local function spawn_hand_actors()
    local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

    local viewport = game_engine.GameViewport
    if viewport == nil then
        print("Viewport is nil")
        return
    end

    local world = viewport.World
    if world == nil then
        print("World is nil")
        return
    end

    reset_hand_actors()

    local pawn = api:get_local_pawn(0)

    if pawn == nil then
        --print("Pawn is nil")
        return
    end

    local pos = pawn:K2_GetActorLocation()

    left_hand_actor = spawn_actor(world, actor_c, pos, 1, nil)

    if left_hand_actor == nil then
        print("Failed to spawn left hand actor")
        return
    end

    right_hand_actor = spawn_actor(world, actor_c, pos, 1, nil)

    if right_hand_actor == nil then
        print("Failed to spawn right hand actor")
        return
    end

    hmd_actor = spawn_actor(world, actor_c, pos, 1, nil)

    if hmd_actor == nil then
        print("Failed to spawn hmd actor")
        return
    end

    print("Spawned hand actors")

    -- Add scene components to the hand actors
    left_hand_component = api:add_component_by_class(left_hand_actor, motion_controller_component_c)
    right_hand_component = api:add_component_by_class(right_hand_actor, motion_controller_component_c)
    hmd_component = api:add_component_by_class(hmd_actor, scene_component_c)

    if left_hand_component == nil then
        print("Failed to add left hand scene component")
        return
    end

    if right_hand_component == nil then
        print("Failed to add right hand scene component")
        return
    end

    if hmd_component == nil then
        print("Failed to add hmd scene component")
        return
    end

    left_hand_component.MotionSource = kismet_string_library:Conv_StringToName("Left")
    right_hand_component.MotionSource = kismet_string_library:Conv_StringToName("Right")

    -- Not all engine versions have the Hand property
    if left_hand_component.Hand ~= nil then
        left_hand_component.Hand = 0
        right_hand_component.Hand = 1
    end

    print("Added scene components")

    -- The HMD is the only one we need to add manually as UObjectHook doesn't support motion controller components as the HMD
    local hmdstate = UEVR_UObjectHook.get_or_add_motion_controller_state(hmd_component)

    if hmdstate then
        hmdstate:set_hand(2) -- HMD
        hmdstate:set_permanent(true)
    end

    print(string.format("%x", left_hand_actor:get_address()) .. " " .. string.format("%x", right_hand_actor:get_address()) .. " " .. string.format("%x", hmd_actor:get_address()))
end

local function reset_hand_actors_if_deleted()
    if left_hand_actor ~= nil and not UEVR_UObjectHook.exists(left_hand_actor) then
        left_hand_actor = nil
        left_hand_component = nil
    end

    if right_hand_actor ~= nil and not UEVR_UObjectHook.exists(right_hand_actor) then
        right_hand_actor = nil
        right_hand_component = nil
    end

    if hmd_actor ~= nil and not UEVR_UObjectHook.exists(hmd_actor) then
        hmd_actor = nil
        hmd_component = nil
    end
end

local function on_level_changed(new_level)
    -- All actors can be assumed to be deleted when the level changes
    print("Level changed")
    if new_level then
        print("New level: " .. new_level:get_full_name())
    end
    left_hand_actor = nil
    right_hand_actor = nil
    left_hand_component = nil
    right_hand_component = nil
    is_vignette_enabled = true
end

--api:execute_command("Camera.AimSnapping 0") --Disable aim assist

local is_using_two_handed_weapon = false


uevr.sdk.callbacks.on_pre_engine_tick(function(engine_voidptr, delta)
    local engine = game_engine_class:get_first_object_matching(false)
    if not engine then
        return
    end

    local pawn = api:get_local_pawn(0)    
    local viewport = engine.GameViewport

    if viewport then
        local world = viewport.World

        if world then
            local level = world.PersistentLevel

            if last_level ~= level then
                on_level_changed(level)
            end

            last_level = level
        end
    end

    

    reset_hand_actors_if_deleted()

    if left_hand_actor == nil or right_hand_actor == nil then
        spawn_hand_actors()
    end
end)

local player_hud = nil
local hudwidget = nil   

uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
    -- if we are at main menu, bail out.
    local game_instance_gf = find_required_object("Class /Script/GunfireRuntime.GameInstanceGunfire")
    local game_instance_gf_instance = UEVR_UObjectHook.get_first_object_by_class(game_instance_gf)
    local is_in_gameplay = game_instance_gf_instance:IsInGameplay()
    if is_in_gameplay == false then
        return
    end
	
    -- Kill object hooks in cinematics
	local player_instance = api:get_player_controller(0)
	if player_instance ~= nil then
		local is_in_cinematic = player_instance:IsInCinematic()
		if is_in_cinematic == true then
			UEVR_UObjectHook.set_disabled(true)
		elseif UEVR_UObjectHook.is_disabled() == true then
			UEVR_UObjectHook.set_disabled(false)
		end
	end

 
	local pawn = api:get_local_pawn(0)
	local player_controller = api:get_player_controller(0)
	if pawn ~= nil and player_controller ~= nil then
        
        --Get HUD info
        local player_hud = player_controller:GetHud()
        local hudwidget = player_hud.HudWidget        
        
        if (hudwidget) then
            local vignette = hudwidget.Vignette
            if is_vignette_enabled and vignette:IsVisible() then
                vignette:SetVisibility(1) --Disable Vignette
                is_vignette_enabled = false
            end
        end
        
        --Attach weapons
        local equipped_Weapon = pawn:GetCurrentRangedWeapon()

        if equipped_Weapon then
            local IsAiming = equipped_Weapon:IsAiming()
            -- print(equipped_Weapon)
            local gun_attach = pawn.Gun_Attach
            local gun_attach_hook = UEVR_UObjectHook.get_or_add_motion_controller_state(gun_attach)
            
            if gun_attach_hook then
                gun_attach_hook:set_hand(1)
                gun_attach_hook:set_permanent(true)
                if IsAiming then
                    equipped_Weapon:SetInHand(true)
                else
                    equipped_Weapon:SetInHand(false)
                end
                --local ammo_type = equipped_Weapon.AmmoPool
                --local ammo_name = tostring(ammo_type)
                --if string.find(ammo_name,"LongGun") then
                    --is_using_two_handed_weapon = true
                --else
                gun_attach_hook:set_rotation_offset(temp_vec3f:set(0, -1.571, 0)) --Rotate 90 degrees (in radians)
                is_using_two_handed_weapon = false
            end

        else
            print("Failed to find gun_attach")
        end
    end

end)

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

    if (is_in_menu == true) then
        return
    end

    local pawn = api:get_local_pawn(0)

    --Only enable pawn yaw rotation when the right stick is in use to resolve roll direction issue
    if pawn ~= nil then
        pawn.bUseControllerRotationPitch = false
        pawn.bUseControllerRotationRoll = true
        pawn.bUseControllerRotationYaw = (state.Gamepad.sThumbRX > 5000 or state.Gamepad.sThumbRX < -5000)
    end
end)



-- Two Handed Weapons
--[[ uevr.sdk.callbacks.on_post_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
    local pawn = api:get_local_pawn(0)

    if pawn ~= nil and type(pawn.GetCurrentRangedWeapon) == "function"  then
        local equipped_Weapon = pawn:GetCurrentRangedWeapon()

        if equipped_Weapon and is_using_two_handed_weapon and equipped_Weapon:IsAiming() then
            local left_hand_pos = left_hand_component:K2_GetComponentLocation()
            local right_hand_pos = right_hand_component:K2_GetComponentLocation()
            local dir_to_left_hand = (left_hand_pos - right_hand_pos):normalized()
            local right_hand_rotation = right_hand_component:K2_GetComponentRotation()
            local gun_attach = pawn.Gun_Attach
            local weapon_up_vector = gun_attach:GetUpVector()
            
            -- Calculate the new direction rotation
            local new_direction_rot = kismet_math_library:MakeRotFromXZ(dir_to_left_hand, weapon_up_vector)
            
            -- InverseRotator function
            local function InverseRotator(rotator)
                rotator.Pitch = rotator.Pitch * -1
                rotator.Yaw = rotator.Yaw * -1
                rotator.Roll = rotator.Roll * -1
                return rotator
            end
            local inverted_right_hand_rotation = InverseRotator(right_hand_rotation)
            
            -- Calculate the delta rotation
            local delta_rotation = kismet_math_library:ComposeRotators(new_direction_rot, inverted_right_hand_rotation)
            
                -- Function to apply a 180-degree yaw rotation (in degrees)
            local function RotateYaw(rotation, yaw_degrees)
                rotation.Yaw = rotation.Yaw + yaw_degrees
                return rotation
            end

            -- Add 180 degrees to the yaw (PI radians)
            delta_rotation = RotateYaw(delta_rotation, 180)

            -- Rotate the delta vector using the custom RotateVector function
            local function RotateVector(vector, rotator)
                -- Convert angles to radians
                local pitch_rad = math.rad(rotator.Pitch)
                local yaw_rad = math.rad(rotator.Yaw)
                local roll_rad = math.rad(rotator.Roll)
        
                -- Axis rotation matrices
                local cos_pitch, sin_pitch = math.cos(pitch_rad), math.sin(pitch_rad)
                local cos_yaw, sin_yaw = math.cos(yaw_rad), math.sin(yaw_rad)
                local cos_roll, sin_roll = math.cos(roll_rad), math.sin(roll_rad)
        
                local yaw_matrix = {
                    {cos_yaw, -sin_yaw, 0},
                    {sin_yaw,  cos_yaw, 0},
                    {0,         0,      1}
                }
                local pitch_matrix = {
                    {cos_pitch,  0, sin_pitch},
                    {0,          1, 0        },
                    {-sin_pitch, 0, cos_pitch}
                }
                local roll_matrix = {
                    {1, 0,         0        },
                    {0, cos_roll, -sin_roll},
                    {0, sin_roll,  cos_roll}
                }
        
                local function matrix_mult(A, B)
                    local result = {}
                    for i = 1, 3 do
                        result[i] = {}
                        for j = 1, 3 do
                            result[i][j] = A[i][1] * B[1][j] + A[i][2] * B[2][j] + A[i][3] * B[3][j]
                        end
                    end
                    return result
                end
        
                local rotation_matrix = matrix_mult(matrix_mult(yaw_matrix, pitch_matrix), roll_matrix)
                local rotated_vector = {
                    x = rotation_matrix[1][1] * vector.x + rotation_matrix[1][2] * vector.y + rotation_matrix[1][3] * vector.z,
                    y = rotation_matrix[2][1] * vector.x + rotation_matrix[2][2] * vector.y + rotation_matrix[2][3] * vector.z,
                    z = rotation_matrix[3][1] * vector.x + rotation_matrix[3][2] * vector.y + rotation_matrix[3][3] * vector.z
                }
        
                return rotated_vector
            end
        
            local original_grip_position = right_hand_pos
            local delta_to_grip = original_grip_position - gun_attach:K2_GetComponentLocation()
            local delta_rotated_vector = RotateVector(delta_to_grip, delta_rotation)
            
            -- Apply the rotation and translation
            local current_rotation = gun_attach:K2_GetComponentRotation()
            local new_rotation = kismet_math_library:ComposeRotators(delta_rotation, current_rotation)
            print("New Rotation: " ..tostring(new_rotation))
            local gun_attach = pawn.Gun_Attach
            gun_attach:K2_SetWorldRotation(new_rotation, false, empty_hitresult, false)
        
            
            -- Ensure original_grip_position is a valid vector
            if original_grip_position and original_grip_position.x and original_grip_position.y and original_grip_position.z then
            -- Ensure delta_rotated_vector is a valid vector
                if delta_rotated_vector and delta_rotated_vector.x and delta_rotated_vector.y and delta_rotated_vector.z then
                    -- Perform the vector subtraction
                    local new_weapon_position = Vector3f.new(original_grip_position.x - delta_rotated_vector.x, original_grip_position.y - delta_rotated_vector.y, original_grip_position.z - delta_rotated_vector.z)
                    
                    -- Apply the new weapon position
                    gun_attach:K2_SetWorldLocation(new_weapon_position, false, empty_hitresult, false)
                else
                    print("Error: delta_rotated_vector is not a valid vector.")
                end
            else
                print("Error: original_grip_position is not a valid vector.")
            end
        end
    end
end) ]]


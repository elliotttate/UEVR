local api = uevr.api
local vr = uevr.params.vr

local function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        print("Cannot find " .. name)
        return nil
    end

    return obj
end

local EquippedItem_c = find_required_object("Class /Script/Alabama.EquippedItem")
local temp_vec3f = Vector3f.new(0, 0, 0)

local function get_equipped_items(slot)
    local pawn = api:get_local_pawn()

    if pawn == nil then
        return
    end

    local items = EquippedItem_c:get_objects_matching(false)

    for _, item in ipairs(items) do
        if item.OwningAlabamaCharacter == pawn and not string.find(item:get_full_name(), "Unarmed") and not string.find(item:get_full_name(), "Sword") and not string.find(item:get_full_name(), "Hammer") and not string.find(item:get_full_name(), "Grimoire") then
            local ItemSlot = item.EquipSlot
            local Equip_Tag = ItemSlot.TagName:to_string()
            if string.find(Equip_Tag, slot) and item:IsUnsheathed() then
                local visual_components = item.SpawnedVisualComponents
                for _, visual_component in ipairs(visual_components) do
                    if string.find(visual_component:get_fname():to_string(), "Skeletal") or string.find(visual_component:get_fname():to_string(), "Static") then
                        return visual_component
                    end
                end
            end
        end
    end

    return nil
end

local previous_left_weapon = nil
local previous_right_weapon = nil

local left_attach = nil
local right_attach = nil

uevr.sdk.callbacks.on_pre_engine_tick(function(engine_voidptr, delta)
--uevr.sdk.callbacks.on_post_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)



end)

local should_render_mesh = true

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)
    local pawn = api:get_local_pawn()

    if pawn == nil then
        return
    end

        --Gun Hook WIP
    ----------------------------------------------------------------------
    local current_right_weapon = get_equipped_items("Right")
    local current_left_weapon = get_equipped_items("Left")
    local FPMesh = pawn.FirstPersonMesh
    local children = FPMesh.AttachChildren

    --UEVR can't handle multiname via uobject hook profiles so it must be scripted
    if current_left_weapon == nil and current_right_weapon == nil then
        should_render_mesh = true
    else
        should_render_mesh = false
    end
    
    for _, child in ipairs(children) do
        if string.find(child:get_full_name(), "Poseable") then
            child:SetVisibility(should_render_mesh, true) -- Hide upper body
        end
    end

    if current_left_weapon ~= previous_left_weapon then
        if previous_left_weapon then -- Remove old attachment if it existed
            UEVR_UObjectHook.remove_motion_controller_state(previous_left_weapon)
            left_attach = nil
        end

        if current_left_weapon then -- Attach new weapon if it exists
            left_attach = UEVR_UObjectHook.get_or_add_motion_controller_state(current_left_weapon)
            left_attach:set_hand(0)
            left_attach:set_permanent(true)
            if not string.find(current_left_weapon.AttachParent:get_full_name(), "Pistol") then
                left_attach:set_rotation_offset(temp_vec3f:set(1.000, 0, 0))
            end
        end

        previous_left_weapon = current_left_weapon -- Update previous weapon
    end

    -- Check for changes in right weapon (same logic as left)
    if current_right_weapon ~= previous_right_weapon then
        if previous_right_weapon then
            UEVR_UObjectHook.remove_motion_controller_state(previous_right_weapon)
            right_attach = nil
        end

        if current_right_weapon then
            right_attach = UEVR_UObjectHook.get_or_add_motion_controller_state(current_right_weapon)
            right_attach:set_hand(1)
            right_attach:set_permanent(true)
            if not string.find(current_right_weapon.AttachParent:get_full_name(), "Pistol") then
                right_attach:set_rotation_offset(temp_vec3f:set(1.000, 0, 0))
            end
        end

        previous_right_weapon = current_right_weapon
    end


    -- Change aim method based on fired weapon
    if UEVR_UObjectHook.exists(current_left_weapon) or UEVR_UObjectHook.exists(current_right_weapon) then
        if (state.Gamepad.bLeftTrigger > 200) then
            --print("Left")
            vr.set_mod_value("VR_AimMethod", "3")
        elseif (state.Gamepad.bRightTrigger > 200) then
            --print("Right")
            vr.set_mod_value("VR_AimMethod", "2")
        end
    end
end)


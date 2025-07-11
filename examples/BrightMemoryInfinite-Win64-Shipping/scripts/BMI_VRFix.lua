local api = uevr.api
local vr = uevr.params.vr
local params = uevr.params
local callbacks = params.sdk.callbacks

local cur_weap = nil
local is_cur_weap = nil
local is_cur_weap_mesh = nil
local is_weap1 = nil
local is_weap2 = nil
local is_weap3 = nil
local is_weap4 = nil
local is_weap9 = nil
local sniper_active = false
local is_cg = nil
local active_weap = nil
local weap_loc = nil
local hand = nil
local hand_animinstance = nil
local is_moving = nil
local Magmesh = nil
local swinging_fast = nil
local weapon_switching = nil

local melee_data = {
    right_hand_pos_raw = UEVR_Vector3f.new(),
    right_hand_q_raw = UEVR_Quaternionf.new(),
    right_hand_pos = Vector3f.new(0, 0, 0),
    last_right_hand_raw_pos = Vector3f.new(0, 0, 0),
    last_time_messed_with_attack_request = 0.0,
    first = true,
}

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    
	vr.get_pose(vr.get_right_controller_index(), melee_data.right_hand_pos_raw, melee_data.right_hand_q_raw)

    -- Copy without creating new userdata
    melee_data.right_hand_pos:set(melee_data.right_hand_pos_raw.x, melee_data.right_hand_pos_raw.y, melee_data.right_hand_pos_raw.z)

    if melee_data.first then
        melee_data.last_right_hand_raw_pos:set(melee_data.right_hand_pos.x, melee_data.right_hand_pos.y, melee_data.right_hand_pos.z)
        melee_data.first = false
    end

    local velocity = (melee_data.right_hand_pos - melee_data.last_right_hand_raw_pos) * (1 / delta)

    -- Clone without creating new userdata
    melee_data.last_right_hand_raw_pos.x = melee_data.right_hand_pos_raw.x
    melee_data.last_right_hand_raw_pos.y = melee_data.right_hand_pos_raw.y
    melee_data.last_right_hand_raw_pos.z = melee_data.right_hand_pos_raw.z
    melee_data.last_time_messed_with_attack_request = melee_data.last_time_messed_with_attack_request + delta
	
	local vel_len = velocity:length()
	
	if velocity.y < 0 then
		swinging_fast = vel_len >= 2.5
	end
	
    local pawn = api:get_local_pawn(0)
    if string.find(tostring(pawn:get_full_name()), "DefaultPawn") then
	    vr.set_mod_value("VR_AimMethod", "0")
		vr.set_mod_value("VR_RoomscaleMovement", "0")
    else
        if pawn ~= nil then		
		
			--FPMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "Player_Foot" then
						Fpmesh = mesh
						--print(tostring(Fpmesh:get_full_name()))
						
						break
					end
				end
			end	
			
			pawn["PressCrouch?"] = false
			pawn["VisCrossHair?"] = false

			hand = pawn.Hand
			hand_animinstance = hand.AnimScriptInstance
			is_moving = hand_animinstance.IsMoving
			
			Fpmesh:SetRenderInMainPass(is_moving)
			
			cur_weap = pawn.NowWeaponTagActor
			active_weap = pawn.Hand
			
			is_weap1 = pawn.Weapon1Name:to_string()
			is_weap2 = pawn.Weapon2Name:to_string()
			is_weap3 = pawn.Weapon3Name:to_string()
			is_weap4 = pawn.Weapon4Name:to_string()			
			is_weap9 = pawn.Weapon9Name:to_string()
			
			is_cg = pawn.IsCG
			
			if is_cg == false then
			   UEVR_UObjectHook.set_disabled(false)
			   vr.set_mod_value("VR_AimMethod", "2")
			   vr.set_mod_value("VR_RoomscaleMovement", "1")
			else
			   UEVR_UObjectHook.set_disabled(true)
			   vr.set_mod_value("VR_AimMethod", "0")
			   vr.set_mod_value("VR_RoomscaleMovement", "1")			   
			end

			for i, mesh in ipairs(cur_weap) do
				is_cur_weap = mesh:get_full_name()
				is_cur_weap_mesh = mesh.Mesh
			end
			
			local mag_mesh_c = api:find_uobject("Class /Script/Engine.SkinnedMeshComponent")
			if skeletal_mesh_c ~= nil then
				local mag_meshes = mag_mesh_c:get_objects_matching(false)
				for i, mesh in ipairs(mag_meshes) do
				--print(tostring(mesh:get_full_name()))
					if string.find(mesh:get_full_name(), ".PersistentLevel") and string.find(mesh:get_full_name(), "NODE_AddSkeletalMeshComponent") and string.find(mesh:get_full_name(), "-") then
						Magmesh = mesh
						if sniper_active == true then
							is_cur_weap_mesh.bAbsoluteLocation = true
							Magmesh.bAbsoluteLocation = true
							Magmesh:call("SetRenderInMainPass", false)
							pawn.Hand:call("SetRenderInMainPass", false)
							--print(tostring(Magmesh:get_full_name()))
						else
							is_cur_weap_mesh.bAbsoluteLocation = false
							Magmesh.bAbsoluteLocation = false
							Magmesh:call("SetRenderInMainPass", true)
							pawn.Hand:call("SetRenderInMainPass", true)
						end
						
						--break
					end
				end
			end
			
			if active_weap then
				weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(active_weap)

				if weap_loc and is_cur_weap then
					weap_loc:set_hand(1)
					weap_loc:set_permanent(true)
					weap_loc = UEVR_UObjectHook.remove_motion_controller_state(active_weap)	
					weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(active_weap)
					if string.find(is_cur_weap, "W_Meta") then
						weap_loc:set_location_offset(Vector3f.new(10.676, 52.498, -57.240))
						weap_loc:set_rotation_offset(Vector3f.new(0.020, -0.005, -.148))
					elseif string.find(is_cur_weap, "W_VEPR") then
						weap_loc:set_location_offset(Vector3f.new(7.065, 45.288, -26.738))
						weap_loc:set_rotation_offset(Vector3f.new(0.020, -0.005, -.148))
					elseif string.find(is_cur_weap, "W_GER") then
						weap_loc:set_location_offset(Vector3f.new(7.065, 45.288, -26.738))
						weap_loc:set_rotation_offset(Vector3f.new(0.00, 0.00, 0.00))
					elseif string.find(is_cur_weap, "W_IOPR") then
						weap_loc:set_location_offset(Vector3f.new(7.065, 36.008, -20.018))
						weap_loc:set_rotation_offset(Vector3f.new(0.00, -0.000, -.00))
					elseif string.find(is_cur_weap, "W_CaiDao") then
						weap_loc:set_location_offset(Vector3f.new(24.000, 43.000, -40.000))
						weap_loc:set_rotation_offset(Vector3f.new(0.020, -0.005, -.148))
					end
				end
			end	
		end
	end
end)
		
uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	if (state ~= nil) then
	
		vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
		vr.set_mod_value("VR_CameraRightOffset", "0.000000")
		vr.set_mod_value("VR_CameraUpOffset", "0.000000")

		if state.Gamepad.wButtons & 0x4000 ~= 0 and block_b == false then
            block_x = true
            state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_X)
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B
            --print("X to B Button")
        else
            block_x = false
        end

        if state.Gamepad.wButtons & 0x2000 ~= 0 and block_x == false then
            block_b = true
            state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_B)
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_X
            --print("B to X Button")
        else
            block_b = false
        end
		
		local pawn = api:get_local_pawn(0)
		
		if state.Gamepad.wButtons & 0x2000 ~= 0 then		
			if state.Gamepad.bLeftTrigger ~= 0 and string.find(is_cur_weap, "W_Meta") then
				if is_weap2 ~= "None" then
					pawn:call("Select2")
					weapon_switching = true	
				elseif is_weap3 ~= "None" then
					pawn:call("Select3")
					weapon_switching = true	
				elseif is_weap4 ~= "None" then
					pawn:call("Select4")
					weapon_switching = true	
				elseif is_weap5 ~= "None" then
					pawn:call("Select5")
					weapon_switching = true		
				end				
			elseif state.Gamepad.bLeftTrigger ~= 0 and string.find(is_cur_weap, "W_VEPR") then
				if is_weap3 ~= "None" then
					pawn:call("Select3")
					weapon_switching = true	
				elseif is_weap4 ~= "None" then
					pawn:call("Select4")
					weapon_switching = true	
				elseif is_weap5 ~= "None" then
					pawn:call("Select5")
					weapon_switching = true	
				elseif is_weap1 ~= "None" then
					pawn:call("Select1")
					weapon_switching = true		
				end		
			elseif state.Gamepad.bLeftTrigger ~= 0 and string.find(is_cur_weap, "W_GER") then
				if is_weap4 ~= "None" then
					pawn:call("Select4")
					weapon_switching = true	
				elseif is_weap9 ~= "None" then
					pawn:call("Select9")
					weapon_switching = true	
				elseif is_weap1 ~= "None" then
					pawn:call("Select1")
					weapon_switching = true	
				elseif is_weap2 ~= "None" then
					pawn:call("Select2")
					weapon_switching = true		
				end				
			elseif state.Gamepad.bLeftTrigger ~= 0 and string.find(is_cur_weap, "W_IOPR") then
				if is_weap9 ~= "None" then
					pawn:call("Select9")
					weapon_switching = true	
				elseif is_weap1 ~= "None" then
					pawn:call("Select1")
					weapon_switching = true	
				elseif is_weap2 ~= "None" then
					pawn:call("Select2")
					weapon_switching = true	
				elseif is_weap3 ~= "None" then
					pawn:call("Select3")
					weapon_switching = true		
				end			
			elseif state.Gamepad.bLeftTrigger ~= 0 and string.find(is_cur_weap, "W_CaiDao") then
				if is_weap1 ~= "None" then
					pawn:call("Select1")
					weapon_switching = true	
				elseif is_weap2 ~= "None" then
					pawn:call("Select2")
					weapon_switching = true	
				elseif is_weap3 ~= "None" then
					pawn:call("Select3")
					weapon_switching = true	
				elseif is_weap4 ~= "None" then
					pawn:call("Select4")
					weapon_switching = true		
				end						
			end		
		else	
            if state.Gamepad.bLeftTrigger == 0 then
			    weapon_switching = false			
			end
		
			if state.Gamepad.bLeftTrigger ~= 0 and weapon_switching == false and string.find(is_cur_weap, "W_IOPR") then
				if sniper_active == false then
					sniper_active = true
				end
			else
				sniper_active = false
			end				
		end
		
		if swinging_fast == true then
			if string.find(is_cur_weap, "W_CaiDao") then
				state.Gamepad.bRightTrigger = 200
			else
				state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_Y
			end 
		end		
	end
end)
--##########################
--# Remnant 2 Vr Fix - CJ117 #
--##########################



local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks
local hidearms = false
local is_cut = false
local Mactive = false
local Playing = false
local mDown = false
local mUp = false
local offset = {}
local adjusted_offset = {}
local base_pos = {0, 0, 0}
local mAttack = false
local mDownC = 0
local mUpC = 0
local mDB = false
local base_dif = 0
local JustCentered = false
local is_running = false
local is_scanning = false
local GetPawn = nil
local is_mouse = false
local is_hidden = false
local is_loading = false
local is_primary = false
local hide_once = false
local in_talk = false
local flash_light = false
local cur_state = nil
local in_travel = false
local is_reset = true
local orient_mode = nil
local is_aim = false
local cur_cam = nil
local melee_in_hand = false
local equipped_weapon = nil
local cur_state_m = nil
local is_interact = false
local top_ladder = false
local top_once = false
local on_ladder = false
local cur_ladder = nil
local next_ladder = nil
local cam_change = false
local cur_c_state = nil
local cur_c_state_ID = nil
local cur_c_state_name = nil
local is_melee_attached = false
local is_melee_equipped = false

local function First_P()
	local tpawn = api:get_local_pawn(0)
	if tpawn.RemnantStateCamera ~= nil then
		if tpawn.RemnantStateCamera.CurrentCamera.Distance ~= nil then
			tpawn.RemnantStateCamera.CurrentCamera.Distance = 0
			tpawn.RemnantStateCamera.CurrentCamera.FadeCharacterDistance = 0
			tpawn.RemnantStateCamera.CurrentCamera.LeftRightOffset = 0
		end
	end	
    UEVR_UObjectHook.set_disabled(false)
end

local function Third_P()
	local tpawn = api:get_local_pawn(0)
	if tpawn.RemnantStateCamera ~= nil then
		if tpawn.RemnantStateCamera.CurrentCamera.Distance ~= nil then
			tpawn.RemnantStateCamera.CurrentCamera.Distance = 180
			tpawn.RemnantStateCamera.CurrentCamera.FadeCharacterDistance = 50
			tpawn.RemnantStateCamera.CurrentCamera.LeftRightOffset = 55
		end	
	end	
    --UEVR_UObjectHook.set_disabled(true)
end

local function reset_height()
    local base = UEVR_Vector3f.new()
    params.vr.get_standing_origin(base)
	local hmd_index = params.vr.get_hmd_index()
    local hmd_pos = UEVR_Vector3f.new()
    local hmd_rot = UEVR_Quaternionf.new()
    params.vr.get_pose(hmd_index, hmd_pos, hmd_rot)
	base.x = hmd_pos.x
    base.y = hmd_pos.y
	base.z = hmd_pos.z
    params.vr.set_standing_origin(base)
	if hmd_pos.y >= 0.4 then
		InitLocY = 0.30
	else
		InitLocY = -0.10
	end
end

local function ResetPlayUI()
	params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
	params.vr.set_mod_value("VR_CameraUpOffset", "0.00")
	params.vr.set_mod_value("VR_CameraRightOffset", "0.00")
	params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
	params.vr.set_mod_value("UI_Distance", "4.500")
	params.vr.set_mod_value("UI_Size", "3.60")
	params.vr.set_mod_value("UI_X_Offset", "0.00")
	params.vr.set_mod_value("UI_Y_Offset", "0.00")
	
end

local function WeapHide()

	local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
    if skeletal_mesh_c ~= nil then
        local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

        
        for i, mesh in ipairs(skeletal_meshes) do
            if string.find(mesh:get_full_name(), "Weapon_") and string.find(mesh:get_full_name(), "PersistentLevel") then
				local is_vis = bVisible
                --print(tostring(mesh:get_full_name()))
				if is_vis == false then
					mesh:call("SetRenderInMainPass", false)
				end
                
                --break
			end
        end
    end
end

local function MelDet()

	local skeletal_mesh_c = api:find_uobject("Class /Script/Remnant.RemnantMeleeWeapon")
    if skeletal_mesh_c ~= nil then
        local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

        
        for i, mesh in ipairs(skeletal_meshes) do
            if string.find(mesh:get_full_name(), "Weapon_") and string.find(mesh:get_full_name(), "PersistentLevel") then
				is_melee_equipped = true

                break
            else
				break
			end
        end
    end
end

local function FlashHide()

	local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.StaticMeshComponent")
    if skeletal_mesh_c ~= nil then
        local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

        
        for i, mesh in ipairs(skeletal_meshes) do
            if mesh:get_fname():to_string() == "StaticMesh1" and string.find(mesh:get_full_name(), "Main.Main.PersistentLevel") and string.find(mesh:get_full_name(), "Flashlight_") then
				--print(tostring(mesh:get_full_name()))
				mesh:call("SetRenderInMainPass", false)
				mesh:call("SetVisibility", false)
				
				--print("I only happened once!")
                break
			end
        end
    end
end

local function MeleeHide()
	
	local mpawn = api:get_local_pawn(0)
	local mel_at = mpawn.Melee_Attach.AttachChildren
	local mel_active = tostring(mpawn.CachedStateMachine.CurrentState.AnimationID)
	if mel_at ~= nil then
		local mel_obj = mel_at[1]
		if string.find(mel_active, "Melee") then
			mel_obj:call("SetRenderInMainPass", true)
			melee_in_hand = true
			cam_change = true
		else
			cam_change = false
			melee_in_hand = false
			mel_obj:call("SetRenderInMainPass", false)
		end
	end
end

local function ShowArms()
	local apawn = api:get_local_pawn(0)
	if apawn ~= nil and not string.find(apawn:get_full_name(), "MainMenu") then
		apawn.Visual_Gloves:call("SetRenderInMainPass", true)
	end
end

local function HideArms()
	local apawn = api:get_local_pawn(0)
	if apawn ~= nil and not string.find(apawn:get_full_name(), "MainMenu") then
		apawn.Visual_Gloves:call("SetRenderInMainPass", false)
	end
end

local function ShowHead()
	local apawn = api:get_local_pawn(0)
	if apawn ~= nil and not string.find(apawn:get_full_name(), "MainMenu") then
		apawn.Visual_Head:call("SetRenderInMainPass", true)
	end
end

local function HideHead()
	local apawn = api:get_local_pawn(0)
	if apawn ~= nil and not string.find(apawn:get_full_name(), "MainMenu") then
		apawn.Visual_Head:call("SetRenderInMainPass", false)
		apawn.Visual_Helmet:call("SetRenderInMainPass", false)
	end
end

local function TopLadder()

	local skeletal_mesh_c = api:find_uobject("Class /Script/GunfireRuntime.CameraState")
    if skeletal_mesh_c ~= nil then
        local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

        
        for i, mesh in ipairs(skeletal_meshes) do
            if string.find(mesh:get_fname():to_string(), "Camera_Ladder_Top_C_") then
				cur_ladder = mesh:get_full_name()
				if cur_ladder == next_ladder then else
					top_ladder = true
					--print(tostring(mesh:get_full_name()))
					
					break
				end	
			end
        end
    end
end

params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
params.vr.set_aim_method(0)
reset_height()
ResetPlayUI()

local pawn = nil
local pcont = nil

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    
    pawn = api:get_local_pawn(0)
	pcont = api:get_player_controller(0)
	local GetPawn = nil
	
	--local test = pawn:ModifyReticule(ShowDefaultReticule)
	--print(test)
	
	if pawn ~= nil then
		GetPawn = pawn:get_full_name()
		is_mouse = pcont.bShowMouseCursor
		is_hidden = pcont.MyHUD.bHidden
		is_loading = pcont.Loading
		is_primary = pcont.bPrimaryWeaponToggled
		flash_light = pawn.FlashlightAttach
		is_reset = pcont.IsResetComplete
		
		--print(is_melee_attached)
		
		if pawn.Gun_Attach ~= nil then
			equipped_weapon = pawn:GetCurrentRangedWeapon()
		end	
		
		if pawn.Melee_Attach ~= nil then
			is_melee_attached = pawn.Melee_Attach.AttachChildren
			MelDet()
		end	
		
		if pawn.StateMachine ~= nil then
			cur_state_m = pawn.StateMachine.CurrentState:get_fname():to_string()
			cur_c_state_ID = tostring(pawn.StateMachine.CurrentState.AnimationID)
			cur_c_state_name = tostring(pcont.PlayState.CurrentState.StateName)
		end
		
		if pcont.PlayState ~= nil then
			if pcont.PlayState.CurrentState ~= nil then
				cur_state = pcont.PlayState.CurrentState.StateName
			end	
		end
		--[[
		if pawn.RemnantStateCamera ~= nil then
			orient_mode = pawn.RemnantStateCamera.CurrentCamera.OrientMode
			if orient_mode == 5 then
				--StowHide()
			end
		end	
		]]
		if pawn.RemnantStateCamera ~= nil then
			if pawn.RemnantStateCamera.CurrentCamera ~= nil then
				cur_cam = pawn.RemnantStateCamera.CurrentCamera:get_full_name()
			end	
		end	
		
		if pawn.CachedStateMachine ~= nil then
			cur_c_state = pawn.CachedStateMachine.CurrentState:get_full_name()
		end
		
		if pcont.MyHUD.ContextActor ~= nil and pcont.MyHUD.ContextActor.Dialogue ~= nil then
			in_talk = pcont.MyHUD.ContextActor.Dialogue.DialogActive
		else
			in_talk = false
		end
		
		if pcont.MyHUD.HudWidget ~= nil then
			is_cut = pcont.MyHUD.HudWidget.InCinematic
		else
			is_cut = false
		end

	end
	

	if tostring(cur_state) == "AliveWorldReset" then
		in_travel = true
	elseif tostring(cur_state) == "FinalizeWorldReset" then
		in_travel = true	
	elseif tostring(cur_state) == "Loading" then
		is_loading = true
	elseif tostring(cur_state) == "Playing" then
		is_loading = false
		in_travel = false
	end
	
	if cur_state_m ~= nil and string.find(cur_state_m, "InteractiveState") then
		is_interact = true
	else
		is_interact = false
	end
	
	if pawn == nil or string.find(pawn:get_full_name(), "MainMenu") or is_mouse == true or is_hidden == true or is_loading == true or is_cut == true or in_travel == true or is_reset == false or is_interact == true then 
		if Mactive == false then
			print("InCut")
			Mactive = true
			Playing = false
			mDB = false
			params.vr.set_mod_value("UI_Distance", "4.500")
			params.vr.set_mod_value("UI_Size", "3.60")
			params.vr.set_aim_method(0)
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
		end
		
		if is_cut == true then
			UEVR_UObjectHook.set_disabled(true)
			Third_P()
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
			params.vr.set_mod_value("VR_EnableGUI", "false")
		else
			if in_talk == true then
				params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
				params.vr.set_mod_value("VR_CameraForwardOffset", "20.00")
				UEVR_UObjectHook.set_disabled(true)
				--HideHead()
				--Third_P()
				--pawn.RemnantStateCamera:Activate(Enable)
			else
				params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
				params.vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "true")
			end	
			params.vr.set_mod_value("VR_EnableGUI", "true")
		end
		
		if is_loading == true then
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
			params.vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
		end
		
		if in_travel == true or is_interact == true then
			equipped_weapon:SetInHand(false)
			ShowArms()
		end
		
		--print(cur_c_state_ID)
		--print(cur_c_state_name)
		if cur_c_state_ID ~= nil and cur_c_state_name ~= nil then
			if string.find(cur_c_state_ID, "IdleMove") and string.find(cur_c_state_name, "Cinematic") then
				First_P()
				pawn.RemnantStateCamera:Deactivate(Call)
				params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
				params.vr.set_mod_value("VR_CameraForwardOffset", "20.00")
				UEVR_UObjectHook.set_disabled(true)
				--HideHead()
			end
		end	
	else
		if Playing == false then
			print("Playing")
			Mactive = false
			Playing = true 
			mDB = false
			params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
			params.vr.set_mod_value("UI_Distance", "10.00")
			params.vr.set_mod_value("UI_Size", "8.00")
			pawn.RemnantStateCamera:Deactivate(Call)
			First_P()
			FlashHide()
			params.vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
			params.vr.set_mod_value("VR_EnableGUI", "true")
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			UEVR_UObjectHook.set_disabled(false)
			params.vr.set_aim_method(2)
		end
		
		MeleeHide()
		
		--[[
		--Might not be needed
		
		if equipped_weapon ~= nil then
			equipped_weapon:SetInHand(true)
			
			local IsAiming = equipped_weapon:IsAiming()
			if IsAiming then
				is_aim = true
				equipped_weapon:SetInHand(true)
			else
				if is_aim == true then
					is_aim = false
					equipped_weapon:SetInHand(false)
					StowHide()
				end	
			end
			
		end
		]]
		
		if string.find(cur_cam, "Ladder") or string.find(cur_c_state, "Ladder") then
			equipped_weapon:SetInHand(false)
			on_ladder = true
			cam_change = true
			ShowArms()
			TopLadder()
		elseif string.find(cur_cam, "Vault") or string.find(cur_c_state, "Vault") then
			equipped_weapon:SetInHand(false)
			cam_change = true
			ShowArms()	
		elseif string.find(cur_cam, "Melee") or melee_in_hand == true then
			equipped_weapon:SetInHand(false)
			if cam_change == false then
				cam_change = true
				MeleeHide()
			end
		else
			top_once = false
			top_ladder = false
			on_ladder = false
			if cam_change == true then
				cam_change =  false
				next_ladder = cur_ladder
				top_once = false
				top_ladder = false
				on_ladder = false
				melee_in_hand = false
				HideArms()
				MeleeHide()
			end	
			
			equipped_weapon:SetInHand(true)
		end
		
		if Playing == true and is_melee_equipped == true then

				local right_controller_index = params.vr.get_right_controller_index()
				local right_controller_position = UEVR_Vector3f.new()
				local right_controller_rotation = UEVR_Quaternionf.new()
				params.vr.get_pose(right_controller_index, right_controller_position, right_controller_rotation)

				offset[1] = right_controller_position.y - base_pos[1]
				offset[2] = right_controller_position.z - base_pos[2]
				adjusted_offset[2] = offset[2] + base_dif
				if offset[1] <= -0.02 then
					mDown = true
				end
				if adjusted_offset[2] <= -0.0112 then
					mUp = true
				end
				if mDown == true and mUp == true and mDB == true then
					mDownC = 0
					mUpC = 0
					mDown = false
					mUp = false
					mAttack = true
				end
				base_pos[1] = right_controller_position.y
				base_pos[2] = right_controller_position.z
				base_dif = 0
				if offset[2] < 0 then
					base_dif = offset[2]
				end
				if mUp == true then
					mUpC = mUpC + 1
				end
				if mDown == true then
					mDownC = mDownC + 1
				end
				if mDownC > 10 or mUpC > 10 then
					mDownC = 0
					mUpC = 0
					mDown = false
					mUp = false
					mDB = true
				end
				
				if mAttack == true then
					mDB = false
				end
			end
			
			
	end

end)	

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	if (state ~= nil) then

		if Playing == false then
			if state.Gamepad.bLeftTrigger ~= 0 and state.Gamepad.bRightTrigger ~= 0 then
				if JustCentered == false then
					JustCentered = true
					reset_height()
					params.vr.recenter_view()
					state.Gamepad.bLeftTrigger = 0
					state.Gamepad.bRightTrigger = 0
				end
			else
				JustCentered = false
			end
		end
		
		if Playing == true and mAttack == true then
			mAttack = false
			MeleeHide()
			state.Gamepad.bRightTrigger = 250
		end
		
		
		if Playing == true then
			if state.Gamepad.sThumbRY >= 30000 then
				if is_running == false then
					is_running = true
					state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_LEFT_THUMB
				end
			else
				is_running = false
			end
		end
		
		if Playing == true then
			if state.Gamepad.sThumbRY <= -30000 then
				if is_scanning == false then
					is_scanning = true
					state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_RIGHT_THUMB
					
				end
			else
				is_scanning = false
			end
		end
		
		
	end

end)

callbacks.on_pre_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
	if on_ladder == true and top_ladder == true then
		rotation.Yaw = rotation.Yaw + 180
	end
end)
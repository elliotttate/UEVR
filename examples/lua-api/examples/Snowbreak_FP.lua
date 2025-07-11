
UEVR_UObjectHook.activate()

local api = uevr.api
local m_VR = uevr.params.vr

m_VR.set_mod_value("FrameworkConfig_AdvancedMode", "true")

local player_old = ""
local scene_old = ""

local weapon, girl_head, girl_body = nil, nil, nil

local grip_action = m_VR.get_action_handle("/actions/default/in/Grip")

local DPad_Up = m_VR.get_action_handle("/actions/default/in/DPad_Up")
local DPad_Down = m_VR.get_action_handle("/actions/default/in/DPad_Down")

local left_source = m_VR.get_left_joystick_source()
local right_source = m_VR.get_right_joystick_source()

local lplayer = nil
local pawn = nil

local scene_c = ""
local is_right_grip_active = nil
local player_c = nil

local rotator_c = api:find_uobject("ScriptStruct /Script/CoreUObject.Rotator")
local vector_c = api:find_uobject("ScriptStruct /Script/CoreUObject.Vector")
local fhitresult = api:find_uobject("ScriptStruct /Script/Engine.HitResult")

vr_cam_r = StructObject.new(rotator_c)
Fix_aim = StructObject.new(vector_c)
weapon_r = StructObject.new(rotator_c)
hit_result = StructObject.new(fhitresult)

local hmd_index = m_VR.get_hmd_index()
local hmd_position = UEVR_Vector3f.new()
local hmd_rotation = UEVR_Quaternionf.new()

local right_controller_index = m_VR.get_right_controller_index()
local right_controller_position = UEVR_Vector3f.new()
local right_controller_rotation = UEVR_Quaternionf.new()

local cam_target = nil

local kismet_math_library_c = api:find_uobject("Class /Script/Engine.KismetMathLibrary")
local kismet_math_library = kismet_math_library_c:get_class_default_object()

local function set2d()

			m_VR.set_mod_value("VR_AimMethod", 0)
			m_VR.set_mod_value("VR_DesktopRecordingFix_V2", "true")
			m_VR.set_mod_value("VR_DecoupledPitch", "false")
			m_VR.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			m_VR.set_mod_value("FrameworkConfig_AlwaysShowCursor", "true")
			m_VR.set_mod_value("VR_2DScreenMode", "true")
			
			m_VR.recenter_horizon()
			
end

local function set3d()

			m_VR.set_mod_value("VR_AimMethod", 2)
			m_VR.set_mod_value("VR_DecoupledPitch", "true")
			m_VR.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			m_VR.set_mod_value("VR_EnableCustomZNear", "true")
			m_VR.set_mod_value("VR_2DScreenMode", "false")
			m_VR.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
			m_VR.set_mod_value("VR_CustomZNear", 1.5)
			
end

local Is_3d = false
local Is_Cutscene = false

local View_Target = nil
local View_Target_old = ""

uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)

	pawn = api:get_local_pawn(0)
	lplayer = api:get_player_controller(0)
	
	scene_c = lplayer.PlayerCameraManager:get_fname():to_string()
	
	View_Target = lplayer:GetViewTarget()
	
	if View_Target:get_full_name() ~= View_Target_old then
	
		print("View_Target: " .. View_Target:get_full_name())
		View_Target_old = View_Target:get_full_name()
		
	end
	
	if string.sub(View_Target:get_fname():to_string(), 0, 10) == "CineCamera" then
		Is_Cutscene = true
	else
		Is_Cutscene = false
	end
	
	if pawn ~= nil and string.sub(scene_c, 0, 25) == "GamePlayerCameraManager_C" then
	
		if pawn.AllMesh ~= nil then
			player_c = pawn:get_fname():to_string()
			
			if player_c ~= player_old then
			
				local all_mesh_arr = pawn.AllMesh[1].AttachChildren
				
				weapon, girl_head, girl_body = nil, nil, pawn.AllMesh[1]

				for i, c_Obj in ipairs(all_mesh_arr) do
					local objName = c_Obj:get_fname():to_string()
					
					if objName == "WeaponRoot" then 
						weapon = c_Obj
					end
					
					if objName == "Head" then
						girl_head = c_Obj
					end
					
				end
				
				player_old = player_c
				
			end
			
		end
		
		if weapon ~= nil and girl_head ~= nil and girl_body ~= nil then
				
					if pawn.CharacterMovement.CharacterStateMachineComponent.RetainWeaponRaising < 2 then
						--pawn.CharacterMovement.CharacterStateMachineComponent.RetainWeaponRaising = 20
					end
					
					if not Is_Cutscene then
					
						if girl_head:IsVisible() then girl_head:SetVisibility(false,true) end
						
						if not girl_body:IsBoneHiddenByName("Bip001-Head") then
							girl_body:HideBoneByName("Bip001-Head", 1)
						end
						
					else
					
						if not girl_head:IsVisible() then girl_head:SetVisibility(true,true) end
						
						if girl_body:IsBoneHiddenByName("Bip001-Head") then
							girl_body:UnHideBoneByName("Bip001-Head")
						end
						
					end
					
					if pawn.CameraArm.bDoCollisionTest == true then pawn.CameraArm.bDoCollisionTest = false end
					if pawn.CameraArm.FadeRadiusPercent > 0 then pawn.CameraArm.FadeRadiusPercent = 0 end
					if pawn.CameraArm.HideCharacterHitTestRadius > 0 then pawn.CameraArm.HideCharacterHitTestRadius = 0 end
					
					Fix_aim.X = 250
					Fix_aim.Y = -60
					Fix_aim.Z = 30
					
					pawn.Camera:K2_SetRelativeLocation(Fix_aim, false, hit_result, false)
					
					if Is_3d and lplayer.bShowMouseCursor and not Is_Cutscene then
					
						set2d()
						Is_3d = false
						print(0)

					elseif not Is_3d and not lplayer.bShowMouseCursor and string.sub(scene_c, 0, 25) == "GamePlayerCameraManager_C" then
					
						set3d()
						Is_3d = true
						print(1)
						
					end
					
		end

	else
		weapon, girl_head, girl_body = nil, nil, nil
	end
	
	if scene_c ~= scene_old then

		if string.sub(scene_c, 0, 25) == "GamePlayerCameraManager_C" then
			
			set3d()
			Is_3d = true
			
		elseif string.sub(scene_c, 0, 24) == "HousePlayerCameraManager" then
		
			set3d()
			Is_3d = true
		
		else
		
			set2d()
			Is_3d = false
			
		end
		
		scene_old = scene_c
	
	end
	
end)

local Z_Offset = -20

uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
	
	if pawn ~= nil and weapon ~= nil and girl_head ~= nil and girl_body ~= nil and string.sub(scene_c, 0, 25) == "GamePlayerCameraManager_C" then
	
		cam_target = girl_body:GetSocketLocation("Bip001-Head")
		
		if Is_Cutscene then
			cam_target = nil
		end
		
		if cam_target ~= nil then
			position.x = cam_target.X
			position.y = cam_target.Y
			position.z = cam_target.Z + 13
		end
		
	end
	
	if pawn ~= nil and string.sub(scene_c, 0, 24) == "HousePlayerCameraManager" then
	
		if pawn.Mesh ~= nil then
		
			if pawn.Mesh:IsVisible() then pawn.Mesh:SetVisibility(false, false) end
		
			cam_target = pawn.Mesh:GetSocketLocation("socket_face")
			
			if pawn.CapsuleComponent:K2_GetComponentScale().x == 1 then
				--pawn.CapsuleComponent:SetWorldScale3D(Vector3d.new(0.01, 0.01, 1))
			end
			
			vr_cam_r.Pitch = rotation.x
			vr_cam_r.Yaw = rotation.y
			vr_cam_r.Roll = rotation.z
			
			local forward_vector = kismet_math_library:Conv_RotatorToVector(vr_cam_r)
			local position_in_front_of_camera = cam_target + (forward_vector * 0)
			
			if m_VR.is_action_active(DPad_Up, left_source) then
				Z_Offset = Z_Offset + 1
			elseif m_VR.is_action_active(DPad_Down, left_source) then
				Z_Offset = Z_Offset - 1
			end
			
			if Is_Cutscene then
				cam_target = nil
			end
			
			if cam_target ~= nil then
				position.x = position_in_front_of_camera.X + 0
				position.y = position_in_front_of_camera.Y + 0
				position.z = position_in_front_of_camera.Z + Z_Offset
			end
			
					if Is_3d and lplayer.bShowMouseCursor and not Is_Cutscene then
					
						set2d()
						Is_3d = false
						print(0)

					elseif not Is_3d and not lplayer.bShowMouseCursor and string.sub(scene_c, 0, 24) == "HousePlayerCameraManager" then
					
						set3d()
						Is_3d = true
						print(1)
						
					end
		
		end
		
	end
	
end)

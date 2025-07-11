--################################
--# VLADiK BRUTAL VR Fix - CJ117 #
--################################

local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks

local fp_mesh = nil
local runonce = false
local in_menu = true
local weap_loc = nil
local cur_weap = nil
local inventory_open = false
local mDown = false
local mUp = false
local offset = {}
local adjusted_offset = {}
local base_pos = { 0, 0, 0 }
local mAttack = false
local mDownC = 0
local mUpC = 0
local mDB = false
local base_dif = 0
local Mactive = false
local Playing = false
local JustCentered = false
local is_running = false
local is_right_click = false
local is_dead = false
local open_scene = false
local mel_weap_type = false
local is_pickup = false
local pickup_active = 0
local logos_active = false
local is_moving = nil
local TutSOnce = false
local TutCOnce = false
local is_loading = false
local is_loading_screen = false
local is_cut = false

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

local function Logo()
	local skeletal_mesh_c = api:find_uobject("Class /Script/UMG.UserWidget")
	if skeletal_mesh_c ~= nil then
		local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)


		for i, mesh in ipairs(skeletal_meshes) do
			if string.find(mesh:get_fname():to_string(), "W_Logo_Video_C_") and string.find(mesh:get_full_name(), "Transient.GameEngine") then
				logos_active = true
				--print(tostring(Fpmesh:get_full_name()))

				break
			else
				logos_active = false
			end
		end
	end
end

local function Pikup()
	local skeletal_mesh_c = api:find_uobject("Class /Script/LevelSequence.LevelSequenceDirector")
	if skeletal_mesh_c ~= nil then
		local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)


		for i, mesh in ipairs(skeletal_meshes) do
			if string.find(mesh:get_fname():to_string(), "SequenceDirector_C_") and string.find(mesh:get_full_name(), "PersistentLevel") then
				if string.find(mesh:get_full_name(), "PikUP")
					or string.find(mesh:get_full_name(), "PikUp")
					or string.find(mesh:get_full_name(), "AZ_8")
					or string.find(mesh:get_full_name(), "Level_8.PersistentLevel.Gavruha")
					or string.find(mesh:get_full_name(), "Ded_Kat_Scene")
					or string.find(mesh:get_full_name(), "Level_27.PersistentLevel")
					and not string.find(mesh:get_full_name(), "Boom")
					and not string.find(mesh:get_full_name(), "_1.")
					or string.find(mesh:get_full_name(), "Door")
					and not string.find(mesh:get_full_name(), "Level_3")
					and not string.find(mesh:get_full_name(), "Level_4")
					and not string.find(mesh:get_full_name(), "Boom") then
					pickup_active = mesh.Player.Status

					--break
				end
				--print(tostring(Fpmesh:get_full_name()))
			end
		end
	end
end

print("VLADiK BRUTAL VR - CJ117")
params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
UEVR_UObjectHook.set_disabled(true)
params.vr.set_aim_method(0)
params.vr.set_mod_value("VR_DPadShiftingMethod", "2")

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
	local tut_c = api:find_uobject("Class /Script/Engine.Actor")
	local tutshow_fn = tut_c:find_function("EnableInput")
	local tuthide_fn = tut_c:find_function("DisableInput")

	if tutshow_fn ~= nil and TutSOnce == false then
		print("Found Starter function")
		TutSOnce = true
		tutshow_fn:hook_ptr(nil, function(fn, obj, locals, result)
			is_loading_screen = false
			--print("Started")
		end)
	end

	if tuthide_fn ~= nil and TutCOnce == false and TutSOnce == true then
		print("Found Ender function")
		TutCOnce = true
		tuthide_fn:hook_ptr(nil, function(fn, obj, locals, result)
			is_loading_screen = true
			--print("Stopped")
		end)
	end


	local pawn = api:get_local_pawn(0)
	local pcont = api:get_player_controller(0)

	if pawn ~= nil and not string.find(pawn:get_full_name(), "Uaz_Kat_scene_C") and not string.find(pawn:get_full_name(), "Car_") then
		in_menu = pcont.bShowMouseCursor
		fp_mesh = pawn.Mesh_FPP
		active_weap = pawn.EquippedWeapon
		is_pickup = pawn.Pik_Up_Object_On
		is_cut = pawn.cat_scene
		Logo()
		Pikup()

		if pawn.Open_Inventore ~= nil then
			inventory_open = pawn.Open_Inventore.bIsActive
		end

		is_dead = pawn.IsDead

		if active_weap ~= nil then
			cur_weap = pawn.EquippedWeapon.Weapon_Mesh_FPP
		end
	end

	if Open_Inventore == nil and in_menu == false then
		open_scene = true
	else
		open_scene = false
	end

	--print(pawn:get_full_name())
	if in_menu == true or is_cut == true or is_loading_screen == true or logos_active == true or pickup_active == 1 or is_dead == true or open_scene == false or string.find(pawn:get_full_name(), "Uaz_Kat_scene_C") or string.find(pawn:get_full_name(), "Car_") then
		if Mactive == false then
			Mactive = true
			Playing = false
			mDB = false
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
			params.vr.set_aim_method(0)
			if string.find(pawn:get_full_name(), "Car_") or is_cut == true then
				params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
			else
				params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
			end
		end
		if in_menu == true or is_loading == 5 then
			if not string.find(pawn:get_full_name(), "Car_") then
				pawn.Mesh_FPP:call("SetRenderInMainPass", false)
			end
		end
	else
		if Playing == false then
			Mactive = false
			Playing = true
			mDB = false
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			UEVR_UObjectHook.set_disabled(false)
			params.vr.set_aim_method(2)
			params.vr.set_mod_value("VR_DPadShiftingMethod", "0")

			fp_mesh.RelativeLocation.Z = -115.000
			fp_mesh.RelativeScale3D.X = 1.150
			fp_mesh.RelativeScale3D.Y = 1.150
			fp_mesh.RelativeScale3D.Z = 1.150
		end
		if active_weap and cur_weap ~= nil then
			if string.find(active_weap:get_full_name(), "Granata") then
				weap_loc = UEVR_UObjectHook.remove_motion_controller_state(cur_weap)
				cur_weap = pawn.EquippedWeapon.Granata_2_GRZ
				weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(cur_weap)
			else
				weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(cur_weap)
			end

			if weap_loc then
				weap_loc:set_hand(1)
				weap_loc:set_permanent(true)
				weap_loc = UEVR_UObjectHook.remove_motion_controller_state(cur_weap)
				weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(cur_weap)
				if string.find(cur_weap:get_full_name(), "AC_VALL") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -4.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Revolver") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(-3.000, -9.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Pistol_TT") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 0.000, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -13.000, 9.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "AKSU") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -9.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Shotgun_Osnova") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -9.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Regulator") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 3.140, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -28.000, -30.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Shotgun_Barrel_Osnova") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(-10.000, -17.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Sniper") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.580, -0.000))
					weap_loc:set_location_offset(Vector3f.new(-28.000, -15.000, 0.000))
					active_weap.Override_ADS_WithCustomEvent = false
					mel_weap_type = false
				elseif string.find(cur_weap:get_full_name(), "Rpg7") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.480, -0.000))
					weap_loc:set_location_offset(Vector3f.new(14.600, -10.000, 0.000))
					mel_weap_type = false
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Granata") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.000, 0.000, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, -7.000, 3.000))
					mel_weap_type = true
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Molotov") then
					weap_loc:set_rotation_offset(Vector3f.new(-0.000, 0.000, -1.560))
					weap_loc:set_location_offset(Vector3f.new(0.000, 0.000, 5.000))
					mel_weap_type = true
					active_weap.Override_ADS_WithCustomEvent = true
				elseif string.find(cur_weap:get_full_name(), "Crowbar") then
					weap_loc:set_rotation_offset(Vector3f.new(0.310, 0.000, -0.000))
					weap_loc:set_location_offset(Vector3f.new(0.000, 20.000, 5.000))
					mel_weap_type = true
					active_weap.Override_ADS_WithCustomEvent = true
				end
			end
		end


		if Playing == true and mel_weap_type == true then
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
		if Mactive == true then
			if state.Gamepad.bLeftTrigger ~= 0 and state.Gamepad.bRightTrigger ~= 0 then
				if JustCentered == false then
					JustCentered = true
					reset_height()
					params.vr.recenter_view()
					JustCentered = false
				end
			end
		end

		if Playing == true then
			if state.Gamepad.sThumbRY >= 30000 then
				if is_running == false then
					is_running = true
					state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_LEFT_THUMB
					--print("Running")
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

		if Playing == true and mAttack == true and mel_weap_type == true then
			mAttack = false
			state.Gamepad.bRightTrigger = 255
		end
	end
end)

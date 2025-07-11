--##############################
--# Scars Above Vr Fix - CJ117 #
--##############################

local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks

local fp_mesh = nil
local in_menu = nil
local is_menu = nil
local weap_loc = nil
local cur_weap = nil
local active_weap = nil
local is_active_weap = "None"
local inventory_open = false
local Mactive = false
local Playing = false
local JustCentered = false
local is_running = false
local is_scanning = false
local is_talking = false
local is_death_start = false
local is_right_click = false
local inv = nil
local inv_open = nil
local menu_open = false
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
local is_hitting = false
local is_dead = false
local open_scene = false
local is_pickup = false
local CtSOnce = false
local CtStOnce = false
local CtiSOnce = false
local CtiStOnce = false
local vera_cam = nil
local anim_cam = nil
local anim_start = false
local n_anim_start = false
local narrative_cam = nil
local narrative_start = false
local narrative_type = nil
local nar_act_cam = nil
local narrative_end = false
local in_narrative = false
local playing_narrative = false
local pc_start = false
local pc_stop = false
local hide_mesh = nil
local hide_weap = nil
local is_drone_scan = false
local is_ar_construct = nil
local pinned = false
local is_aiming = false
local is_aim = nil
local is_tutorial = nil
local tutorial_active = false
local reading_tut = false
local interact_zoom = nil
local cut_active = false
local in_cut = false
local is_examine = false
local examine_object = nil
local is_crafting = false
local craft_item = nil
local walk_move = nil
local walk_speed = nil
local do_speed = false
local in_storm = false
local run_active = false
local chapter = nil
local w_mesh = nil
local is_weap_active = nil
local is_state = nil
local anim_playing = false
local current_talk = nil
local alien_talk = false
local is_puzzle = false
local puzzle_active = false
local wait_for_play = false
local is_shift = false
local is_shift_end = false
local isinteracting = false
local is_throwing = false
local glitch_damage = false
local ui_damage = nil
local is_cut_cam = false
local flip = false
local weap_sel = false
local is_vignette = nil
local end_credits = nil
local is_log = nil
local is_in_log = false
local playing_E_narrative = false
local is_keypad = false
local fsm_payload = "None"
local payload_active = false
local gadget_wheel = nil
local last_level = nil
local level_name = nil

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
		InitLocY = 0.10
	else
		InitLocY = -0.40
	end
end

local function Cross_check()
	local skeletal_mesh_c = api:find_uobject("Class /Script/Project.CrosshairWidget")
    if skeletal_mesh_c ~= nil then 

		local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

		local mesh = nil
		for i, mesh in ipairs(skeletal_meshes) do
			if string.find(mesh:get_full_name(), "W_Crosshair_") and string.find(mesh:get_full_name(), "W_CrosshairHost_C") and string.find(mesh:get_full_name(), "Transient.GameEngine") then
				if string.find(mesh:get_full_name(), "VOLTAPrototype") then
					mesh.MImg_Crosshair_BL.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_BR.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_UL.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_UR.ActiveBrush.DrawAs = 0
					mesh.MImg_Dot.ActiveBrush.DrawAs = 0
					mesh.MImg_B.ActiveBrush.DrawAs = 0
					mesh.MImg_L.ActiveBrush.DrawAs = 0
					mesh.MImg_R.ActiveBrush.DrawAs = 0
					mesh.MImg_U.ActiveBrush.DrawAs = 0
				elseif string.find(mesh:get_full_name(), "ChemicalShotgun") then
					mesh.MImg_CrosshairL.ActiveBrush.DrawAs = 0
					mesh.MImg_CrosshairR.ActiveBrush.DrawAs = 0
					mesh.MImg_CrosshairL_Back.ActiveBrush.DrawAs = 0
					mesh.MImg_CrosshairR_Back.ActiveBrush.DrawAs = 0
					mesh.MImg_CrosshairL_Small.ActiveBrush.DrawAs = 0
					mesh.MImg_CrosshairR_Small.ActiveBrush.DrawAs = 0	
				elseif string.find(mesh:get_full_name(), "CryoLauncher") then
					mesh.MImg_Bottom.ActiveBrush.DrawAs = 0
					mesh.MImg_TopLeft.ActiveBrush.DrawAs = 0
					mesh.MImg_TopRight.ActiveBrush.DrawAs = 0
				elseif string.find(mesh:get_full_name(), "ThermiteLauncherPrototype") or string.find(mesh:get_full_name(), "HeatBeamPrototype") then
					mesh.MImg_Crosshair_D.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_U.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_L.ActiveBrush.DrawAs = 0
					mesh.MImg_Crosshair_R.ActiveBrush.DrawAs = 0
				else
					if mesh.MImg_DL ~= nil and Playing == true then
						mesh.MImg_DL.ActiveBrush.DrawAs = 0
						mesh.MImg_DR.ActiveBrush.DrawAs = 0
						mesh.MImg_UL.ActiveBrush.DrawAs = 0
						mesh.MImg_UR.ActiveBrush.DrawAs = 0
						mesh.MImg_Dot.ActiveBrush.DrawAs = 0
					elseif mesh.MImg_DL ~= nil and Playing == false then
						mesh.MImg_DL.ActiveBrush.DrawAs = 0
						mesh.MImg_DR.ActiveBrush.DrawAs = 0
						mesh.MImg_UL.ActiveBrush.DrawAs = 0
						mesh.MImg_UR.ActiveBrush.DrawAs = 0	
					end
				end

				--break
			end
		end
	end
end

local function Vera_Craft()
	local skeletal_mesh_c = api:find_uobject("Class /Script/Project.ScreenCaptureExaminationSubject")
    if skeletal_mesh_c ~= nil then 

		local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)

		for i, vera_cam in ipairs(skeletal_meshes) do
			if string.find(vera_cam:get_full_name(), "SUB_Intro_Hermes") and string.find(vera_cam:get_full_name(), "BP_Examination_Vera_2") then
				pc_start = vera_cam.ModulationStarted
				pc_stop = vera_cam.ModulationFinished
				is_calibrating = vera_cam.CalibrationStarted

				if is_calibrating == true then
					vera_cam.SpringArm.SocketOffset.X = -23.760
					vera_cam.SpringArm.SocketOffset.Y = 0
					vera_cam.SpringArm.SocketOffset.Z = -33.060
				elseif pc_start == true then
					vera_cam.SpringArm.SocketOffset.X = -11.130
					vera_cam.SpringArm.SocketOffset.Y = 0
					vera_cam.SpringArm.SocketOffset.Z = 10.940
				elseif vera_cam.SpringArm ~= nil then
					vera_cam.SpringArm.SocketOffset.X = 0
					vera_cam.SpringArm.SocketOffset.Y = 0
					vera_cam.SpringArm.SocketOffset.Z = 0
				end	

				break
			end
		end
	end
end

local function Anim_Detect()
	local skeletal_mesh_c = api:find_uobject("Class /Script/LevelSequence.LevelSequenceDirector")
    if skeletal_mesh_c ~= nil then 

		local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
		for i, anim_cam in ipairs(skeletal_meshes) do
			if string.find(anim_cam:get_fname():to_string(), "SequenceDirector_C") then
				anim_start = anim_cam.Player.Status
				--print(anim_start)
				--break
			end
		end
	end
end

local function Narrative_Detect()
	
	local narrative_c = api:find_uobject("Class /Script/Engine.Actor")
    if narrative_c ~= nil then 
	
		local narrative_meshes = narrative_c:get_objects_matching(false)

		local nar_H_act_cam = nil
		for i, nar_H_act_cam in ipairs(narrative_meshes) do
			
			
			if string.find(nar_H_act_cam:get_fname():to_string(), "ACTOR_NarrativeHandler") then
				if nar_H_act_cam.UseCameraFocus ~= nil then
					--nar_H_act_cam.UseCameraFocus = false
					--nar_H_act_cam.UseHeadFocus = false
				end	
				
				in_narrative = nar_H_act_cam.NarrativeEventOngoing
				narrative_end = nar_H_act_cam.CFCompleted
				
				if in_narrative == true and narrative_end == false then
					n_anim_start = true
					
					break
				else
					n_anim_start = false
				end	
				
				
				--print(nar_H_act_cam:get_full_name())
				--print(nar_H_act_cam:get_fname():to_string())
				--print(nar_H_act_cam:get_full_name())

			end
		end
	end
end

local function Puzzle_Detect()
	local narrative_c = api:find_uobject("Class /Script/Engine.Actor")
	if narrative_c ~= nil and Mactive == true then

		local narrative_meshes = narrative_c:get_objects_matching(false)
		
		local nar_act_cam = nil
		for i, nar_act_cam in ipairs(narrative_meshes) do
			if string.find(nar_act_cam:get_fname():to_string(), "BP_CraneHookPuzzle2") then
				
				is_puzzle = nar_act_cam.IsInsidePuzzle
				if is_puzzle == true then
					puzzle_active = true
				end
				
				--print(nar_act_cam:get_full_name())
				break
			end
		end
	end	
end

local function hide_body()
	local bpawn = api:get_local_pawn(0)
	if bpawn ~= nil then
		local body_parts = bpawn.Array_CharacterMeshes
		for i, mesh in ipairs(body_parts) do
			hide_mesh = mesh
			hide_mesh:call("SetRenderInMainPass", false)
		end
		bpawn.CatDoll:call("SetRenderInMainPass", false)
		bpawn.HolsteredKnife:call("SetRenderInMainPass", false)
	end	
end

local function show_body()
	local bpawn = api:get_local_pawn(0)
	if bpawn ~= nil then
		local body_parts = bpawn.Array_CharacterMeshes
		for i, mesh in ipairs(body_parts) do
			hide_mesh = mesh
			hide_mesh:call("SetRenderInMainPass", true)
		end
		bpawn.CatDoll:call("SetRenderInMainPass", true)
		bpawn.HolsteredKnife:call("SetRenderInMainPass", true)
	end	
end

local function machete_hide_weapons()
	local wpawn = api:get_local_pawn(0)
	if wpawn ~= nil then
		local weaps = wpawn.Children
		
		local is_cur_weap_mesh = nil
		local is_cur_weap_mod = nil
		local is_cur_weap_chem = nil
		for i, mesh in ipairs(weaps) do
			hide_weap = mesh
			
			is_cur_weap_mesh = hide_weap.WeaponMesh
			is_cur_weap_mod = hide_weap.ModMesh
			is_cur_weap_chem = hide_weap.ChemicalShotgun
			if string.find(hide_weap:get_full_name(), "Machete") then
				if is_cur_weap_mesh ~= nil then
					is_cur_weap_mesh:call("SetRenderInMainPass", true)
					is_cur_weap_mesh:call("SetVisibility", true)
				end
			else
				if is_cur_weap_mesh ~= nil then
					is_cur_weap_mesh:call("SetRenderInMainPass", false)
					is_cur_weap_mesh:call("SetVisibility", false)
				end
				if is_cur_weap_mod ~= nil then
					is_cur_weap_mod:call("SetRenderInMainPass", false)
					is_cur_weap_mod:call("SetVisibility", false)
				end
				if is_cur_weap_chem ~= nil then
					is_cur_weap_chem:call("SetRenderInMainPass", false)
					is_cur_weap_chem:call("SetVisibility", false)
				end
				
				if string.find(hide_weap:get_full_name(), "EnergyRail") then
					hide_weap.P_EnergyRail_Idle:call("SetRenderInMainPass", false)
					hide_weap.P_EnergyRail_Idle_2_Boost:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "CryoLauncher") then
					hide_weap.P_CryoLauncher_Idle_Test3:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "ChemicalShotgun") then
					hide_weap.P_ChemicalShotgun_MuzzleIdle:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle1:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle2:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle3:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle4:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle5:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle6:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle7:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "HeatBeam") then
					hide_weap.SM_HeatBeam_01:call("SetRenderInMainPass", false)
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "VOLTA") then
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
					hide_weap.Volta:call("SetRenderInMainPass", false)		
				end
			end
		end
	end
end

local function machete_show_weapons()
	local wpawn = api:get_local_pawn(0)
	local weaps = wpawn.Children
	
	local is_cur_weap_mod = nil
	local is_cur_weap_chem = nil
	local is_cur_weap_mesh = nil
	for i, mesh in ipairs(weaps) do
		hide_weap = mesh
		if string.find(hide_weap:get_full_name(), "Machete") then
			is_cur_weap_mesh = hide_weap.WeaponMesh
			if is_cur_weap_mesh ~= nil then
				is_cur_weap_mesh:call("SetRenderInMainPass", false)
				is_cur_weap_mesh:call("SetVisibility", false)
			end
		else
			is_cur_weap_mesh = hide_weap.WeaponMesh
			is_cur_weap_mod = hide_weap.ModMesh
			is_cur_weap_chem = hide_weap.ChemicalShotgun
			
			if is_cur_weap_mesh ~= nil then
				is_cur_weap_mesh:call("SetRenderInMainPass", true)
				is_cur_weap_mesh:call("SetVisibility", true)
			end
			if is_cur_weap_mod ~= nil then
				is_cur_weap_mod:call("SetRenderInMainPass", true)
				is_cur_weap_mod:call("SetVisibility", true)
			end
			if is_cur_weap_chem ~= nil then
				is_cur_weap_chem:call("SetRenderInMainPass", true)
				is_cur_weap_chem:call("SetVisibility", true)
			end
			
			if string.find(hide_weap:get_full_name(), "EnergyRail") then
				hide_weap.P_EnergyRail_Idle:call("SetRenderInMainPass", true)
				hide_weap.P_EnergyRail_Idle_2_Boost:call("SetRenderInMainPass", true)
			elseif string.find(hide_weap:get_full_name(), "CryoLauncher") then
				hide_weap.P_CryoLauncher_Idle_Test3:call("SetRenderInMainPass", true)
			elseif string.find(hide_weap:get_full_name(), "ChemicalShotgun") then
				hide_weap.P_ChemicalShotgun_MuzzleIdle:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle1:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle2:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle3:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle4:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle5:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle6:call("SetRenderInMainPass", true)
				hide_weap.P_ChemicalShotgun_MuzzleIdle7:call("SetRenderInMainPass", true)
			elseif string.find(hide_weap:get_full_name(), "HeatBeam") then
				hide_weap.SM_HeatBeam_01:call("SetRenderInMainPass", true)
				hide_weap.ModMesh:call("SetRenderInMainPass", false)
				hide_weap.ModMesh:call("SetVisibility", false)	
			elseif string.find(hide_weap:get_full_name(), "VOLTA") then
				if hide_weap.Volta.bIsActive == true then
					hide_weap.ModMesh:call("SetRenderInMainPass", true)
					hide_weap.ModMesh:call("SetVisibility", true)
				else
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
					hide_weap.ModMesh:call("SetVisibility", false)
				end
				hide_weap.Volta:call("SetRenderInMainPass", true)	
			end
		end
	end
end

local function hide_weapons()
	local wpawn = api:get_local_pawn(0)
	if wpawn ~= nil then
		local weaps = wpawn.Children
		
		local is_cur_weap_mesh = nil
		local is_cur_weap_mod = nil
		local is_cur_weap_chem = nil
		for i, mesh in ipairs(weaps) do
			hide_weap = mesh
			if not string.find(hide_weap:get_full_name(), "Machete") then
				is_cur_weap_mesh = hide_weap.WeaponMesh
				is_cur_weap_mod = hide_weap.ModMesh
				is_cur_weap_chem = hide_weap.ChemicalShotgun
				
				if is_cur_weap_mesh ~= nil then
					is_cur_weap_mesh:call("SetRenderInMainPass", false)
					is_cur_weap_mesh:call("SetVisibility", false)
				end
				if is_cur_weap_mod ~= nil then
					is_cur_weap_mod:call("SetRenderInMainPass", false)
					is_cur_weap_mod:call("SetVisibility", false)
				end
				if is_cur_weap_chem ~= nil then
					is_cur_weap_chem:call("SetRenderInMainPass", false)
					is_cur_weap_chem:call("SetVisibility", false)
				end
				
				if string.find(hide_weap:get_full_name(), "EnergyRail") then
					hide_weap.P_EnergyRail_Idle:call("SetRenderInMainPass", false)
					hide_weap.P_EnergyRail_Idle_2_Boost:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "CryoLauncher") then
					hide_weap.P_CryoLauncher_Idle_Test3:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "ChemicalShotgun") then
					hide_weap.P_ChemicalShotgun_MuzzleIdle:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle1:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle2:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle3:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle4:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle5:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle6:call("SetRenderInMainPass", false)
					hide_weap.P_ChemicalShotgun_MuzzleIdle7:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "HeatBeam") then
					hide_weap.SM_HeatBeam_01:call("SetRenderInMainPass", false)
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
				elseif string.find(hide_weap:get_full_name(), "VOLTA") then
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
					hide_weap.Volta:call("SetRenderInMainPass", false)		
				end
			end
		end
	end
end

local function show_weapons()
	if active_weap ~= nil and string.find(active_weap:get_full_name(), "Machete") then else
		local wpawn = api:get_local_pawn(0)
		local weaps = wpawn.Children
		
		local is_cur_weap_mod = nil
		local is_cur_weap_chem = nil
		local is_cur_weap_mesh = nil
		for i, mesh in ipairs(weaps) do
			hide_weap = mesh
			if not string.find(hide_weap:get_full_name(), "Machete") then
				is_cur_weap_mesh = hide_weap.WeaponMesh
				is_cur_weap_mod = hide_weap.ModMesh
				is_cur_weap_chem = hide_weap.ChemicalShotgun

				if is_cur_weap_mesh ~= nil then
					is_cur_weap_mesh:call("SetRenderInMainPass", true)
					is_cur_weap_mesh:call("SetVisibility", true)
				end
				if is_cur_weap_mod ~= nil then
					is_cur_weap_mod:call("SetRenderInMainPass", true)
					is_cur_weap_mod:call("SetVisibility", true)
				end
				if is_cur_weap_chem ~= nil then
					is_cur_weap_chem:call("SetRenderInMainPass", true)
					is_cur_weap_chem:call("SetVisibility", true)
				end
				
				if string.find(hide_weap:get_full_name(), "EnergyRail") then
					hide_weap.P_EnergyRail_Idle:call("SetRenderInMainPass", true)
					hide_weap.P_EnergyRail_Idle_2_Boost:call("SetRenderInMainPass", true)
				elseif string.find(hide_weap:get_full_name(), "CryoLauncher") then
					hide_weap.P_CryoLauncher_Idle_Test3:call("SetRenderInMainPass", true)
				elseif string.find(hide_weap:get_full_name(), "ChemicalShotgun") then
					hide_weap.P_ChemicalShotgun_MuzzleIdle:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle1:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle2:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle3:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle4:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle5:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle6:call("SetRenderInMainPass", true)
					hide_weap.P_ChemicalShotgun_MuzzleIdle7:call("SetRenderInMainPass", true)
				elseif string.find(hide_weap:get_full_name(), "HeatBeam") then
					hide_weap.SM_HeatBeam_01:call("SetRenderInMainPass", true)
					hide_weap.ModMesh:call("SetRenderInMainPass", false)
					hide_weap.ModMesh:call("SetVisibility", false)					
				elseif string.find(hide_weap:get_full_name(), "VOLTA") then
					if hide_weap.Volta.bIsActive == true then
						hide_weap.ModMesh:call("SetRenderInMainPass", true)
						hide_weap.ModMesh:call("SetVisibility", true)
					else
						hide_weap.ModMesh:call("SetRenderInMainPass", false)
						hide_weap.ModMesh:call("SetVisibility", false)
					end
					hide_weap.Volta:call("SetRenderInMainPass", true)	
				end
			end
		end
	end
end

local function WeaponSelect()
	--D-Pad
	if Playing == true then
		local InitRot = 0.70
		local InitLocY = 0.60
		local InitLocZ = 0.07
		local InitLocW = -0.60
		local right_controller_index = params.vr.get_right_controller_index()
		local right_controller_position = UEVR_Vector3f.new()
		local right_controller_rotation = UEVR_Quaternionf.new()
		params.vr.get_pose(right_controller_index, right_controller_position, right_controller_rotation)

		--print("Position: " .. tostring(right_controller_position.x) .. ", " .. tostring(right_controller_position.y) .. ", " .. tostring(right_controller_position.z))
		--print("Rotation: " .. tostring(right_controller_rotation.x) .. ", " .. tostring(right_controller_rotation.w))

		local pose_x_current = right_controller_rotation.x
		local pose_y_current = right_controller_position.y
		local pose_z_current = right_controller_position.z
		local pose_w_current = right_controller_rotation.w
		if pose_x_current >= InitRot and pose_w_current <= InitLocW and weap_sel == false then
			print("Weapon Select Active")
			params.vr.set_aim_method(1)
			params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
			weap_sel = true
			mDB = false
		elseif pose_x_current <= InitRot and pose_w_current >= InitLocW and weap_sel == true then
			print("Weapon Select Closed")
			params.vr.set_aim_method(2)
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
			weap_sel = false
			mDB = false
		end	
	end
end

print("ScarsAbove VR - CJ117")
params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
UEVR_UObjectHook.set_disabled(true)
params.vr.set_aim_method(0)
params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
params.vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
params.vr.set_mod_value("VR_CameraUpOffset", "0.00")
params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
params.vr.set_mod_value("UI_Distance", "3.000")
params.vr.set_mod_value("UI_Size", "2.700")
params.vr.set_mod_value("UI_X_Offset", "0.00")
params.vr.set_mod_value("UI_Y_Offset", "0.00")
params.vr.set_mod_value("UI_FollowView", "false")
			
uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
		
	local pawn = api:get_local_pawn(0)
	local pcont = api:get_player_controller(0)

	if pawn ~= nil then 
		
		Narrative_Detect()
		Anim_Detect()
		Puzzle_Detect()
		is_mouse = pcont.bShowMouseCursor
		in_menu = pawn.FSM_UI.CurrentPayload
		is_menu = pawn.FSM_UI.CurrentContext.CurrentState
		is_aim = pawn.FSM_Aim.CurrentContext.CurrentState
		fp_mesh = pawn.Mesh
		is_scan = pawn.VFX_PulseScan_Anim.bIsActive
		is_shift = pawn.VFX_ShiftStart_Anim.bIsActive
		is_shift_end = pawn.VFX_ShiftEnd_Anim.bIsActive
		is_death_start = pawn.VFX_DeathPillarStart_Anim.bIsActive
		active_weap = pawn.WeaponInHands
		is_pickup = pawn.Pik_Up_Object_On
		is_drone_scan = pawn.P_DroneScanBeam01.bIsActive
		is_ar_construct = pawn.CameraAimAlpha
		is_aiming = pawn.bAimReadyToFire
		is_now_aiming = pawn.bWantsToAim
		is_tutorial = pcont.MyHUD.TutorialAdvancedWidget
		in_cut = pcont.MyHUD.CinematicWidget
		run_active = pawn.bWantsToSprint
		walk_move = pawn.CharacterMovement
		in_storm = pawn.bStorm
		is_state = pawn.PlayerMovementStates.PlayerAnimOverlayState
		is_throwing = pawn.bConsumableInputHeldDown
		end_credits = pcont.MyHUD.CreditsWidget.Visibility
		glitch_damage = pawn.VFX_PP_ScreenGlitchAnim.bIsActive
		ui_damage = pawn.FSM_Main.CurrentContext.CurrentState
		is_cut_cam = pcont.PlayerCameraManager.bClientSimulatingViewTarget
		is_vignette = pcont.MyHUD.VignetteWidget
		
		
		if pcont.MyHUD.GadgetWheelWidget ~= nil then
			gadget_wheel = pcont.MyHUD.GadgetWheelWidget.Visibility
		end
		
		if pawn.FSM_Main.CurrentPayload ~= nil then
			if pawn.FSM_Main.CurrentPayload.InteractionPayload ~= nil then
				if pawn.FSM_Main.CurrentPayload.InteractionPayload.Parent ~= nil then
					payload_active = true
					fsm_payload = pawn.FSM_Main.CurrentPayload.InteractionPayload.Parent:get_full_name()
					is_keypad = pawn.FSM_Main.CurrentPayload.InteractionPayload.Parent.KeypadActive
				end
			else
				payload_active = false
			end	
		end
		is_log = nil
		is_in_log = false
		if pcont.MyHUD.InGameMenuWidget ~= nil then
			if pcont.MyHUD.InGameMenuWidget.LogScreenWidget ~= nil then
				is_log = pcont.MyHUD.InGameMenuWidget.LogScreenWidget.Visibility
				is_in_log = pcont.MyHUD.InGameMenuWidget.LogScreenWidget.bIsCategorySoundEnabled
			end
		end
		
		if ui_damage == 5 or ui_damage == 6 or ui_damage == 7 then
			flip = false
		else
			flip = true
		end
		
		if pcont.MyHUD.VignetteWidget ~= nil then
			is_vignette.Visibility = 0
			is_vignette.ColorAndOpacity.A = 0.0
			is_vignette.ImgVignette.Visibility = 0
		end
		
		if pcont.MyHUD.CrosshairHostWidget.DefaultCrosshair ~= nil and is_examine ~= 4 then
			pcont.MyHUD.CrosshairHostWidget.DefaultCrosshair.ActiveBrush.DrawAs = 0
		end
		
		if pcont.MyHUD.CrosshairHostWidget.CurrentCrosshairWidget ~= nil and is_examine ~= 4 then
			pcont.MyHUD.CrosshairHostWidget.CurrentCrosshairWidget.NotAimingAtEnemyColor.A = 0.0
		end
		
		if is_state ~= 0 and active_weap ~= nil then
			anim_playing = true
		else
			anim_playing = false
		end
		
		if pcont.MyHUD.ChapterNameWidget ~= nil then
			chapter = pcont.MyHUD.ChapterNameWidget.Visibility
		end
		
		if walk_move ~= nil then
			walk_speed = walk_move.MinAnalogWalkSpeed
		end
		
		if in_cut ~= nil then
			cut_active = in_cut.Visibility
		end
		
		if is_tutorial ~= nil then
			tutorial_active = is_tutorial.Visibility
		end
		
		if pawn.CameraZoomTimeline ~= nil then
			interact_zoom = pawn.CameraZoomTimeline.bIsActive
		end
		
		if pcont.MyHUD.DialogueWidget ~= nil then
			current_talk = pcont.MyHUD.DialogueWidget.DialogueIdentifier
			is_talking = pcont.MyHUD.DialogueWidget.SentenceIsBeingPlayed
			if pcont.MyHUD.DialogueWidget.SentenceIsBeingPlayed == true then
				is_sentence = true
			else
				is_sentence = false
			end
			
			if string.find(tostring(current_talk), "DT_VO_01Introhermes_03PlanetApproach") then
				if is_sentence == true then
					isinteracting = true
				end
			elseif string.find(tostring(current_talk), "DT_VO_01Introhermes_03PlanetAliensAppear") then
				if is_sentence == true then
					isinteracting = false
				end
			end
			
			if string.find(tostring(current_talk), "DT_VO_05Underground_01TransitControlPzElevatorNoPower") or 
				string.find(tostring(current_talk), "DT_VO_05Underground_03CavesFearSharp") then
				if is_sentence == true then
					isinteracting = true
				else
					isinteracting = false
				end
			end	
			
			if string.find(tostring(current_talk), "DT_VO_05Underground_02AlienLabPzHintNoDoor") and alien_talk == false or
				string.find(tostring(current_talk), "DT_VO_05Underground_02AlienLabPzArmTakeHint") and alien_talk == false then
				is_talking = pcont.MyHUD.DialogueWidget.SentenceIsBeingPlayed
				if is_talking == true then
					puzzle_active = false
				else
					alien_talk = false
				end	
			else
				alien_talk = false
			end
		end
		
		if alien_talk == true then
			wait_for_play = true
		else 
			wait_for_play = false
		end
		
		if pcont.MyHUD.ExamineScreenWidget ~= nil then
			is_examine = pcont.MyHUD.ExamineScreenWidget.Visibility
			examine_object = pcont.MyHUD.ExamineScreenWidget.ExamineBaseObject
		end
		
		craft_item = nil
		is_crafting = nil
		if pcont.MyHUD.CraftingScreenWidget ~= nil then
			craft_item = pcont.MyHUD.CraftingScreenWidget.RecipeItem
			is_crafting = pcont.MyHUD.CraftingScreenWidget.Visibility
		end

		if in_menu ~= nil and not string.find(in_menu:get_full_name(), "WeaponWheelPayload") then
			menu_open = true
			inv = in_menu.TabMenuCategory 
		else
			menu_open = false
		end
		
		
		if in_storm == true or is_talking == true then
			pawn.CharacterMovement.MinAnalogWalkSpeed = 75.00
			pawn.CharacterMovement.MaxWalkSpeed = 175.00
			do_speed = false
			--print(current_talk)
			--print(tostring(is_talking))
		else
			if is_now_aiming == true then
				pawn.CharacterMovement.MinAnalogWalkSpeed = 275.0
			else
				if do_speed == false then
					do_speed = true
					pawn.CharacterMovement.MaxWalkSpeed = 275.0
					pawn.CharacterMovement.MinAnalogWalkSpeed = 275.0
				end
				pawn.CharacterMovement.MinAnalogWalkSpeed = 125.0
				--pawn.CharacterMovement.MaxWalkSpeed = 125.00
			end
		end
		
		
	end
	


	if pawn == nil or n_anim_start == true or anim_start == 1 or end_credits == 4 or is_cut_cam == true or isinteracting == true or puzzle_active == true or wait_for_play == true or chapter ~=1 or is_menu ~= 0 or menu_open == true or cut_active == 4 or is_examine == 4 or tutorial_active == 0 or is_menu ~= 0 and is_ar_construct ~= 0.0 then
		if Mactive == false and is_menu ~=3 and flip == true then
			Mactive = true
			Playing = false
			if is_menu == 3 then else

				if inventory_open ~= true then
					UEVR_UObjectHook.set_disabled(true)
					hide_weapons()
				end
				if is_drone_scan ~= true or is_ar_construct ~= 0.0 then
					if is_ar_construct ~= 0.0 then
						hide_weapons()
					end
					params.vr.set_aim_method(0)
					params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
					if anim_start ~= 1 and playing_narrative == false and is_drone_scan == false and isinteracting == false then
						params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
					end
				else
					UEVR_UObjectHook.set_disabled(false)
				end
			end
			params.vr.set_mod_value("UI_X_Offset", "0.00")
			params.vr.set_mod_value("UI_Y_Offset", "0.00")
			mDB = false
			print("Menu / Paused / Cut")
		end

		hide_body()
		Vera_Craft()
		
		if is_crafting ~= nil and is_crafting == 4 then
			hide_weapons()
			if string.find(craft_item:get_full_name(), "Vera") then
				params.vr.set_mod_value("VR_CameraForwardOffset", "150.00")
				params.vr.set_mod_value("VR_CameraUpOffset", "-20.00")
			else
				params.vr.set_mod_value("VR_CameraForwardOffset", "100.00")
				params.vr.set_mod_value("VR_CameraUpOffset", "-20.00")
			end	
			if is_keypad == true then
				params.vr.set_mod_value("VR_CameraForwardOffset", "-10.00")
				params.vr.set_mod_value("UI_X_Offset", "0.00")
				params.vr.set_mod_value("UI_Y_Offset", "0.00")
				params.vr.set_mod_value("UI_Distance", "0.400")
				params.vr.set_mod_value("UI_Size", "0.300")
			elseif string.find(fsm_payload, "TamaraTablet") then
				params.vr.set_mod_value("UI_X_Offset", "0.00")
				params.vr.set_mod_value("UI_Y_Offset", "0.00")
				params.vr.set_mod_value("UI_Distance", "0.460")
				params.vr.set_mod_value("UI_Size", "0.500")
			else
				params.vr.set_mod_value("VR_CameraForwardOffset", "100.00")
				params.vr.set_mod_value("UI_Distance", "1.440")
				params.vr.set_mod_value("UI_Size", "1.377")
			end	
		end	
		if is_examine == 4 then
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			if string.find(examine_object:get_full_name(), "CraneHookConsole") then
				params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
			else
				params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
			end
			if is_keypad == true then
				params.vr.set_mod_value("VR_CameraForwardOffset", "-10.00")
				params.vr.set_mod_value("UI_X_Offset", "0.00")
				params.vr.set_mod_value("UI_Y_Offset", "0.00")
				params.vr.set_mod_value("UI_Distance", "0.400")
				params.vr.set_mod_value("UI_Size", "0.300")
			elseif string.find(fsm_payload, "TamaraTablet") then
				params.vr.set_mod_value("UI_X_Offset", "0.00")
				params.vr.set_mod_value("UI_Y_Offset", "0.00")
				params.vr.set_mod_value("UI_Distance", "0.460")
				params.vr.set_mod_value("UI_Size", "0.500")
			else
				params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
				params.vr.set_mod_value("UI_Distance", "1.440")
				params.vr.set_mod_value("UI_Size", "1.377")
			end	
		else
			if is_crafting == 4 then
				params.vr.set_mod_value("UI_Distance", "1.000")
				params.vr.set_mod_value("UI_Size", "0.681")
			else
				params.vr.set_mod_value("UI_Distance", "3.000")
				params.vr.set_mod_value("UI_Size", "2.700")
			end
		end
		if cut_active == 4 then
			if is_crafting == 4 then
				params.vr.set_mod_value("UI_Distance", "1.000")
				params.vr.set_mod_value("UI_Size", "0.681")
			else
				params.vr.set_mod_value("UI_Distance", "3.000")
				params.vr.set_mod_value("UI_Size", "3.700")
			end	
		end
		if pawn ~= nil and fp_mesh ~= nil then
			fp_mesh:call("SetVisibility", false)
		end	
		hide_weapons()
		if is_now_aiming == true or active_weap ~= nil and is_menu == 3 then
			show_weapons()
		end
		if is_log == 4 and is_in_log == true then
			params.vr.set_mod_value("VR_DPadShiftingMethod", "3")
		elseif puzzle_active == true then
			params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
		else
			params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
		end
	else
		if Playing == false then
			Mactive = false
			Playing = true
			pinned = false
			playing_narrative = false
			wait_for_play = false
			alien_talk = false
			show_weapons()
			
			pawn.bFindCameraComponentWhenViewTarget = false
			params.vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
			params.vr.set_mod_value("UI_Distance", "3.000")
			params.vr.set_mod_value("UI_Size", "2.700")
			params.vr.set_mod_value("UI_X_Offset", "0.00")
			params.vr.set_mod_value("UI_Y_Offset", "0.00")
			params.vr.set_mod_value("UI_FollowView", "false")
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			UEVR_UObjectHook.set_disabled(false)
			params.vr.set_aim_method(2)
			params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
			Cross_check()
			params.vr.set_mod_value("VR_CameraUpOffset", "0.00")
			params.vr.set_mod_value("VR_CameraForwardOffset", "0.00")
			print("Playing")
		end	
		if weap_sel == false then
			params.vr.set_mod_value("VR_DPadShiftingMethod", "0")
		end	
		if active_weap ~= nil and is_ar_construct == 0.0 then
			cur_weap = pawn.Children

			if string.find(active_weap:get_full_name(), "EnergyRail") then
				is_active_weap = "EnergyRail"
				show_weapons()
			elseif string.find(active_weap:get_full_name(), "VOLTA") then
				is_active_weap = "VOLTA"
				show_weapons()	
			elseif string.find(active_weap:get_full_name(), "ThermiteCharger") then
				is_active_weap = "ThermiteCharger"
				show_weapons()
			elseif string.find(active_weap:get_full_name(), "CryoLauncher") then
				is_active_weap = "CryoLauncher"
				show_weapons()
			elseif string.find(active_weap:get_full_name(), "ChemicalShotgun") then
				is_active_weap = "ChemicalShotgun"	
				show_weapons()
			elseif string.find(active_weap:get_full_name(), "Machete_Upgraded") then
				is_active_weap = "Machete_Upgraded"	
				hide_weapons()	
			elseif string.find(active_weap:get_full_name(), "Machete") then
				is_active_weap = "Machete"
				machete_hide_weapons()				
			elseif string.find(active_weap:get_full_name(), "HeatBeam") then
				is_active_weap = "HeatBeam"	
				show_weapons()
			else
				is_active_weap = "None"
				hide_weapons()
			end
		end	
		
		
		if Playing == true and cur_weap ~= nil and active_weap ~= nil then
		else
			hide_weapons()
		end
		

		if is_now_aiming == true or active_weap ~= nil then
			show_weapons()
		end
		
		
		Cross_check()
		
		if is_scan == true and is_aim == 0 then
			pawn.bFindCameraComponentWhenViewTarget = true
			pawn.SpringArm.DesiredCameraSetup.TargetArmLength = 0
            		pawn.SpringArm.DesiredCameraSetup.SocketOffset.x = -0
			pawn.SpringArm.DesiredCameraSetup.SocketOffset.y = 0
			pawn.SpringArm.DesiredCameraSetup.SocketOffset.z = 4
			pawn.SpringArm.DesiredCameraSetup.TargetOffset.y = 0
			pawn.SpringArm.DesiredCameraSetup.TargetOffset.z = 0
		else
			pawn.bFindCameraComponentWhenViewTarget = false
			pawn.Mesh.RelativeLocation.x = 14
			pawn.Mesh.RelativeLocation.y = -10
			pawn.Mesh.RelativeLocation.z = -48
			pawn.SpringArm.DesiredCameraSetup.TargetArmLength = 300
            		pawn.SpringArm.DesiredCameraSetup.SocketOffset.x = 0
			pawn.SpringArm.DesiredCameraSetup.SocketOffset.y = 0
			pawn.SpringArm.DesiredCameraSetup.SocketOffset.z = -9.479
			pawn.SpringArm.DesiredCameraSetup.TargetOffset.y = 0
			pawn.SpringArm.DesiredCameraSetup.TargetOffset.z = 0
		end
		if is_death_start == true then
			show_body()
		end
		if is_shift == true then
			params.vr.set_mod_value("VR_CustomZNear", "20.000")
		end
		if is_shift_end == true then
			params.vr.set_mod_value("VR_CustomZNear", "0.001")
		end
		
		
		if Playing == true and weap_sel == false then

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
	WeaponSelect()
	
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
		
		if Playing == false and is_menu ~= 5 and pawn ~= nil or anim_start == 1 then
			if state.Gamepad.bLeftTrigger ~= 0 then
				state.Gamepad.bLeftTrigger = 0
			end
		end
		
		if Playing == false and is_menu ~= 5 and pawn ~= nil or anim_start == 1 then
			if state.Gamepad.bRightTrigger ~= 0 then
				state.Gamepad.bRightTrigger = 0
			end
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
		
		if Playing == true and mAttack == true and weap_sel == false then
			mAttack = false
			state.Gamepad.bRightTrigger = 255
		end
		
	end

end)


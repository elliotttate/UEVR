local uevrUtils = require("libs/uevr_utils")
uevrUtils.setLogLevel(LogLevel.Debug)
uevrUtils.initUEVR(uevr)
local flickerFixer = require("libs/flicker_fixer")
local controllers = require("libs/controllers")
local animation = require("libs/animation")
--animation.setLogLevel(LogLevel.Debug)
local hands = require("libs/hands")
local weapons = require("addons/weapons")
local handAnimations = require("addons/hand_animations")

local handParams = 
{
	Arms = 
	{
		Left = 
		{
			Name = "lowerarm_l",
			Rotation = {0, 90, -90},
			Location = {-2.8, -40.7, -1.5},	
			Scale = {1, 1, 1},			
			AnimationID = "left_hand"
		},
		Right = 
		{
			Name = "lowerarm_r",
			Rotation = {0, -90, 90},
			Location = {-2.8, -40.7, -1.5},		
			Scale = {1, 1, 1},			
			AnimationID = "right_hand"
		}
	}
}
local knuckleBoneList = {24, 12, 15, 21, 18, 48, 36, 39, 45, 42}

local isInCinematic = false
local canCreateHands = false

local g_wasSnapTurnEnabled = nil
function disableSnapTurn(val)
	if val then
		print("Disabling snap turn\n")
		if g_wasSnapTurnEnabled == nil then
			g_wasSnapTurnEnabled = uevr.params.vr.is_snap_turn_enabled()
		end
		uevr.params.vr.set_snap_turn_enabled(false)		
	else
		if g_wasSnapTurnEnabled ~= nil then
			print("Enabling snap turn\n")
			uevr.params.vr.set_snap_turn_enabled(g_wasSnapTurnEnabled)
		else
			print("Snap turn did not need to be enabled\n")
		end
		g_wasSnapTurnEnabled = nil
	end
end

function setFixedCamera(val)
	uevr.params.vr.set_mod_value("UI_FollowView", val and "false" or "true")
	uevr.params.vr.set_aim_method(val and 0 or 2)

end

local wasInCinematic = false
function updateViewState()
	if isInCinematic and not wasInCinematic then
		uevrUtils.print("Start cinematic")
		setFixedCamera(true)
		hands.hideHands(true)
		disableSnapTurn(true)
		uevr.params.vr.recenter_view()
	elseif not isInCinematic and wasInCinematic then
		uevrUtils.print("End cinematic")
		setFixedCamera(false)
		hands.hideHands(false)
		disableSnapTurn(false)
		uevr.params.vr.recenter_view()
	end
	wasInCinematic = isInCinematic
end

function handleLevelChange(level)
	local levelName = level:get_full_name()
	uevrUtils.print(levelName)
	if  string.find(levelName, "MAP_StartingMap") then
		setFixedCamera(true)
	else
		setFixedCamera(false)
	end
	
	--get rid of black bars in cinematics
	for i, slot in ipairs(pawn["FPPHudWidget"]["WB HUD"]["CanvasPanel_0"]["Slots"]) do
		if string.find(slot.Content:get_full_name(), "Image_FrameTop") or string.find(slot.Content:get_full_name(), "Image_FrameBot") then
			slot.Content:SetVisibility(1)
		end
	end
	--Fix the shooting gallery
	local modifiers = uevrUtils.find_all_instances("Class /Script/Game.RotationLimitCameraModifier", false)
	for i, mesh in ipairs(modifiers) do
		if mesh:get_fname():to_string() == "RotationLimitCameraModifier_0" then
			mesh:DisableModifier(true)
			break
		end
	end
	uevr.api:get_player_controller(0).PlayerCameraManager.CachedCameraShakeMod:DisableModifier(true)
	uevr.params.vr.recenter_view()
		
end


function on_level_change(level)
	uevrUtils.print("Level changed")
	--flickerFixer.create()
	controllers.onLevelChange()
	controllers.createController(0)
	controllers.createController(1)
	controllers.createController(2)
	canCreateHands = false
	hands.reset()
	wasInCinematic = false
	disableSnapTurn(false)
	handleLevelChange(level)
	
	delay(2000, function()
		canCreateHands = true
	end)
end

function on_weapon_change(activeWeapon)
	uevrUtils.print("on_weapon_change called " .. (activeWeapon == nil and " with no weapon" or " with active weapon"))
	if activeWeapon ~= nil then
		animation.updateAnimation("right_hand", "right_grip_weapon", false)
		animation.updateAnimation("right_hand", "right_grip_weapon", true)
	else
		animation.updateAnimation("right_hand", "right_grip", false)
	end
end

function on_lazy_poll()
	if canCreateHands and not hands.exists() then
		hands.create(pawn.Mesh, handParams, handAnimations)
		animation.setBoneSpaceLocalRotator(hands.getHandComponent(1), "hand_r", uevrUtils.rotator(0, 0, -90))
		animation.setBoneSpaceLocalRotator(hands.getHandComponent(0), "hand_l", uevrUtils.rotator(0, 0, -90))
		uevrUtils.fixMeshFOV(hands.getHandComponent(0), "UsePanini", 0.0, true, true, false)
		uevrUtils.fixMeshFOV(hands.getHandComponent(1), "UsePanini", 0.0, true, true, false)
		if isInCinematic then
			hands.hideHands(true)
		end
		
		uevrUtils.fixMeshFOV(pawn.Mesh, "UsePanini", 0.0, true, true, false)
		
		local weaponMesh = uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"})
		if weaponMesh ~= nil then uevrUtils.fixMeshFOV(weaponMesh, "UsePanini", 0.0, true, true, false) end
	end
	
	--pawn["FPPHudWidget"]["WB HUD"]["CanvasPanel_0"]:SetVisibility(isInCinematic and 1 or 0) --the black horizontal bars
	--print(#pawn["FPPHudWidget"]["WB HUD"]["CanvasPanel_0"]["Slots"])
	--for in #pawn["FPPHudWidget"]["WB HUD"]["CanvasPanel_0"]["Slots"]

end

function on_xinput_get_state(retval, user_index, state)
	if hands.exists() then
		hands.handleInput(state, weapons.getActiveWeapon() ~= nil)
	end
end


local is_hidden = false
local is_load = false
local detective = falsee
local is_input = false
local is_view = false
local interact_with = nil
local is_interact = false
local is_menu = false

function on_post_engine_tick(engine, delta)
	
	if uevrUtils.getValid(pawn) ~= nil then
		pawn.bFindCameraComponentWhenViewTarget = false
		pawn.PlayerCameraShakeScale = 0
		
		if is_hidden ~= pawn.bHidden then print("is_hidden changed",pawn.bHidden) end
		if is_load ~= pawn.bNetLoadOnClient then print("is_load changed",pawn.bNetLoadOnClient) end
		if detective ~= pawn.DetectiveModeComponent.bIsActive then print("detective changed",pawn.DetectiveModeComponent.bIsActive) end
		if is_input ~= pawn.bInputEnabled then print("is_input changed",pawn.bInputEnabled) end
		if is_view ~= pawn.bIsLocalViewTarget then print("is_view changed",pawn.bIsLocalViewTarget) end
		if interact_with ~= pawn.PawnInteractionComponent.InteractionWith then print("interact_with changed",pawn.PawnInteractionComponent.InteractionWith) end
		if is_interact ~= pawn.PawnInteractionComponent.InteractionTextHidden then print("is_interact changed",pawn.PawnInteractionComponent.InteractionTextHidden) end
		if is_menu ~= uevrUtils.get_world().AuthorityGameMode.bIsInGameMenuShown then print("is_menu changed",uevrUtils.get_world().AuthorityGameMode.bIsInGameMenuShown) end
		
		is_hidden = pawn.bHidden
		is_load = pawn.bNetLoadOnClient
		detective = pawn.DetectiveModeComponent.bIsActive
		is_input = pawn.bInputEnabled
		is_view = pawn.bIsLocalViewTarget
		interact_with = pawn.PawnInteractionComponent.InteractionWith
		is_interact = pawn.PawnInteractionComponent.InteractionTextHidden
		is_menu = uevrUtils.get_world().AuthorityGameMode.bIsInGameMenuShown
		
		if is_view and is_input then
			isInCinematic = false
		else
			isInCinematic = true
		end
	end
	
	updateViewState()
	
	local weaponMesh = uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"})
	if weaponMesh ~= nil then 
		weapons.attachWeapon(weaponMesh)
		local weaponConfig = uevrUtils.getValid(pawn,{"Weapon","FPPWeaponConfig"})
		if weaponConfig ~= nil then
			weaponConfig.bSwayWeapon = false
			weaponConfig.bUseRecoil = false
			weaponConfig.bUseSpread = false
			weaponConfig.AimingSwayX = 0
			weaponConfig.HipsSwayX = 0
		end
	end

	local mesh = uevrUtils.getValid(pawn, {"Mesh"})
	if mesh ~= nil then 
		local breachOverlapActor = uevrUtils.getValid(pawn, {"BreachOverlapActor"})
		if breachOverlapActor ~= nil and breachOverlapActor.bBreachingStarted then
			mesh:call("SetRenderInMainPass", true)
		else
			mesh:call("SetRenderInMainPass", false) 
		end
	end

	-- local weaponMesh = uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"})
	-- if weaponMesh ~= nil then 
	-- uevrUtils.fixMeshFOV(weaponMesh, "UsePanini", 0.0, true, true, false) end

end


register_key_bind("F1", function()
    print("F1 pressed\n")
	--animation.logBoneNames(pawn.Mesh)
	--animation.getHierarchyForBone(pawn.Mesh, "lowerarm_r")
	--hands.createSkeletalVisualization()
	--hands.enableHandAdjustments(knuckleBoneList)
	
	-- local component = hands.getHandComponent(1)
	-- component:CopyPoseFromSkeletalComponent(pawn.Mesh)
	-- animation.logBoneRotators(component, knuckleBoneList)
end)


-- [animation] 0 Root
-- [animation] 1 Pelvis
-- [animation] 2 spine_01
-- [animation] 3 spine_02
-- [animation] 4 spine_03
-- [animation] 5 neck_01
-- [animation] 6 Head
-- [animation] 7 Camera
-- [animation] 8 clavicle_l
-- [animation] 9 upperarm_l
-- [animation] 10 lowerarm_l
-- [animation] 11 hand_l
-- [animation] 12 index_01_l
-- [animation] 13 index_02_l
-- [animation] 14 index_03_l
-- [animation] 15 middle_01_l
-- [animation] 16 middle_02_l
-- [animation] 17 middle_03_l
-- [animation] 18 pinky_01_l
-- [animation] 19 pinky_02_l
-- [animation] 20 pinky_03_l
-- [animation] 21 ring_01_l
-- [animation] 22 ring_02_l
-- [animation] 23 ring_03_l
-- [animation] 24 thumb_01_l
-- [animation] 25 thumb_02_l
-- [animation] 26 thumb_03_l
-- [animation] 27 Weapon_l
-- [animation] 28 lowerarm_twist_01_l
-- [animation] 29 forearmsFlashlight
-- [animation] 30 forearmsCover
-- [animation] 31 upperarm_twist_01_l
-- [animation] 32 clavicle_r
-- [animation] 33 upperarm_r
-- [animation] 34 lowerarm_r
-- [animation] 35 hand_r
-- [animation] 36 index_01_r
-- [animation] 37 index_02_r
-- [animation] 38 index_03_r
-- [animation] 39 middle_01_r
-- [animation] 40 middle_02_r
-- [animation] 41 middle_03_r
-- [animation] 42 pinky_01_r
-- [animation] 43 pinky_02_r
-- [animation] 44 pinky_03_r
-- [animation] 45 ring_01_r
-- [animation] 46 ring_02_r
-- [animation] 47 ring_03_r
-- [animation] 48 thumb_01_r
-- [animation] 49 thumb_02_r
-- [animation] 50 thumb_03_r
-- [animation] 51 Weapon
-- [animation] 52 DataSpike_Base
-- [animation] 53 DataSpike_Needle
-- [animation] 54 pinky_r_root
-- [animation] 55 index_r_root
-- [animation] 56 middle_r_root
-- [animation] 57 ring_r_root
-- [animation] 58 lowerarm_twist_01_r
-- [animation] 59 upperarm_twist_01_r
-- [animation] 60 thigh_l
-- [animation] 61 calf_l
-- [animation] 62 calf_twist_01_l
-- [animation] 63 foot_l
-- [animation] 64 ball_l
-- [animation] 65 ball_l_end
-- [animation] 66 thigh_twist_01_l
-- [animation] 67 thigh_r
-- [animation] 68 calf_r
-- [animation] 69 calf_twist_01_r
-- [animation] 70 foot_r
-- [animation] 71 ball_r
-- [animation] 72 ball_r_end
-- [animation] 73 thigh_twist_01_r
-- [animation] 74 thigh_open_side_01_move
-- [animation] 75 thigh_open_side_02_rotate
-- [animation] 76 thigh_open_side_03_move
-- [animation] 77 thigh_open_side_04_rotate
-- [animation] 78 thigh_open_side_05
-- [animation] 79 thigh_open_front
-- [animation] 80 thigh_weapon_slide
-- [animation] 81 weapon_leg
-- [animation] 82 ik_foot_root
-- [animation] 83 ik_foot_l
-- [animation] 84 ik_foot_r
-- [animation] 85 ik_hand_root
-- [animation] 86 ik_hand_gun
-- [animation] 87 ik_hand_l
-- [animation] 88 ik_hand_r

--##########################
--# RoboCop Vr Fix - CJ117 #
--##########################

--########################
--# D_Pad_Method Options #
--########################
-- 0 = Right Thumbrest + Left Joystick
-- 1 = Left Thumbrest + Right Joystick
-- 4 = Gesture (Head) + Left Joystick
-- 5 = Gesture (Head) + Right Joystick

--[[
local D_Pad_Method = 0
local Enable_Scan_Fix = true

--# Do Not Edit Below This Line #

local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks

local Mactive = false
local Playing = false
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
local JustCentered = false
local is_running = false
local is_scanning = false
local weap_loc = nil
local active_weap = nil
local active_weap_name = "None"
local cur_weap = nil
local seq_active = false
local cut_paused = false
local eval_active = false
local panel_active = false
local move_mode = nil
local is_hidden = false
local is_load = false
local detective = false
local is_input = false
local is_view = false
local interact_with = nil
local det_enable = false
local crossmesh = nil
local retmesh = nil
local is_menu = false
local in_gallery = false
local is_end = false
local is_tut = false
local shake_obj = nil
local muzzle_obj = nil
local is_punch = false
local is_logo = false
local on_ladder = false
local is_breach = false
local is_interact = false
local shady_active = false
local is_crosshair_visible = false
local pause_time = 0
local alt_time = 0
local is_false = 0
local last_weapon = nil

local function find_required_object(name)
	local obj = uevr.api:find_uobject(name)
	if not obj then
		return nil
	end

	return obj
end

local rot_mod_c = find_required_object("Class /Script/Game.RotationLimitCameraModifier")
local end_cred_c = find_required_object("WidgetBlueprintGeneratedClass /Game/UI/Popups/Comics/WB_ComicsCard.WB_ComicsCard_C")
local int_logo_c = find_required_object("Class /Script/UMG.UserWidget")
local game_engine_class = find_required_object("Class /Script/Engine.GameEngine")
local melee_mesh_c = find_required_object("Class /Script/Game.PlayerMeleeWeapon")

local function ShadyFix()
	local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

	local viewport = game_engine.GameViewport
	if viewport == nil then

		return
	end

	local world = viewport.World

	local mg_mesh_c = world.OwningGameInstance:get_outer()
	if mg_mesh_c ~= nil then
		local mg_mesh = mg_mesh_c.GameUserSettings

		if mg_mesh ~= nil then
			crossmesh = mg_mesh
			is_crosshair_visible = mg_mesh.bCrosshairVisible
		end
	end	
end

local function RetRem()
	--RetMesh
	local rpawn = api:get_local_pawn(0)
	local ret_mesh = rpawn.FPPHudWidget.CurrentReticle

	if ret_mesh ~= nil and is_crosshair_visible == true then
		ret_mesh.Image_DownLaser.Brush.DrawAs = 0
		ret_mesh.Image_UpLaser.Brush.DrawAs = 0
		ret_mesh.Image_LeftLaser.Brush.DrawAs = 0
		ret_mesh.Image_RightLaser.Brush.DrawAs = 0

		if shady_active == true then
			ret_mesh.Image_CenterDotLaser.Brush.DrawAs = 0
			ret_mesh.Image_CenterSquareLaser.Brush.DrawAs = 0
		end
	end	
end

local function GalleryFix()
	local wpawn = api:get_local_pawn(0)
	local cur_weap = wpawn.Weapon.WeaponMesh

	if rot_mod_c ~= nil then
		local rot_mod = rot_mod_c:get_objects_matching(false)

		for i, mesh in ipairs(rot_mod) do
			if mesh:get_fname():to_string() == "RotationLimitCameraModifier_0" then
				cur_weap:call("SetVisibility", true)
				mesh:DisableModifier(Disable)

				--print(tostring(mesh:get_full_name()))
				break
			end
		end
	end
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
end

local function ResetPlayUI()
	params.vr.set_mod_value("VR_EnableGUI", "true")
	params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
	params.vr.set_mod_value("UI_Distance", "8.500")
	params.vr.set_mod_value("UI_Size", "7.50")
	params.vr.set_mod_value("UI_X_Offset", "0.00")
	params.vr.set_mod_value("UI_Y_Offset", "0.00")
	params.vr.set_mod_value("VR_DPadShiftingMethod", D_Pad_Method)
end

local function Tut_Eval_Check()
	
	local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

	local viewport = game_engine.GameViewport
	if viewport == nil then 
		print("Viewport is nil")
		return
	end
	
	world = viewport.World
	
	if world.AuthorityGameMode ~= nil then
		pause_time = world.GameState.ReplicatedWorldTimeSecondsDouble
		--print(pause_time)
		--print(alt_time)
		if pause_time == alt_time then
			is_false = is_false+1
			if is_false > 30 then
				is_paused = true
				--is_tut = true
				eval_active = true
				--print("Paused")	
				--print("Paused")
				--print(is_false)
			end
		else
			is_false = 0
			is_paused = false
			eval_active = false
			--print("Playing")
		end
	end
end

local function ScaleFix()
	local spawn = api:get_local_pawn(0)
	local sactive_weap = spawn.Weapon
	if Playing == true and sactive_weap ~= nil then

		if spawn ~= nil then
			
			local materials = sactive_weap.WeaponMesh.OverrideMaterials
			local mat_num = #materials
			--print(mat_num)

			for i=1, mat_num do
				
				local d_material = sactive_weap.WeaponMesh:CreateAndSetMaterialInstanceDynamicFromMaterial(i-1, sactive_weap.WeaponMesh.OverrideMaterials[i])
				d_material:SetScalarParameterValue("UsePanini", 0.0)
				--print(i)
				
			end
			
		end
	end
end

local function Melee()
	if melee_mesh_c ~= nil then
		local melee_mesh = melee_mesh_c:get_objects_matching(false)

		for i, mesh in ipairs(melee_mesh) do
			if string.find(mesh:get_fname():to_string(), "WP_MeleeWeapon_C_") then
				if mesh.bHidden == false then
					is_punch = true
					params.vr.set_mod_value("VR_EnableGUI", "true")

					break
				else
					is_punch = false
				end
			else
				is_punch = false
			end
		end
	end	
end

local function EndCredits()
	if end_cred_c ~= nil then
		local end_cred = end_cred_c:get_objects_matching(false)

		for i, mesh in ipairs(end_cred) do
			if string.find(mesh:get_fname():to_string(), "WB_ComicsCard_") and string.find(mesh:get_full_name(), "Transient.GameEngine") then
				is_end = true
				params.vr.set_mod_value("VR_EnableGUI", "true")
				--print(tostring(mesh:get_full_name()))

				break
			else
				is_end = false
			end
		end
	end
end

local function IntroLogo()
	local lpawn = api:get_local_pawn(0)
	if string.find(lpawn:get_full_name(), "L01_PrologueTVstation_ALL") then
		if int_logo_c ~= nil then
			local int_logo = UEVR_UObjectHook.get_objects_by_class(int_logo_c, false)

			for i, mesh in ipairs(int_logo) do
				if string.find(mesh:get_fname():to_string(), "WB_LogoEnter_C") and string.find(mesh:get_full_name(), "Transient.GameEngine") then
					is_logo = true
					params.vr.set_mod_value("VR_EnableGUI", "true")
					--print(tostring(mesh:get_full_name()))

					break
				else
					is_logo = false
				end
			end
		end
	end
end

print("Robocop: Rogue City VR - CJ117")
params.vr.set_mod_value("VR_GhostingFix", "false")
params.vr.set_aim_method(0)
reset_height()
ResetPlayUI()

local pawn = nil
local sactive_mesh = nil

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
	
	local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

	local viewport = game_engine.GameViewport
	if viewport == nil then

		return
	end

	local world = viewport.World

	pawn = api:get_local_pawn(0)
	local pcont = api:get_player_controller(0)

	if pawn ~= nil then
		active_weap = pawn.Weapon
		sactive_mesh = pawn.Mesh
		is_mouse = pcont.bShowMouseCursor
		is_hidden = pawn.bHidden
		is_load = pawn.bNetLoadOnClient
		detective = pawn.DetectiveModeComponent.bIsActive
		is_input = pawn.bInputEnabled
		is_view = pawn.bIsLocalViewTarget
		interact_with = pawn.PawnInteractionComponent.InteractionWith
		is_interact = pawn.PawnInteractionComponent.InteractionTextHidden
		is_menu = world.AuthorityGameMode.bIsInGameMenuShown
		shake_obj = pcont.PlayerCameraManager.CachedCameraShakeMod
		ShadyFix()

		
		if active_weap ~= nil then
			active_weap_name = active_weap:get_full_name()
		end

		if pawn.BreachOverlapActor ~= nil then
			if string.find(pawn.BreachOverlapActor:get_full_name(), "BP_Ladder_C") then
				on_ladder = pawn.BreachOverlapActor.bIsClimbing
			else
				on_ladder = false
			end
			if string.find(pawn.BreachOverlapActor:get_full_name(), "Door")
				or string.find(pawn.BreachOverlapActor:get_full_name(), "Breachable") then
				is_breach = true
			else
				is_breach = false
			end
		end

		if not string.find(pawn:get_full_name(), "Starting") then

			Tut_Eval_Check()

			if string.find(pawn:get_full_name(), "PoliceStation") then
				if is_input == false then
					GalleryFix()
				end
			end

			if interact_with == nil and eval_active == false and is_interact == false then
				if is_input == false and is_view == false then
					params.vr.set_mod_value("VR_EnableGUI", "false")
				elseif is_input == true and is_view == false then
					params.vr.set_mod_value("VR_EnableGUI", "false")
				elseif is_input == true and is_view == true then
					params.vr.set_mod_value("VR_EnableGUI", "true")
					is_tut = false
					eval_active = false
				end
			else
				if is_input == false and is_view == false then
					params.vr.set_mod_value("VR_EnableGUI", "false")
				elseif is_input == true and is_view == false then
					params.vr.set_mod_value("VR_EnableGUI", "true")
				elseif is_input == true and is_view == true then
					params.vr.set_mod_value("VR_EnableGUI", "true")
					is_tut = false
				end
			end

			if is_mouse == true then
				params.vr.set_mod_value("VR_EnableGUI", "true")
			end

			if string.find(pawn:get_full_name(), "L27_OCPHQ_ALL") then
				EndCredits()
			end
		end

		if pawn.Weapon ~= nil and active_weap ~= nil then
			if not string.find(active_weap:get_full_name(), "NoWeapon") then
				cur_weap = pawn.Weapon.WeaponMesh
				if cur_weap == last_weapon then 
				
				else
					local u_exist = UEVR_UObjectHook.exists(last_weapon)
					if u_exist == true then
						weap_loc = UEVR_UObjectHook.remove_motion_controller_state(last_weapon)
					end	
					ScaleFix()
				end
			end
		end
	end

	if pawn == nil or string.find(pawn:get_full_name(), "Starting") or seq_active == true or is_mouse == true or eval_active == true or is_hidden == true or is_load == false or detective == true or is_input == false or is_view == false or is_menu == true or on_ladder == true or is_tut == true then
		if Mactive == false then
			print("InCut")
			Mactive = true
			Playing = false
			mDB = false
			params.vr.set_mod_value("VR_GhostingFix", "false")
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
			UEVR_UObjectHook.set_disabled(true)
			params.vr.set_aim_method(0)
		end
		
		IntroLogo()
		if cut_paused == true or string.find(pawn:get_full_name(), "Starting") or eval_active == true or panel_active == true or is_mouse == true or detective == true or is_input == true or is_menu == true or is_end == true or is_tut == true or is_logo == true and seq_active == false then
			if string.find(pawn:get_full_name(), "Starting") or is_mouse == true or eval_active == true or is_end == true or is_tut == true then
				if interact_with == nil then
					UEVR_UObjectHook.set_disabled(false)
					params.vr.set_mod_value("UI_FollowView", "false")
					params.vr.set_mod_value("UI_Distance", "8.500")
					params.vr.set_mod_value("UI_Size", "7.50")
				else
					if is_menu == false and is_end == false then
						params.vr.set_mod_value("VR_DPadShiftingMethod", D_Pad_Method)
						params.vr.set_mod_value("UI_Distance", "0.500")
						params.vr.set_mod_value("UI_Size", "0.70")
					else
						params.vr.set_mod_value("UI_FollowView", "false")
						params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
						params.vr.set_mod_value("UI_Distance", "8.500")
						params.vr.set_mod_value("UI_Size", "7.50")
					end
				end
			else
				params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
				if is_logo == true or eval_active == true and interact_with == nil then
					params.vr.set_mod_value("UI_Distance", "8.500")
					params.vr.set_mod_value("UI_Size", "7.50")
				else
					params.vr.set_mod_value("UI_Distance", "0.500")
					params.vr.set_mod_value("UI_Size", "0.70")
				end
			end
		end

		if is_view == false or is_tut == true then
			params.vr.set_mod_value("VR_DPadShiftingMethod", "2")
		end

		if is_input == false and is_breach == true then
			pawn.Mesh:call("SetRenderInMainPass", true)
		end
		
		if eval_active == true then
			params.vr.set_mod_value("VR_EnableGUI", "true")
		end
	else
		if Playing == false then
			print("Playing")
			Mactive = false
			Playing = true
			mDB = false
			
			ResetPlayUI()
			params.vr.set_mod_value("VR_GhostingFix", "true")
			UEVR_UObjectHook.set_disabled(false)
			params.vr.set_aim_method(2)
		end

		Melee()

		if string.find(pawn:get_full_name(), "L15_ShadyMeeting") then
			params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			params.vr.set_mod_value("UI_FollowView", "true")
			ShadyFix()
			shady_active = true
		else
			shady_active = false
			if is_crosshair_visible == true then
				crossmesh.bCrosshairVisible = true
			end
			params.vr.set_mod_value("UI_FollowView", "false")
		end

		if active_weap ~= nil and cur_weap ~= nil and not string.find(active_weap:get_full_name(), "NoWeapon") then
			weap_loc = UEVR_UObjectHook.get_or_add_motion_controller_state(cur_weap)
			
		 	RetRem()

		 	if Playing == true then
		 		weap_loc:set_hand(1)
		 		weap_loc:set_permanent(true)
		 		
				last_weapon = cur_weap			
		 		if string.find(cur_weap:get_full_name(), "BerettaAuto") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-2.500, -3.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "SmgUZI") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-2.500, -3.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "SigSauer") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-2.500, -3.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "AKM") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-7.500, -7.500, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "Mossberg") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(0.000, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "HK21") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-7.00, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "BrowningM60") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-7.00, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "DesertEagle") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-2.500, -3.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "ATGM") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "IntraTec") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "SteyrAUG") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "HKG11") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "Spas12") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-2.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "MM1GL") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "Sniper") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-3.500, -5.000, 0.000))
		 		elseif string.find(cur_weap:get_full_name(), "BarrettM82") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(-7.00, -5.000, 0.000))
				elseif string.find(cur_weap:get_full_name(), "SterlingMk6") then
		 			weap_loc:set_rotation_offset(Vector3f.new(-0.00, 1.575, -0.000))
		 			weap_loc:set_location_offset(Vector3f.new(0.00, -5.00, 0.000))	
		 		end
		 	end
			
			if eval_active == true then
				params.vr.set_aim_method(0)
			else
				params.vr.set_aim_method(2)
			end

		 	if Playing == true and not string.find(active_weap:get_full_name(), "NoWeapon") then
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

		 	if is_punch == true then
		 		pawn.Mesh:call("SetRenderInMainPass", true)
		 	end
		 end
	end

	alt_time = world.GameState.ReplicatedWorldTimeSecondsDouble
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

		if Playing == true and in_gallery == true then
			if state.Gamepad.bLeftTrigger ~= 0 then
				state.Gamepad.bLeftTrigger = 0
			end
		end



		if Playing == true and mAttack == true then
			mAttack = false
			state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_RIGHT_SHOULDER
		end


		if Playing == true and active_weap ~= nil then
			if state.Gamepad.bRightTrigger ~= 0 then
				shake_obj:DisableModifier(Disable)
			end
		end

		if pawn ~= nil and Enable_Scan_Fix == true then
			if active_weap == nil or string.find(active_weap_name, "NoWeapon") then
				if Playing == true then
					if state.Gamepad.bLeftTrigger ~= 0 then
						if det_enable == false then
							det_enable = true
							params.vr.set_mod_value("VR_RenderingMethod", "1")
						end
					else
						local cur_meth = params.vr:get_mod_value("VR_RenderingMethod")
						det_enable = false
						if cur_meth ~= 0 then
							params.vr.set_mod_value("VR_RenderingMethod", "0")
						end
					end
				end
			end
		end
	end
end)
]]--
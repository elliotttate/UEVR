local uevrUtils = require("libs/uevr_utils")
uevrUtils.setLogLevel(LogLevel.Debug)
uevrUtils.initUEVR(uevr)
local flickerFixer = require("libs/flicker_fixer")
local controllers = require("libs/controllers")
local animation = require("libs/animation")
local hands = require("libs/hands")
local weapons = require("addons/weapons")
local handAnimations = require("addons/hand_animations")

local handedness = Handed.Right

local handParams = 
{
	Arms = 
	{
		Left = 
		{
			Name = "lowerarm_l",
			Rotation = {0, 90, -90},
			Location = {2.8, -40.7, -1.5},	
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
local wasInCinematic = false

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
	-- uevr.params.vr.set_mod_value("UI_FollowView", val and "false" or "true")
	-- uevr.params.vr.set_aim_method(val and 0 or 2)
end

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
	if string.find(levelName, "MAP_StartingMap") then
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
	weapons.reset()
	isInCinematic = false
	wasInCinematic = false
	disableSnapTurn(false)
	handleLevelChange(level)

	delay(2000, function()
		canCreateHands = true
	end)
end

function on_weapon_change(activeWeapon)
	uevrUtils.print("on_weapon_change called " .. (activeWeapon == nil and " with no weapon" or " with active weapon"))
	local handStr = handedness == Handed.Right and "right" or "left"
	if activeWeapon ~= nil then
		animation.updateAnimation(handStr.."_hand", handStr.."_grip_weapon", false)
		animation.updateAnimation(handStr.."_hand", handStr.."_grip_weapon", true)
	else
		animation.updateAnimation(handStr.."_hand", handStr.."_grip", false)
	end
end

function on_lazy_poll()
	if canCreateHands and not hands.exists() then
		hands.create(pawn.Mesh, handParams, handAnimations)
		animation.setBoneSpaceLocalRotator(hands.getHandComponent(Handed.Right), "hand_r", uevrUtils.rotator(0, 0, -90))
		animation.setBoneSpaceLocalRotator(hands.getHandComponent(Handed.Left), "hand_l", uevrUtils.rotator(0, 0, -90))
		uevrUtils.fixMeshFOV(hands.getHandComponent(Handed.Left), "UsePanini", 0.0, true, true, false)
		uevrUtils.fixMeshFOV(hands.getHandComponent(Handed.Right), "UsePanini", 0.0, true, true, false)
		if isInCinematic then
			hands.hideHands(true)
		end
		
		uevrUtils.fixMeshFOV(pawn.Mesh, "UsePanini", 0.0, true, true, false)
		
		local weaponMesh = uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"})
		if weaponMesh ~= nil then uevrUtils.fixMeshFOV(weaponMesh, "UsePanini", 0.0, true, true, false) end
	end
	
end

function on_xinput_get_state(retval, user_index, state)
	if hands.exists() then
		hands.handleInput(state, weapons.getActiveWeapon() ~= nil, handedness, true)
	end
end

function on_post_engine_tick(engine, delta)	
	if uevrUtils.getValid(pawn) ~= nil then
		is_input = pawn.bInputEnabled
		is_view = pawn.bIsLocalViewTarget
		
		if is_view and is_input then
			isInCinematic = false
		else
			isInCinematic = true
		end
	end
	
	updateViewState()
	weapons.attachWeapon(uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"}), handedness)

end

register_key_bind("F1", function()
    print("F1 pressed\n")
	weapons.enableAdjustments()
end)
register_key_bind("F2", function()
    print("F2 pressed\n")
	--animation.logDescendantBoneTransforms(hands.getHandComponent(Handed.Right), "lowerarm_r", true, true, false)
end)

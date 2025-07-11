--###################################
--# Alices Lullaby Menu Fix - CJ117 #
--# Contributions by BudWheizzah    #
--# Added Cutscene Detection        #
--# Added Cutscene lock             #
--# Long LT press for lock/recenter #
--# Some code improvements          #
--###################################

local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks

local LongpressDelay = 0.8
local ThumbstickThreshold = 25000
local TriggerThreshold = 128

local CutsceneLock = false
local PlayState = 0
local PlayerControllerState = 0
local PlayerCount = 0

local justPressedCombo = false
local comboPressed = 0
local longpressHeld = false
local countLongpress = 0

local game_engine_class = api:find_uobject("Class /Script/Engine.GameEngine")
local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)
local game_instance = nil

local player = nil
local player_controller = nil

local function reset_height(recenter)

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

	if recenter == true then
		params.vr.recenter_view()
	end

end

local function toggle_roomscale(tog)

	if tog == true then

		params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
		UEVR_UObjectHook.set_disabled(false)
		params.vr.set_aim_method(2)
		-- Enabling RS movement allows player to move physically while keeping a correct rotation center
		params.vr.set_mod_value("VR_RoomscaleMovement","true");
		CutsceneLock = false

	else

		params.vr.set_mod_value("VR_DecoupledPitchUIAdjust", "false")
		UEVR_UObjectHook.set_disabled(true)
		params.vr.set_aim_method(0)
		-- Ironically, disabling RS movement allows RS movement in cutscenes
		params.vr.set_mod_value("VR_RoomscaleMovement","false");
		CutsceneLock = true

	end

end

params.vr.set_aim_method(0)
reset_height(false)

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)

	if longpressHeld == true then
		countLongpress = countLongpress + delta
	else
		countLongpress = 0
	end

	local pawn = api:get_local_pawn(0)

	if pawn == nil or string.find(pawn:get_full_name(), "MainMenu") then

		-- Title screen tick actions
		if PlayState ~= 1 then

			-- Disable hooks and set PlayState to 1
			params.functions.log_info("[HelperScript] Menu detected! PlayState 1, PlayerControllerState 0.")
			PlayState = 1
			toggle_roomscale(false)

		end

		PlayerControllerState = 0
		-- End title screen tick actions

	else

		-- In-game tick actions

		-- Fetch player controller and player pawn for cutscene detection
		game_instance = game_engine.GameInstance
		if game_instance ~= nil then
			PlayerCount = #game_instance.LocalPlayers
			if PlayerCount > 0 then
				player = game_instance.LocalPlayers[1]
			else
				params.functions.log_error("[HelperScript] Zero players found! Critical! Skipping tick")
				return
			end
		else
			params.functions.log_error("[HelperScript] Game instance is null! Critical! Skipping tick")
			return
		end

		if player~=nil then

			player_controller = player.PlayerController

			if PlayerControllerState ~= 1 then
				params.functions.log_info("[HelperScript] We found the player object! PlayerControllerState 1.")
				if player_controller == nil then
					params.functions.log_error("[HelperScript] Player controller is Null!")
				end
			end 

			PlayerControllerState = 1 --checked and not null

		else

			player_controller = nil
			if PlayerControllerState ~= 2 then
				params.functions.log_error("[HelperScript] Player instance is null! PlayerControllerState 2. Count is " .. #game_instance.LocalPlayers)
			end 

			PlayerControllerState = 2 --checked and null

		end

		-- can move by default
		local playerIsBlocked = false

		if player_controller ~= nil then
			-- player_controller:IsLookInputIgnored() Seems like the right call, because notes allow rotation, cutscenes don't
			playerIsBlocked = player_controller:IsLookInputIgnored()
		end

		if playerIsBlocked == true and PlayState ~= 3 then

				params.functions.log_info("[HelperScript] Cutscene detected! PlayState 3")
				PlayState = 3

				toggle_roomscale(false)

		elseif playerIsBlocked == false and PlayState ~= 2 then

				params.functions.log_info("[HelperScript] Gameplay detected! PlayState 2")

				-- Weird error checks
				if player_controller == nil then
					params.functions.log_error("[HelperScript] Player controller is Null still in state 2, not normal!")
				end

				PlayState = 2
				toggle_roomscale(true)

		end
		-- End in-game tick actions

	end

end)

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	--0 none, 1 long press LT, 2 LT + RS up(?)
	comboPressed = 0

	--register combo press globally for all states
	if state.Gamepad.bLeftTrigger > TriggerThreshold then
		longpressHeld = true

		--long press timer
		if countLongpress > LongpressDelay then
			comboPressed = 1 -- room scale toggle 
		elseif state.Gamepad.sThumbRY > ThumbstickThreshold then
			comboPressed = 2 -- recenter
		end
	else
		longpressHeld = false
	end

	--For play state 1 (menu) perform re-center on combo press
	if state ~= nil and comboPressed > 0 and justPressedCombo == false then
		if comboPressed == 1 then

			if PlayState == 1 then

				-- Long press Left Trigger (Title screen only)
				params.functions.log_info("[HelperScript] Perform re-center by long press")
				reset_height(true)

			elseif PlayState >= 2 then

				-- Long press Left Trigger room scale toggle
				params.functions.log_info("[HelperScript] Toggle cutscene lock by long press")
				if CutsceneLock == false then
					params.functions.log_info("[HelperScript] Enable cutscene lock!")
					toggle_roomscale(false)
					reset_height(true)
				else
					params.functions.log_info("[HelperScript] Disable cutscene lock!")
					toggle_roomscale(true)
				end
	
			end

		elseif comboPressed == 2 then

			-- Quick press Left Trigger + Right Stick Up recenter/reset standing
			params.functions.log_info("[HelperScript] Perform re-center by binding")
			reset_height(true)

		end

		justPressedCombo = true
		countLongpress = 0

	end

	--reset justPressedCombo only when combo is released
	if justPressedCombo == true and comboPressed == 0 and longpressHeld == false then

		justPressedCombo = false
		countLongpress = 0

	end

end)

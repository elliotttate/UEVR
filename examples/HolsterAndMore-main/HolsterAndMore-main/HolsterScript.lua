--I tried adding some easy way to edit this. first of all the config part for some quick settings. 
--For editing zones and what they do you have to go to Lines:



--CONFIG--
--------	
	local isRhand = true							--Right hand or left hand inputs	
	local isLeftHandModeTriggerSwitchOnly = true    --If left hand input, only swaps triggers
	local HapticFeedback = true                     --Haptic feedback when entering zones
	local PhysicalLeaning = false                   --automatically leans when leaning IRL, ONLY WORKS IF YOU ADD LEAN FUNCTIONS OR KEYS
	local disableDpad = false						--Disables most Dpad functions except up(can be configured further down)
	local disableButtonsWhenPlaying =false			--Disables buttons that are replaced by gestures, ONLY WORKS IF YOU SET UP THE GESTURES ALREADY OF COURSE
--------
--------	
	local api = uevr.api
	
	local params = uevr.params
	local callbacks = params.sdk.callbacks
	local pawn = api:get_local_pawn(0)
	local vr=uevr.params.vr
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


local lControllerIndex= 1
local rControllerIndex= 2


	local game_engine_class = find_required_object("Class /Script/Engine.GameEngine")
	local actor_c = find_required_object("Class /Script/Engine.Actor")
	local motion_controller_component_c = find_required_object("Class /Script/HeadMountedDisplay.MotionControllerComponent")
	local scene_component_c = find_required_object("Class /Script/Engine.SceneComponent")
	
	local hmd_actor = nil -- The purpose of the HMD actor is to accurately track the HMD's world transform
	local left_hand_actor = nil
	local right_hand_actor = nil
	local left_hand_component = nil
	local right_hand_component = nil
	local hmd_component = nil
	local last_level = nil
	
	local ftransform_c = find_required_object("ScriptStruct /Script/CoreUObject.Transform")
	local temp_transform = StructObject.new(ftransform_c)
	
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
		right_hand_component = nil
		left_hand_component = nil
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
	end
	local counting=false
	local tickCount=0
	
	uevr.sdk.callbacks.on_pre_engine_tick(function(engine_voidptr, delta)
		local engine = game_engine_class:get_first_object_matching(false)
		if not engine then
			return
		end
	
		local viewport = engine.GameViewport
	
		if viewport then
			local world = viewport.World
	
			if world then
				local level = world.PersistentLevel
	
				if last_level ~= level then
					on_level_changed(level)
					reset_hand_actors()
					counting=true
				end
	
				last_level = level
			end
		end
		if counting==true then 
			tickCount=tickCount+1
			if tickCount>=1000 then
				State= 3
				counting=false
				tickCount=0
				spawn_hand_actors()
			end
		end
	--	print(tickCount)
		reset_hand_actors_if_deleted()
	
		if left_hand_actor == nil or right_hand_actor == nil then
			spawn_hand_actors()
		end
	end)
	
	-- Use Vector3d if this is a UE5 game (double precision)
	local last_rot = Vector3f.new(0, 0, 0)
	local last_pos = Vector3f.new(0, 0, 0)
	

	uevr.sdk.callbacks.on_script_reset(function()
		print("Resetting")
	
		reset_hand_actors()
	end)


--------------
--------------
--------------
--------------CODE TO EDIT STARTS HERE_-_--__---__
--------------CODE TO EDIT STARTS HERE_-_--__---__
--------------CODE TO EDIT STARTS HERE_-_--__---__
--------------
--------------
--------------




--USEABLE FUNCTIONS:

--BUTTON HANDLING: example: pressButton(state, XINPUT_GAMEPAD_A) --Sends call to press A
function isButtonPressed(state, button)
	return state.Gamepad.wButtons & button ~= 0
end
function isButtonNotPressed(state, button)
	return state.Gamepad.wButtons & button == 0
end
function pressButton(state, button)
	state.Gamepad.wButtons = state.Gamepad.wButtons | button
end
function unpressButton(state, button)
	state.Gamepad.wButtons = state.Gamepad.wButtons & ~(button)
end

--SENDS call to Press and Unpress Keyboard keys in conjunctino with luakey.dll, accepts virtual key window code, e.g. 0x01
-- EXAMPLE: SendKeyDown('K') or SendKeyDown('0x4B') --presses the K key, still needs an unpress condition somewhere for best results
--VR to key functions
local function SendKeyPress(key_value, key_up)
    local key_up_string = "down"
    if key_up == true then 
        key_up_string = "up"
    end
    
    api:dispatch_custom_event(key_value, key_up_string)
end

local function SendKeyDown(key_value)
    SendKeyPress(key_value, false)
end

local function SendKeyUp(key_value)
    SendKeyPress(key_value, true)
end


--NEEDED BASIC VARIABLES
local rGrabActive =false
local lGrabActive =false
local LZone=0
local ThumbLX   = 0
local ThumbLY   = 0
local ThumbRX   = 0
local ThumbRY   = 0
local LTrigger  = 0
local RTrigger  = 0
local rShoulder = false
local lShoulder = false
local lThumb    = false
local rThumb    = false
local lThumbSwitchState= 0
local lThumbOut= false
local rThumbSwitchState= 0
local rThumbOut= false
local isReloading= false
local ReadyUpTick = 0
local RZone=0
local LWeaponZone=0
local RWeaponZone=0
local inMenu=false
local LTriggerWasPressed = 0
local RTriggerWasPressed = 0
--variables for zones
local RHandLocation=Vector3f.new (0,0,0) 
local LHandLocation=Vector3f.new (0,0,0)
local HmdLocation=Vector3f.new (0,0,0)
local isHapticZoneR = false
local isHapticZoneL = false
local isHapticZoneWR = false
local isHapticZoneWL = false
local isHapticZoneRLast= false
local isHapticZoneWRLast= false
local isHapticZoneWLLast= false
local LeftController=uevr.params.vr.get_left_joystick_source()
local RightController= uevr.params.vr.get_right_joystick_source()
local leanState=0 --1 =left, 2=right


--ADD YOUR OWN VARIABLES HERE



--



uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--Read Gamepad stick input 
	ThumbLX = state.Gamepad.sThumbLX
	ThumbLY = state.Gamepad.sThumbLY
	ThumbRX = state.Gamepad.sThumbRX
	ThumbRY = state.Gamepad.sThumbRY
	LTrigger= state.Gamepad.bLeftTrigger
	RTrigger= state.Gamepad.bRightTrigger
	rShoulder= isButtonPressed(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
	lShoulder= isButtonPressed(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
	lThumb   = isButtonPressed(state, XINPUT_GAMEPAD_LEFT_THUMB)
	rThumb   = isButtonPressed(state, XINPUT_GAMEPAD_RIGHT_THUMB)
	Abutton  = isButtonPressed(state, XINPUT_GAMEPAD_A)
	Bbutton  = isButtonPressed(state, XINPUT_GAMEPAD_B)
	Xbutton  = isButtonPressed(state, XINPUT_GAMEPAD_X)
	Ybutton  = isButtonPressed(state, XINPUT_GAMEPAD_Y)
	--inMenu = api:get_player_controller().bShowMouseCursor
	
--Resets TriggerWasPressed to 0, this is only a dummy variable if you need it	
	if  LTrigger<10 then
		LTriggerWasPressed = 0
	end
	if  RTrigger<10 then
		RTriggerWasPressed = 0
	end

--disables Dpad functions,e.g. if they are annoying and are already replaced by other gestures	
	if disableDpad then	
		if isRhand then
			if not rShoulder then
				unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT		)
				--unpressButton(state, XINPUT_GAMEPAD_DPAD_UP			)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN	    )
			end
		else 
			if not lShoulder then
				unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT		)
				--unpressButton(state, XINPUT_GAMEPAD_DPAD_UP			)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN	    )
			end
		end
	end
--
--Left hand config:	
	
	if not isRhand then
--Triggers only
		state.Gamepad.bLeftTrigger=RTrigger
		state.Gamepad.bRightTrigger=LTrigger
--ALl buttons		
		if not isLeftHandModeTriggerSwitchOnly then
			state.Gamepad.sThumbRX=ThumbLX
			state.Gamepad.sThumbRY=ThumbLY
			state.Gamepad.sThumbLX=ThumbRX
			state.Gamepad.sThumbLY=ThumbRY
			
			unpressButton(state, XINPUT_GAMEPAD_B)
			unpressButton(state, XINPUT_GAMEPAD_A				)
			unpressButton(state, XINPUT_GAMEPAD_X				)	
			unpressButton(state, XINPUT_GAMEPAD_Y				)
			----unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT		)
			----unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT		)
			----unpressButton(state, XINPUT_GAMEPAD_DPAD_UP			)
			----unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN	    )
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER	)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER	)
			unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB		)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB		)
			if Ybutton then
				pressButton(state,XINPUT_GAMEPAD_X)
			end
			if Bbutton then
			--	unpressButton(state, XINPUT_GAMEPAD_B)	
				pressButton(state,XINPUT_GAMEPAD_A)
			end
			if Xbutton then
				pressButton(state,XINPUT_GAMEPAD_Y)
				--unpressButton(state, XINPUT_GAMEPAD_X)
			end	
			if Abutton then
				pressButton(state,XINPUT_GAMEPAD_B)
				--unpressButton(state, XINPUT_GAMEPAD_A)
			end		
			
			if lShoulder then
				pressButton(state,XINPUT_GAMEPAD_RIGHT_SHOULDER)
		--		unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
			end
			if rShoulder then
				pressButton(state,XINPUT_GAMEPAD_LEFT_SHOULDER)
			--	unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
			end
			if lThumb then
				pressButton(state,XINPUT_GAMEPAD_RIGHT_THUMB)
--				unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
			end	
			if rThumb then
				pressButton(state,XINPUT_GAMEPAD_LEFT_THUMB)
		--		unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB)
			end
		end
		
	end
	
--disable further buttons if not in menu:	YOU NEED TO PROPERLY CHECK IF "inMenu" works
--this is just an example of how inMenu can work:	
	--inMenu = api:get_player_controller().bShowMouseCursor
	if disableButtonsWhenPlaying then
	inMenu = api:get_player_controller().bShowMouseCursor
		if not inMenu then
			unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER	)		
			unpressButton(state, XINPUT_GAMEPAD_B)
			--unpressButton(state, XINPUT_GAMEPAD_A				)
			unpressButton(state, XINPUT_GAMEPAD_X				)	
			unpressButton(state, XINPUT_GAMEPAD_Y				)
		end
	end
	
	--Unpress when in Zone
	if isRhand then	
		if  RZone ~=0 then
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB)
		end
	else
		if LZone ~= 0 then
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB)
		end
	end
	
	
	--disable Trigger for modeswitch
	if RWeaponZone == 2 then
		state.Gamepad.bLeftTrigger=0
	end
	
	-- Attachement singlepress fix
	if lThumb and lThumbSwitchState==0 then 
		lThumbOut = true 
		lThumbSwitchState=1
	elseif lThumb and lThumbSwitchState ==1 then
		lThumbOut = false
	elseif not lThumb then
		lThumbOut = false
		lThumbSwitchState=0
	end
	if rThumb and rThumbSwitchState==0 then 
		rThumbOut = true 
		rThumbSwitchState=1
	elseif rThumb and rThumbSwitchState ==1 then
		rThumbOut = false
	elseif not rThumb then
		rThumbOut = false
		rThumbSwitchState=0
	end
	
	
	
	--HOW RELOADING IS TRIGGERED:
	if isReloading then
		pressButton(state, XINPUT_GAMEPAD_X)     --works in most games or replace by keyboard key "R"
	end
	
	
		
	--Grab activation
	if rShoulder then
		rGrabActive= true
	else rGrabActive =false
	end
	if lShoulder  then
		lGrabActive =true
		--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
	else lGrabActive=false
	end
	
	if isRhand then
		
		if LZone==9 and LTrigger>=230 then
			pressButton(state, XINPUT_GAMEPAD_Y)
		end
		
	
	
		if LZone == 5 and lGrabActive then
			pressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
		end
	
	end

--YOUR CODE is best applied here to overwrite any prior control settings:


--

	
end)



	


uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
	pawn=api:get_local_pawn(0)

	RHandLocation=right_hand_component:K2_GetComponentLocation()
	LHandLocation=left_hand_component:K2_GetComponentLocation()
	HmdLocation= hmd_component:K2_GetComponentLocation()

	local HmdRotation= hmd_component:K2_GetComponentRotation()
	local RHandRotation = right_hand_component:K2_GetComponentRotation()
	local LHandRotation = left_hand_component:K2_GetComponentRotation()


	--LEANING: YOU NEED TO FIND PROPER LEAN FUNCTIONS and replace the pawn:ToggleLean parts
	if PhysicalLeaning then
	
		if HmdRotation.z > 20 then
			leanState = 2
			--pawn:ToggleLeanRight(true)
		elseif HmdRotation.z <20 and HmdRotation.z>-20 then
			leanState=0
			--pawn:ToggleLeanRight(false) 
			--pawn:ToggleLeanLeft(false)
		elseif HmdRotation.z < -20 then 
			leanState=1
			--pawn:ToggleLeanLeft(true)
		end
		
		if leanState == 0 and leanStateLast ~= leanState then
			if leanStateLast == 1 then
				--pawn:ToggleLeanLeft(false)
			elseif leanStateLast ==2 then
				--pawn:ToggleLeanRight(false)
			end
			leanStateLast=leanState
			uevr.params.vr.set_mod_value("VR_RoomscaleMovement", "true")
		elseif leanState ==1 and leanStateLast ~= leanState then
			--pawn:ToggleLeanLeft(true)
			leanStateLast=leanState
			uevr.params.vr.set_mod_value("VR_RoomscaleMovement", "false")
		elseif leanState == 2 and leanStateLast ~= leanState then
			--pawn:ToggleLeanRight(true)
			leanStateLast=leanState
			uevr.params.vr.set_mod_value("VR_RoomscaleMovement", "false")
		end
	
	end	
	
	--ZONE MATH: DO NOT EDIT THIS
	-----------------------------
	-- Y IS LEFT RIGHT, X IS BACK FORWARD, Z IS DOWN  UP
	local RotDiff= HmdRotation.y	--(z axis of location)
	local LHandNewX= (LHandLocation.x-HmdLocation.x)*math.cos(-RotDiff/180*math.pi)- (LHandLocation.y-HmdLocation.y)*math.sin(-RotDiff/180*math.pi)
			
	local LHandNewY= (LHandLocation.x-HmdLocation.x)*math.sin(-RotDiff/180*math.pi) + (LHandLocation.y-HmdLocation.y)*math.cos(-RotDiff/180*math.pi)
	
	local RHandNewX= (RHandLocation.x-HmdLocation.x)*math.cos(-RotDiff/180*math.pi)- (RHandLocation.y-HmdLocation.y)*math.sin(-RotDiff/180*math.pi)
		  
	local RHandNewY= (RHandLocation.x-HmdLocation.x)*math.sin(-RotDiff/180*math.pi) + (RHandLocation.y-HmdLocation.y)*math.cos(-RotDiff/180*math.pi)
	
	local RHandNewZ= RHandLocation.z-HmdLocation.z
	local LHandNewZ= LHandLocation.z-HmdLocation.z
	
	--for R Handed 
	--z,yaw Rotation
	local RotWeaponZ= RHandRotation.y
	local LHandWeaponX = (LHandLocation.x-RHandLocation.x)*math.cos(-RotWeaponZ/180*math.pi)- (LHandLocation.y-RHandLocation.y)*math.sin(-RotWeaponZ/180*math.pi)
	local LHandWeaponY = (LHandLocation.x-RHandLocation.x)*math.sin(-RotWeaponZ/180*math.pi) + (LHandLocation.y-RHandLocation.y)*math.cos(-RotWeaponZ/180*math.pi)
	local LHandWeaponZ = (LHandLocation.z-RHandLocation.z)
	--print(RHandRotation.z)
	-- x, Roll Rotation
	local RotWeaponX =RHandRotation.z
	LHandWeaponY = LHandWeaponY*math.cos(RotWeaponX/180*math.pi)- LHandWeaponZ*math.sin (RotWeaponX/180*math.pi)
	LHandWeaponZ = LHandWeaponY*math.sin(RotWeaponX/180*math.pi) + LHandWeaponZ*math.cos(RotWeaponX/180*math.pi)
	-- y, Pitch Rotation
	local RotWeaponY =RHandRotation.x
	LHandWeaponX = LHandWeaponX*math.cos(-RotWeaponY/180*math.pi)- LHandWeaponZ*math.sin(-RotWeaponY/180*math.pi)
	LHandWeaponZ = LHandWeaponX*math.sin(-RotWeaponY/180*math.pi) + LHandWeaponZ*math.cos(-RotWeaponY/180*math.pi)
	
	-- 3d Rotation Complete
	--print(RotWeaponX)
	--print(RotWeaponY)
	--for LEFT
	local RotWeaponLZ= LHandRotation.y
	local RHandWeaponX = (RHandLocation.x-LHandLocation.x)*math.cos(-RotWeaponLZ/180*math.pi)- 	(RHandLocation.y-LHandLocation.y)*math.sin(-RotWeaponLZ/180*math.pi)
	local RHandWeaponY = (RHandLocation.x-LHandLocation.x)*math.sin(-RotWeaponLZ/180*math.pi) + (RHandLocation.y-LHandLocation.y)*math.cos(-RotWeaponLZ/180*math.pi)
	local RHandWeaponZ = (RHandLocation.z-LHandLocation.z)
		
	local RotWeaponLX =LHandRotation.z
	RHandWeaponY = RHandWeaponY*math.cos(RotWeaponLX/180*math.pi)-  RHandWeaponZ*math.sin (RotWeaponLX/180*math.pi)
	RHandWeaponZ = RHandWeaponY*math.sin(RotWeaponLX/180*math.pi) + RHandWeaponZ*math.cos (RotWeaponLX/180*math.pi)
	
	local RotWeaponLY =LHandRotation.x
	RHandWeaponX = RHandWeaponX*math.cos(-RotWeaponLY/180*math.pi)-  RHandWeaponZ*math.sin(-RotWeaponLY/180*math.pi)
	RHandWeaponZ = RHandWeaponX*math.sin(-RotWeaponLY/180*math.pi) + RHandWeaponZ*math.cos(-RotWeaponLY/180*math.pi)
	
	--END OF ZONE MATH-------------------------
	-------------------------------------------
	
	
	--small force feedback on enter and leave, you may edit values
	if HapticFeedback then	
		if isHapticZoneRLast ~= isHapticZoneR  then
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.1, 1.0, 100.0, RightController)
			isHapticZoneRLast=isHapticZoneR
		end
		if isHapticZoneLLast ~= isHapticZoneL then
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.1, 1.0, 100.0, LeftController)
			isHapticZoneLLast=isHapticZoneL
		end
		if isHapticZoneWRLast ~= isHapticZoneWR  then
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.1, 1.0, 100.0, RightController)
			isHapticZoneWRLast=isHapticZoneWR
		end
		if isHapticZoneWLLast ~= isHapticZoneWL then
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.1, 1.0, 100.0, LeftController)
			isHapticZoneWLLast=isHapticZoneWL
		end
	end
	
	--FUNCTION FOR ZONES, dont edit this
local function RCheckZone(Zmin,Zmax,Ymin,Ymax,Xmin,Xmax) -- Z: UP/DOWN, Y:RIGHT LEFT, X FORWARD BACKWARD, checks if RHand is in RZone
	if RHandNewZ > Zmin and RHandNewZ < Zmax and RHandNewY > Ymin and RHandNewY < Ymax and RHandNewX > Xmin and RHandNewX < Xmax then
		return true
	else 
		return false
	end
end
local function LCheckZone(Zmin,Zmax,Ymin,Ymax,Xmin,Xmax) -- Z: UP/DOWN, Y:RIGHT LEFT, X FORWARD BACKWARD, checks if LHand is in LZone
	if LHandNewZ > Zmin and LHandNewZ < Zmax and LHandNewY > Ymin and LHandNewY < Ymax and LHandNewX > Xmin and LHandNewX < Xmax then
		return true
	else 
		return false
	end
end
	
	-----EDIT HERE-------------
	---------------------------
	--define Haptic zones RHand (Zmin,Zmax,Ymin,Ymax,Xmin,Xmax)  Z: UP/DOWN, Y:RIGHT LEFT, X FORWARD BACKWARD,
	if 	   RCheckZone(-10, 15, 10, 30, -5, 20) then 
		isHapticZoneR =true
		RZone=1-- RShoulder
	elseif RCheckZone(-10, 15, -30, -5, -5, 20)      then
		isHapticZoneR =true
		RZone=2--Left Shoulder
	elseif RCheckZone(0, 20, -5, 5, 0, 20)  then
		isHapticZoneR= true
		RZone=3-- Over Head
	elseif RCheckZone(-100,-60,22,50,-10,10)   then
		isHapticZoneR= true
		RZone=4--RHip
	elseif RCheckZone(-100,-60,-50,-10,-10,10)   then
		isHapticZoneR= true
		RZone=5--LHip
	elseif RCheckZone(-40,-25,-15,-5,0,10)   then
		isHapticZoneR= true
		RZone=6--ChestLeft
	elseif RCheckZone(-40,-25,5,15,0,10)  then
		isHapticZoneR= true
		RZone=7--ChestRight
	elseif RCheckZone(-100,-50,-20,20,-30,-15)	  then
		isHapticZoneR= true
		RZone=8--LowerBack Center
	elseif RCheckZone(-5,10,-10,0,0,10) then
		isHapticZoneR= true
		RZone=9--LeftEar
	elseif RCheckZone(-5,10,0,10,0,10)  then
		isHapticZoneR= true
		RZone=10--RightEar
	else 
		isHapticZoneR= false
		RZone=0--EMPTY
	end
	--define Haptic zone Lhandx
	if LCheckZone(-10, 15, 5, 30, -5, 20) then
		isHapticZoneL =true
		LZone=1-- RShoulder
	elseif LCheckZone (-10, 15, -30, -10, -5, 20) then
		isHapticZoneL =true
		LZone=2--Left Shoulder
	elseif LCheckZone(0, 20, -5, 5, 0, 20) then
		isHapticZoneL= true
		LZone=3-- Over Head
	elseif LCheckZone(-100,-60,22,50,-10,10)  then
		isHapticZoneL= true
		LZone=4--RPouch
	elseif LCheckZone(-100,-60,-50,-10,-10,10)  then
		isHapticZoneL= true
		LZone=5--LPouch
	elseif LCheckZone(-40,-25,-15,-5,0,10)   then
		isHapticZoneL= true
		LZone=6--ChestLeft
	elseif LCheckZone(-40,-25,5,15,0,10)  then
		isHapticZoneL= true
		LZone=7--ChestRight
	elseif LCheckZone(-100,-50,-20,20,-30,-15) then
		isHapticZoneL= true
		LZone=8--LowerBack Center
	elseif LCheckZone(-5,10,-10,0,0,10)  then
		isHapticZoneL= true
		LZone=9--LeftEar
	elseif LCheckZone(-5,10,0,10,0,10) then
		isHapticZoneL= true
		LZone=10--RightEar
	else 
		isHapticZoneL= false
		LZone=0--EMPTY
	end
	
	--define Haptic Zone RWeapon
	if isRhand then	
		if LHandWeaponZ <-5 and LHandWeaponZ > -30 and LHandWeaponX < 20 and LHandWeaponX > -15 and LHandWeaponY < 12 and LHandWeaponY > -12 then
			isHapticZoneWL = true
			RWeaponZone = 1 --below gun, e.g. mag reload
		elseif LHandWeaponZ < 10 and LHandWeaponZ > 0 and LHandWeaponX < 10 and LHandWeaponX > -5 and LHandWeaponY < 12 and LHandWeaponY > -12 then
			isHapticZoneWL = true
			RWeaponZone = 2 --close above RHand, e.g. WeaponModeSwitch
		elseif LHandWeaponZ < 25 and LHandWeaponZ > 0 and LHandWeaponX < 45 and LHandWeaponX > 15 and LHandWeaponY < 15 and LHandWeaponY > -15 then
			isHapticZoneWL = true
			RWeaponZone = 3 --Front at barrel l, e.g. Attachement
		else
			RWeaponZone= 0
			isHapticZoneWL=false
		end
	else
		if RHandWeaponZ <-5 and RHandWeaponZ > -30 and RHandWeaponX < 20 and RHandWeaponX > -5 and RHandWeaponY < 12 and RHandWeaponY > -12 then
			isHapticZoneWR = true
	    	LWeaponZone = 1 --below gun, e.g. mag reload
	    elseif RHandWeaponZ < 10 and RHandWeaponZ > 0 and RHandWeaponX < 10 and RHandWeaponX > -5 and RHandWeaponY < 12 and RHandWeaponY > -12 then
	    	isHapticZoneWR = true
	    	LWeaponZone = 2 --close above RHand, e.g. WeaponModeSwitch
	    elseif RHandWeaponZ < 25 and RHandWeaponZ > 0 and RHandWeaponX < 45 and RHandWeaponX > 15 and RHandWeaponY < 12 and RHandWeaponY > -12 then
	    	isHapticZoneWR = true
	    	LWeaponZone = 3 --Front at barrel l, e.g. Attachement
		else
			LWeaponZone=0
			isHapticZoneWR= false
	    end
	end
	
	
	--Code to equip
	if isRhand then
		if RZone== 1 and rGrabActive then
			--local Primary= pawn.Inventory:GetPrimaryWeapon()
			--pawn.Inventory:SwitchToWeapon(Primary)
		elseif RZone== 2 and rGrabActive then
		--	pawn:EquipLongTactical()
		elseif RZone== 4 and rGrabActive then
			--local Secondary= pawn.Inventory:GetSecondaryWeapon()
			--pawn.Inventory:SwitchToWeapon(Secondary)
		elseif RZone== 3 and rGrabActive then
		--	pawn:ToggleNightvisionGoggles()
		elseif LZone== 3 and lGrabActive then
		--	pawn:ToggleNightvisionGoggles()
		elseif RZone== 8 and rGrabActive then
		--	pawn:EquipFlashbang()
		elseif RZone== 6 and rGrabActive then
			--pawn.Inventory:SwitchToAltGrenade()
		elseif RZone== 7 and rGrabActive then
			--pawn.Inventory:SwitchToFragGrenade()
		elseif LZone==2 and lGrabActive then
		--	pawn:EquipLongTactical()
		elseif LZone==5 and lGrabActive then
		--	pawn.InventoryComp:EquipItemFromGroup_Index(1,1)
		elseif LZone==8 and lGrabActive then
		--	pawn.InventoryComp:EquipItemFromGroup_Index(8,0)
		end
	else 
		if LZone == 2 and lGrabActive then
		--	pawn:EquipPrimaryItem()
		elseif LZone== 1 and lGrabActive then
		--	pawn:EquipLongTactical()
		elseif LZone== 5 and lGrabActive then
		--	pawn:EquipSecondaryItem()
		elseif LZone== 3 and lGrabActive then
		--	pawn:ToggleNightvisionGoggles()
		elseif RZone== 3 and rGrabActive then
		--	pawn:ToggleNightvisionGoggles()
		elseif LZone== 8 and lGrabActive then
		--	pawn:EquipFlashbang()
		elseif LZone== 6 and lGrabActive then
			--pawn.Inventory:SwitchToAltGrenade()
		elseif LZone== 7 and lGrabActive then
		--	pawn.Inventory:SwitchToFragGrenade()
		elseif RZone==1 and rGrabActive then
		--	pawn:EquipLongTactical()
		elseif RZone==4 and rGrabActive then
		--	pawn.InventoryComp:EquipItemFromGroup_Index(1,1)
		elseif RZone==8 and rGrabActive then
		--	pawn.InventoryComp:EquipItemFromGroup_Index(8,0)
		end
		
	end
	--Code to trigger Weapon
	if isRhand then
		if RWeaponZone ==1  then
			if lGrabActive then
				isReloading = true --this is forwarded to the XINPUT part of the code edit reloading there if needed
			else isReloading =false
			end
		elseif RWeaponZone == 2 and LTrigger > 230 and LTriggerWasPressed ==0 then
			
			--pawn.Inventory.ActiveWeapon:CycleFireMode()
			--	LTriggerWasPressed=1
				
		elseif RWeaponZone==3 and lThumbOut then
			--pawn:ToggleFlashlight()
		end
	else
		
		if LWeaponZone==1 then
			if rGrabActive then
				isReloading = true
			else isReloading = false
			end
		elseif LWeaponZone== 2 and RTrigger > 230 and RTriggerWasPressed ==0 then
			--pawn.Inventory.ActiveWeapon:CycleFireMode()
			--RTriggerWasPressed=1
		elseif LWeaponZone ==3 and rThumbOut then
			--pawn:ToggleFlashlight()
		end
	end

--DEBUG PRINTS--
--TURN ON FOR HELP WITH COORDINATES

--print("L WEAPON ZONE: " .. LWeaponZone)
--print("R WEAPON ZONE: " .. RWeaponZone)

----COORDINATES FOR HOLSTERS
--print("RHandz: " .. RHandLocation.z .. "     Rhandx: ".. RHandLocation.x )
--print("RHandx: " .. RHandNewX .. "     Lhandx: ".. LHandNewX .."      HMDx: " .. HmdLocation.x)
--print("RHandy: " .. RHandNewY .. "     Lhandy: ".. LHandNewY .."      HMDy: " .. HmdLocation.y)
--print(HmdRotation.y)
--print("                   ")
--print("                   ")
--print("                   ")

----COORDINATES FOR WEAPON ZONES:
--print("RHandz: " .. RHandWeaponZ .. "     Lhandz: ".. LHandWeaponZ )
--print("RHandx: " .. RHandWeaponX .. "     Lhandx: ".. LHandWeaponX )
--print("RHandy: " .. RHandWeaponY .. "     Lhandy: ".. LHandWeaponY )
--print("                   ")
--print("                   ")
--print("                   ")


end)
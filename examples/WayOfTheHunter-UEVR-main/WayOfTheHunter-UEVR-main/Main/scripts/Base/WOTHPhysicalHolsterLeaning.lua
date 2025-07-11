require(".\\Base\\Subsystems\\Trackers")
require(".\\Config\\CONFIG")
require(".\\Base\\Subsystems\\UEHelper")
--CONFIG--
--------	
	--local isRhand = true							--right hand config
	--local isLeftHandModeTriggerSwitchOnly = true    --only swap triggers for left hand
	--local HapticFeedback = true                     --haptic feedback for holsters
	--local PhysicalLeaning = true                    --Physical Leaning
	--local DisableUnnecessaryBindings= true          --Disables some buttons that are replaced by gestures
	--local SprintingActivated=true                   --
	--local HolstersActive=true                       --
	--local WeaponInteractions=true                   --Weapon interation gestures like reloading
	--local isRoomscale=true                          --Roomscale swap when leaning
--------
--------	
	local api = uevr.api
	
	local params = uevr.params
	local callbacks = params.sdk.callbacks
	local pawn = api:get_local_pawn(0)
	local vr=uevr.params.vr



local lControllerIndex= 1
local rControllerIndex= 2



local rGrabActive =false
local lGrabActive =false
local rGrabWasPressed=false
local lGrabWasPressed=false
local LZone=0

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

local isDpadLeft=false
local isDpadRight=false
local isSelect=false
local function isButtonPressed(state, button)
	return state.Gamepad.wButtons & button ~= 0
end
local function isButtonNotPressed(state, button)
	return state.Gamepad.wButtons & button == 0
end
local function pressButton(state, button)
	state.Gamepad.wButtons = state.Gamepad.wButtons | button
end
local function unpressButton(state, button)
	state.Gamepad.wButtons = state.Gamepad.wButtons & ~(button)
end

uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--Read Gamepad stick input 
	--Reset Thumb for WeaponInteractions
	if math.abs(ThumbLY)<=500 and math.abs(ThumbLX)<=500 then
		ThumbLActive=false
	end
	--
	if isRhand or isLeftHandModeTriggerSwitchOnly then
		if DisableUnnecessaryBindings then
			if not Xbutton and inTablet ==false then
				unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_UP			)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN	    )
	
			end
		end
	
	else 
		if DisableUnnecessaryBindings then
			if not lShoulder then
				
				unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT		)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_UP			)
				unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN	    )
			end
		end
	
	end


	if not isRhand then
		state.Gamepad.bLeftTrigger=RTrigger
		state.Gamepad.bRightTrigger=LTrigger
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


	
	
	
	
	--Unpress when in Zone
--   local isPaused = api:get_player_controller().PauseMenu.Visibility
--	--print(isPaused)
--	if isPaused ~=4  then
--		--unpressButton(state, XINPUT_GAMEPAD_B)
--		--unpressButton(state, XINPUT_GAMEPAD_X)
--		--unpressButton(state, XINPUT_GAMEPAD_Y)
--		unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
--	end
	--enable for command menu else disable
--	if not rShoulder then 
--		unpressButton(state, XINPUT_GAMEPAD_DPAD_UP)
--		unpressButton(state, XINPUT_GAMEPAD_DPAD_LEFT)
--		unpressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT)
--		unpressButton(state, XINPUT_GAMEPAD_DPAD_DOWN)
--	end
	if isRhand then	
		if  RZone ~=0 and inTablet ==false then
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB)
		end
	else
		if LZone ~= 0 and inTablet ==false then
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_SHOULDER)
			--unpressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
			unpressButton(state, XINPUT_GAMEPAD_RIGHT_THUMB)
		end
	end
	--print(RWeaponZone .. "   " .. RZone)
	--disable Trigger for modeswitch
	if RWeaponZone == 2 then
		state.Gamepad.bLeftTrigger=0
	end
	
	-- Button singlepress fixes
	-- From Equip calls:
	if isTablet then
		pressButton(state, XINPUT_GAMEPAD_BACK)
		isTablet=false
	end
	
	if not lThumb then 
		--pressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT)
		lThumbSwitchState=0
	end
	
	if not rThumb then
		--pressButton(state, XINPUT_GAMEPAD_DPAD_RIGHT)
		rThumbSwitchState=0
	end
	
	--print(rThumbOut)
	if isReloading then
		pressButton(state, XINPUT_GAMEPAD_X)
	end
	
		
	
	
	--Reset Height
	
	
	--Grab activation
	if rShoulder then
		rGrabActive= true
	else rGrabActive =false
		rGrabWasPressed=false
	end
	if lShoulder  then
		lGrabActive =true
		--unpressButton(state, XINPUT_GAMEPAD_LEFT_SHOULDER)
	else lGrabActive=false
		lGrabWasPressed=false
	end
	
	if isRhand then
		
		if LZone==9 and LTrigger>=230 then
			pressButton(state, XINPUT_GAMEPAD_Y)
		end
		if DisableUnnecessaryBindings==true then
			unpressButton(state,XINPUT_GAMEPAD_RIGHT_SHOULDER)
			if Xbutton then
				pressButton(state,XINPUT_GAMEPAD_RIGHT_SHOULDER)
			end
		end
		
	else
	
		if LZone == 5 and lGrabActive then
			pressButton(state, XINPUT_GAMEPAD_LEFT_THUMB)
		end
	
	end

if isRhand and inTablet ==false then	
	if rThumb then
		pressButton(state,XINPUT_GAMEPAD_DPAD_LEFT)
	end
	if lThumb and LZone== 5 then
		pressButton(state,XINPUT_GAMEPAD_DPAD_UP)
	end
else 
	if rThumb and RZone==4 then
		pressButton(state,XINPUT_GAMEPAD_DPAD_UP)
	end
	if lThumb then
		pressButton(state,XINPUT_GAMEPAD_DPAD_LEFT)
	end	
end
if RWeaponZone==4 and isDriving==false then
	state.Gamepad.sThumbLX=0
	state.Gamepad.sThumbLY=0
end
if isDpadLeft  then
	pressButton(state,XINPUT_GAMEPAD_DPAD_LEFT)
	isDpadLeft=false
end
if isDpadRight  then
	pressButton(state,XINPUT_GAMEPAD_DPAD_RIGHT)
	isDpadRight=false
end
if isSelect then
	pressButton(state,XINPUT_GAMEPAD_BACK)
	isSelect=false
end
	--local VecA= Vector3f.new(x,y,z)
	--	print(VecA.x)

--sprinting 
if isMenu == false and current_scope_state==false then
	unpressButton(state,XINPUT_GAMEPAD_LEFT_SHOULDER)
	unpressButton(state,XINPUT_GAMEPAD_LEFT_THUMB)
	if lShoulder then
		pressButton(state,XINPUT_GAMEPAD_LEFT_THUMB)
	end
end

end)


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
	local HmdVRRot =UEVR_Quaternionf.new()
	local HmdVRPos =UEVR_Vector3f.new()
	
	--print(right_hand_component:K2_GetComponentLocation())
--local LHandLocation = left_hand_actor:K2_GetActorLocation()
--local HMDLocation = hmd_actor:K2_GetActorLocation()
--


local RotDiff= 0

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
	pawn=api:get_local_pawn(0)

	RHandLocation=right_hand_component:K2_GetComponentLocation()
	LHandLocation=left_hand_component:K2_GetComponentLocation()
	HmdLocation= hmd_component:K2_GetComponentLocation()
	

	
	local HmdRotation= hmd_component:K2_GetComponentRotation()
	local RHandRotation = right_hand_component:K2_GetComponentRotation()
	local LHandRotation = left_hand_component:K2_GetComponentRotation()
	


	
	-- Y IS LEFT RIGHT, X IS BACK FORWARD, Z IS DOWN  UP
	 RotDiff= HmdRotation.y	--(z axis of location)
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
	
	--small force feedback on enter and leave
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
	--define Haptic zones RHand Z: UP/DOWN, Y:RIGHT LEFT, X FORWARD BACKWARD, checks if RHand is in RZone
if HolstersActive then	

	if 	   RCheckZone(-10, 15, 10, 30, -10, 20) then --Zone01_Z_Min, Zone01_Z_Max
		isHapticZoneR =true
		RZone=1-- RShoulder
		
	elseif RCheckZone(-10, 15, -30, -10, -10, 20)      then
		isHapticZoneR =true
		RZone=2--Left Shoulder
		
	elseif RCheckZone(0, 20, -5, 5, 0, 20)  then
		isHapticZoneR= true
		RZone=3-- Over Head
		
	elseif RCheckZone(-100,-60,5,50,-10,30)   then
		isHapticZoneR= true
		RZone=4--RHip
		
	elseif RCheckZone(-100,-40,-50,5,-10,50)   then
		isHapticZoneR= false
		RZone=5--LHip
		
	elseif RCheckZone(-30,-20,-15,-5,0,15)   then
		isHapticZoneR= false
		RZone=6--ChestLeft
		
	elseif RCheckZone(-30,-20,5,15,0,15)  then
		isHapticZoneR= true
		RZone=7--ChestRight
		
	elseif RCheckZone(-100,-50,-20,20,-30,-15)	  then
		isHapticZoneR= false
		RZone=8--LowerBack Center
		
	elseif RCheckZone(-5,10,-10,0,0,10) then
		isHapticZoneR= false
		RZone=9--LeftEar
		
	elseif RCheckZone(-5,10,0,10,0,10)  then
		isHapticZoneR= false
		RZone=10--RightEar
	else 
		isHapticZoneR= false
		RZone=0--EMPTY
	end
	--define Haptic zone Lhandx Z: UP/DOWN, Y:RIGHT LEFT, X FORWARD BACKWARD, checks if RHand is in RZone
	if LCheckZone(-10, 15, 10, 30, -10, 20) then
		isHapticZoneL =false
		LZone=1-- RShoulder
		
	elseif LCheckZone (-10, 15, -30, -10, -10, 20) then
		isHapticZoneL =true
		LZone=2--Left Shoulder
		
	elseif LCheckZone(0, 30, -5, 5, 0, 20) then
		isHapticZoneL= true
		LZone=3-- Over Head
		
	elseif LCheckZone(-100,-60,22,50,-10,10)  then
		isHapticZoneL= false
		LZone=4--RPouch
		
	elseif LCheckZone(-100,-45,-50,-20,-10,40)  then
		isHapticZoneL= false
		LZone=5--LPouch
		
	elseif LCheckZone(-30,-20,-15,-5,0,10)   then
		isHapticZoneL= true
		LZone=6--ChestLeft
		
	elseif LCheckZone(-30,-20,5,15,0,10)  then
		isHapticZoneL= false
		LZone=7--ChestRight
		
	elseif LCheckZone(-100,-50,-20,20,-30,-15) then
		isHapticZoneL= false
		LZone=8--LowerBack Center
		
	elseif LCheckZone(-5,10,-10,0,0,10)  then
		isHapticZoneL= false
		LZone=9--LeftEar
		
	elseif LCheckZone(-5,10,0,10,0,10) then
		isHapticZoneL= false
		LZone=10--RightEar
	else 
		isHapticZoneL= false
		LZone=0--EMPTY
	end
end	
	--define Haptic Zone RWeapon
if WeaponInteractions then
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
		elseif LHandWeaponZ < 20 and LHandWeaponZ > 10 and LHandWeaponX < 20 and LHandWeaponX > 0 and LHandWeaponY < 12 and LHandWeaponY > -12 then
			isHapticZoneWL = true
			RWeaponZone = 4
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
end	
	
	--Code to equip
if isDriving==false then
	if isRhand then
		if RZone== 1 and rGrabActive then
			pawn:EquipSlot1()
		elseif RZone== 2 and rGrabActive then
			pawn:EquiptSlot3()
		elseif RZone== 4 and rGrabActive then
			pawn:EquipSlot2()
		elseif RZone== 3 and rGrabActive then
			if rGrabWasPressed == false then
			pawn:ToggleHeadTorch()
			end
		elseif LZone== 3 and lGrabActive then
			if lGrabWasPressed == false then
			pawn:ToggleHeadTorch()
			lGrabWasPressed=true
			end
		elseif RZone== 5 and rGrabActive then
			--pawn:EquipCSGas()
		elseif RZone== 6 and rGrabActive then
			--pawn:EquipStinger()	
		elseif RZone== 7 and rGrabActive then
			--pawn:EquipSlot4()
		elseif LZone==2 and lGrabActive then
			if lGrabWasPressed==false then
				isSelect=true
				lGrabWasPressed=true
			end
		elseif LZone==5 and lGrabActive then
			--pawn.InventoryComp:EquipItemFromGroup_Index(1,1)
		elseif LZone==6 and lGrabActive then
			pawn:EquipSlot4()
		end
	else 
		if LZone == 2 and lGrabActive then
			pawn:EquipPrimaryItem()
		elseif LZone== 1 and lGrabActive then
			pawn:EquipLongTactical()
		elseif LZone== 5 and lGrabActive then
			pawn:EquipSecondaryItem()
		elseif LZone== 3 and lGrabActive then
			pawn:ToggleNightvisionGoggles()
		elseif RZone== 3 and rGrabActive then
			pawn:ToggleNightvisionGoggles()
		elseif LZone== 4 and lGrabActive then
			pawn:EquipFlashbang()
		elseif LZone== 6 and lGrabActive then
			pawn:EquipCSGas()
		elseif LZone== 7 and lGrabActive then
			pawn:EquipStinger()	
		elseif RZone==1 and rGrabActive then
			pawn:EquipLongTactical()
		elseif RZone==4 and rGrabActive then
			pawn.InventoryComp:EquipItemFromGroup_Index(1,1)
		elseif RZone==7 and rGrabActive then
			isTablet = true
		end
		
	end
	--Code to trigger Weapon
	if isRhand then
		if RWeaponZone ==1  then
			if lGrabActive then
				isReloading = true
			else isReloading =false
			end
		elseif RWeaponZone == 2 and LTrigger > 230 then
			pawn:CycleFireMode()
		elseif RWeaponZone==3 and lThumb and lThumbSwitchState==0 then
			pawn:ToggleHeadTorch()
			lThumbSwitchState=1
		elseif RWeaponZone==4 and ThumbLY > 30000 and ThumbLActive==false and pawn.m_isInADS then
			pcall(function()
				pawn:GetCurrentArm():IncreaseZoom()
				ThumbLActive=true
			end)
		elseif RWeaponZone==4 and ThumbLY < -30000 and ThumbLActive==false and pawn.m_isInADS then
			pcall(function()
				pawn:GetCurrentArm():DecreaseZoom()
				ThumbLActive=true
			end)
		elseif RWeaponZone==4 and ThumbLX > 30000 and ThumbLActive==false and pawn.m_isInADS then
			
				isDpadRight=true
				ThumbLActive=true
			
		elseif RWeaponZone==4 and ThumbLX < -30000 and ThumbLActive==false and pawn.m_isInADS then
			
				isDpadLeft=true
				ThumbLActive=true
			
		end
	else
		
		if LWeaponZone==1 then
			if rGrabActive then
				isReloading = true
			else isReloading = false
			end
		elseif LWeaponZone== 2 and RTrigger > 230 then
			pawn:CycleFireMode()
		elseif LWeaponZone ==3 and rThumb and rThumbSwitchState==0 then
			pawn:ToggleUnderbarrelAttachment()
			rThumbSwitchState=1
		end
	end
	
end	

	
--print(LWeaponZone)
--DEBUG PRINTS--
--TURN ON FOR HELP WITH COORDINATES

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
--pawn:LeanRight(0.2)

end)



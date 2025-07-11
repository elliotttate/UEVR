require(".\\Base\\Subsystems\\Trackers")
require(".\\Config\\CONFIG")
require(".\\Base\\Subsystems\\UEHelper")
local api = uevr.api
local vr = uevr.params.vr


 local camera_component_c = api:find_uobject("Class /Script/Engine.CameraComponent")

local ActiveHandState= 0 --0: non, 1:right, 2:left
local isLHandPressed=false
local isRHandPressed=false


--degrees
local CurrentHandRoll_Right = 0
local CurrentHandRoll_Left  = 0
local StartRoll_Left=0
local StartRoll_Right=0


local CurrentSteerVal=0
local LastSteerVal=0
local CheckSteerVal =0
local Roll_Last_Left=0
local Roll_Last_Right=0
local DiffAngleRight=0
local DiffAngleLeft=0
local Tick=0

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

local function UpdateHandState()
	
	
	if Tick >=3 then
	DeltaRight= math.abs(CurrentHandRoll_Right - Roll_Last_Right)
	Roll_Last_Right= CurrentHandRoll_Right
	DeltaLeft= math.abs(CurrentHandRoll_Left - Roll_Last_Left	)
	Roll_Last_Left=CurrentHandRoll_Left
	Tick=0
	else
		Tick=Tick+1
	end
	if lShoulder  then
		--ActiveHandState= 2
		--isLHandPressed = true
		if ActiveHandState ~= 2 and DeltaLeft > DeltaRight then
			ActiveHandState = 2
			StartRoll_Left=CurrentHandRoll_Left
		end
	end
		--DeltaLeft= CurrentHandRoll_Left - Roll_Last_Left
		--Roll_Last_Left=CurrentHandRoll_Left
	
	
	if rShoulder  then
		--ActiveHandState= 1
		--isRHandPressed = true
		if ActiveHandState ~= 1 and DeltaRight >= DeltaLeft then
			StartRoll_Right= CurrentHandRoll_Right
			ActiveHandState=1 
		end
	end
			--DeltaRight= CurrentHandRoll_Right - Roll_Last_Right
		--	Roll_Last_Right= CurrentHandRoll_Right
	if not rShoulder and not lShoulder then
		ActiveHandState=0
	end
	if LastHandState==ActiveHandState then
		CheckSteerVal=CurrentSteerVal	
	elseif LastHandState~=ActiveHandState then
		LastSteerVal=CheckSteerVal
	end
	LastHandState=ActiveHandState
	
end

--XINPUT functions

local function UpdateInput(state)

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
end

local function Drive(state)
	--print(CurrentHandRoll_Left)
	--print(ActiveHandState)
	--print("  ")
	if isDriving then
		--state.Gamepad.sThumbLX = 0
		--state.Gamepad.sThumbRX = 0
	
		if ActiveHandState == 1 then
			DiffAngleRight= CurrentHandRoll_Right-StartRoll_Right
			CurrentSteerVal= LastSteerVal + DiffAngleRight
			
		elseif ActiveHandState ==2 then
			DiffAngleLeft= CurrentHandRoll_Left-StartRoll_Left
			CurrentSteerVal= LastSteerVal + DiffAngleLeft
		elseif ActiveHandState==0 then
			CurrentSteerVal = 0
		end
		if CurrentSteerVal>90 then
			CurrentSteerVal=90 
		end
		if CurrentSteerVal<-90 then
			CurrentSteerVal=-90 
		end
		state.Gamepad.sThumbLX = 32767/90*CurrentSteerVal
	end	
end



uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--Read Gamepad stick input 
if isDriving and PhysicalDriving then
--INPUT OVerrides:	

	Drive(state)
end


end)





local CheckSteerVal

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
	
	
		
	
	pawn=api:get_local_pawn(0)

	--print(isDriving)
UpdateHandState()

--degrees:	
CurrentHandRoll_Right= right_hand_component:K2_GetComponentRotation().z
CurrentHandRoll_Left= left_hand_component:K2_GetComponentRotation().z
--print(CurrentSteerVal)
--print(LastSteerVal)
--print("   ")
--print("   ")


	
end)


uevr.params.sdk.callbacks.on_early_calculate_stereo_view_offset(

function(device, view_index, world_to_meters, position, rotation, is_double)
--print(rotation.x)
if	isDriving==false then    
		DefaultPos=position
	--		if LastTickRot~=rotation.x then
	--		RotDiff = rotation.x -LastTickRot
	--		
	--		LastTickRot = rotation.x
	--		end
	--		print("RotDiff    :"..RotDiff)
	--RotSave=1
	--		else
	--	elseif TrState==0 then
	--		RotSave=0
	--		RotDiff=0
	--	end
	--	if RotSave == 1 then
	--		RotDiff=rotation.x - RotationXStart
	--	end
		
		--local FinalAngle=tostring(PositiveIntegerMask(DefaultOffset)/1000000+RotDiff)
		--uevr.params.vr.set_mod_value("VR_ControllerPitchOffset", FinalAngle)
		local Cpawn=api:get_local_pawn(0)
		if isDriving==false then
			pcall(function()
			local pawn_pos = Cpawn.RootComponent:K2_GetComponentLocation()
			
			position.x = pawn_pos.x 
			position.y = pawn_pos.y
			position.Z = pawn_pos.z + 70-- +5
			end)
		else
			
			
			
		end
	--print(isDriving)
end
end)

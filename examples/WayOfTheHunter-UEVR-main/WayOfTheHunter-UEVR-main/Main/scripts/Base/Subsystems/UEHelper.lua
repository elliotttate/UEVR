
local api = uevr.api
	
	local params = uevr.params
	local callbacks = params.sdk.callbacks
	local pawn = api:get_local_pawn(0)
	local vr=uevr.params.vr



function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        error("Cannot find " .. name)
        return nil
    end

    return obj
end
function find_static_class(name)
    local c = find_required_object(name)
    return c:get_class_default_object()
end

function SearchSubObjectArrayForObject(ObjArray, string_partial)
local FoundItem= nil
	for i, InvItems in ipairs(ObjArray) do
				if string.find(InvItems:get_fname():to_string(), string_partial) then
				--	print("found")
					FoundItem=InvItems
					--return FoundItem
				break
				end
	end
return	FoundItem
end

--INPUT functions:-------------
-------------------------------

--VR to key functions
function SendKeyPress(key_value, key_up)
    local key_up_string = "down"
    if key_up == true then 
        key_up_string = "up"
    end
    
    api:dispatch_custom_event(key_value, key_up_string)
end

function SendKeyDown(key_value)
    SendKeyPress(key_value, false)
end

function SendKeyUp(key_value)
    SendKeyPress(key_value, true)
end

function PositiveIntegerMask(text)
	return text:gsub("[^%-%d]", "")
end
--
--Xinput helpers
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
--

--GLOBAL VARIABLES
current_scope_state=false


--Dynamic helper functions:
 ThumbLX   = 0
 ThumbLY   = 0
 ThumbRX   = 0
 ThumbRY   = 0
 LTrigger  = 0
 RTrigger  = 0
 rShoulder = false
 lShoulder = false
 lThumb    = false
 rThumb    = false
 Abutton = false
 Bbutton = false
 Xbutton = false
 Ybutton = false
 SelectButton=false

isDriving=false
isMenu=false

local GameTime= 0



function UpdateInput(state)

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

local function UpdateDriveStatus(pawn)
	if pawn.AdjustedSteeringInput ~=nil then
		isDriving=true
		uevr.params.vr.set_mod_value("VR_AimMethod" , "0")
	else isDriving=false
		uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
	end
end

local function UpdateMenuStatus(Player)
	if Player.bShowMouseCursor then
		isMenu=true
	else isMenu=false
	end
end



uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)
local dpawn=nil
dpawn=api:get_local_pawn(0)


	UpdateDriveStatus(dpawn)
	
	

--Read Gamepad stick input 
if PhysicalDriving then
	UpdateInput(state)

end

end)

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
	
local Player=api:get_player_controller(0)	
UpdateMenuStatus(Player)
	
	
end)
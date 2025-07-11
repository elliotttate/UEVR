local uevrUtils = require("libs/uevr_utils")

local M = {}

local activeWeapon = nil
local currentLocationOffset = {0, 0, 0}

local weaponOffsets = {
	AKM = {-6.0, -3.2, 0},
	SmgUZI = {-0.8, 0.0, 0.6},
	SteyrAUG = {-0.8, -0.8, 2.0},
	BrowningM60 = {-1.8, 0.0, 0.0},
	BarrettM82_Sniper = {-2.4, 0.0, 0.0}
}

function M.print(str, logLevel)
	uevrUtils.print("[Weapons] " .. str, logLevel)
end

function M.reset()
	activeWeapon = nil
end

function getWeaponLocationOffset(weapon)
	local weaponName = weapon:get_full_name()
	for name, offset in pairs(weaponOffsets) do
		if string.find(weaponName, name) then
			return weaponOffsets[name]
		end
	end

	return {0, 0, 0}
end

function M.attachWeapon(currentWeapon, hand)
	if hand == nil then hand = Handed.Right end
	local lastWeapon = activeWeapon
	if uevrUtils.validate_object(activeWeapon) ~= nil and uevrUtils.validate_object(currentWeapon) ~= nil and activeWeapon ~= currentWeapon then
		print("Disconnecting weapon")
		UEVR_UObjectHook.remove_motion_controller_state(activeWeapon)
		activeWeapon = nil
	end
	
	if uevrUtils.validate_object(currentWeapon) ~= nil and not string.find(currentWeapon:get_full_name(), "NoWeapon") and activeWeapon ~= currentWeapon then
		M.print("Connecting weapon ".. currentWeapon:get_full_name() .. " " .. currentWeapon:get_fname():to_string())
		local state = UEVR_UObjectHook.get_or_add_motion_controller_state(currentWeapon)
		state:set_hand(hand)
		state:set_permanent(true)
		state:set_rotation_offset(Vector3f.new(0, math.rad(90), 0))
		currentLocationOffset = getWeaponLocationOffset(currentWeapon)
		state:set_location_offset(Vector3f.new(currentLocationOffset[1], currentLocationOffset[2], currentLocationOffset[3]))
		
		--print(currentWeapon:get_full_name())
		
		uevrUtils.fixMeshFOV(currentWeapon, "UsePanini", 0.0, true, true, true)	
		activeWeapon = currentWeapon			
	end
	
	if lastWeapon ~= activeWeapon then
		if on_weapon_change ~= nil then
			on_weapon_change(activeWeapon)
		end
	end
end


function M.connectToSocket(pawn, handComponent, socketName, offset)
	local lastWeapon = activeWeapon
	local currentWeapon = uevrUtils.getValid(pawn,{"Weapon","WeaponMesh"})
	if uevrUtils.validate_object(activeWeapon) ~= nil and currentWeapon ~= nil and activeWeapon ~= currentWeapon then
		print("Disconnecting weapon")
		activeWeapon = nil
	end


	if currentWeapon ~= nil and not string.find(pawn.Weapon:get_full_name(), "NoWeapon") and activeWeapon ~= currentWeapon then
	print("Attaching weapon to socket")
		currentWeapon:K2_AttachTo(handComponent, uevrUtils.fname_from_string(socketName), 0, false)
		uevrUtils.set_component_relative_transform(currentWeapon, offset, offset)	
		activeWeapon = currentWeapon			
	end
	if lastWeapon ~= activeWeapon then
		print("Weapon changed")

		if on_weapon_change ~= nil then
			on_weapon_change(activeWeapon)
		end
	end
end


function M.getActiveWeapon()
	return activeWeapon
end

function M.adjustLocation(axis, delta)
	if uevrUtils.getValid(activeWeapon) ~= nil then
		local state = UEVR_UObjectHook.get_or_add_motion_controller_state(activeWeapon)
		currentLocationOffset[axis] = currentLocationOffset[axis] + delta
		state:set_location_offset(Vector3f.new(currentLocationOffset[1], currentLocationOffset[2], currentLocationOffset[3]))
		M.printHandTranforms()
	end
end

function M.adjustRotation(axis, delta)
	M.print("adjustRotation not implemented")
end

local adjustMode = 2  -- 1-weapon rotation  2-weapon location
local adjustModeLabels = {"Weapon Rotation", "Weapon Location"}
local positionDelta = 0.2
local rotationDelta = 5

function M.enableAdjustments()	
	M.print("Adjust Mode " .. adjustModeLabels[adjustMode])
	
	register_key_bind("NumPadFive", function()
		M.print("Num5 pressed")
		adjustMode = (adjustMode % 2) + 1
		M.print("Adjust Mode " .. adjustModeLabels[adjustMode])
	end)

	register_key_bind("NumPadEight", function()
		M.print("Num8 pressed")
		if adjustMode == 1 then
			M.adjustRotation(1, rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(1, positionDelta)
		end
	end)
	register_key_bind("NumPadTwo", function()
		M.print("Num2 pressed")
		if adjustMode == 1 then
			M.adjustRotation(1, -rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(1, -positionDelta)
		end
	end)

	register_key_bind("NumPadFour", function()
		M.print("Num4 pressed")
		if adjustMode == 1 then
			M.adjustRotation(2, rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(2, positionDelta)
		end
	end)
	register_key_bind("NumPadSix", function()
		M.print("Num6 pressed")
		if adjustMode == 1 then
			M.adjustRotation(2, -rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(2, -positionDelta)
		end
	end)

	register_key_bind("NumPadThree", function()
		M.print("Num3 pressed")
		if adjustMode == 1 then
			M.adjustRotation(3, rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(3, positionDelta)
		end
	end)
	register_key_bind("NumPadOne", function()
		M.print("Num1 pressed")
		if adjustMode == 1 then
			M.adjustRotation(3, -rotationDelta)
		elseif adjustMode == 2 then
			M.adjustLocation(3, -positionDelta)
		end
	end)
end

function M.printHandTranforms()
	--M.print("Rotation = {" .. currentRotationOffset[1] .. ", " .. currentRotationOffset[2] .. ", "  .. currentRotationOffset[3] ..  "}", LogLevel.Info)
	M.print("Location = {" .. currentLocationOffset[1] .. ", " .. currentLocationOffset[2] .. ", "  .. currentLocationOffset[3] ..  "}", LogLevel.Info)
end


return M
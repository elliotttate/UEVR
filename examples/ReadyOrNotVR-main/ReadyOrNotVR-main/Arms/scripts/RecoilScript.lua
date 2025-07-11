require("CONFIG")
--YOU CAN EDIT THIS LINE true or false
--local isUpRecoilActive = true
--local isRhand =true

-----------------------------


local api = uevr.api
local pawn = api:get_local_pawn(0) 
local DefaultOffset= uevr.params.vr:get_mod_value("VR_ControllerPitchOffset")
 
function PositiveIntegerMask(text)
	return text:gsub("[^%-%d]", "")
end

local RotSave=0
local RotDiff=0
local RotationXStart=0
local RotationXCur=0
local LastTickRot=0
local CurrentRot=0
local isShooting=false
local RTrigger=0
local VertTickCount=0
local RotDiffLast=0
local UsedPrimary=0
local ResetTick=0
local isFakeDiff=false
local RotDiffFake=0
local RotDiffPre=0
local LastResetTick=0
local VertDiffLast=0
local VertDiffsecondaryLast =0	
local Backrecoil = 0
local BackrecoilSecondary= 0
local BackrecoilLast=0
local BackTickCount=0
local BackrecoilSecondaryLast=0
local VertDiffOut =0
local RotDiffOut=0
local ExtraTickCount=0
local RotSecondaryExtra=0
local VertDiffsecondaryOut=0
local RotSecondaryExtraOut=0
local PrimaryMesh=nil
local SecondaryMesh=nil



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

local lControllerIndex= 1
local rControllerIndex= 2



local VertDiff=0

local function update_weapon_offset(weapon_mesh)
    if not weapon_mesh then return end

    local attach_socket_name = weapon_mesh.AttachSocketName
	local PMesh= pawn.Mesh1P
    -- Get socket transforms
    local default_transform = PMesh:GetSocketTransform("thumb_2_RI",2)--Transform(attach_socket_name, 2)
    local offset_transform = PMesh:GetSocketTransform("pinky_4_RI",2)--weapon_mesh:GetSocketTransform("jnt_offset", 2)
	
	local middle_translation = kismet_math_library:Add_VectorVector(default_transform.Translation, offset_transform.Translation)
    local location_diff = kismet_math_library:Subtract_VectorVector(
        middle_translation,--.Translation,
        Vector3f.new(0,0,0)
    )
    -- from UE to UEVR X->Z Y->-X, Z->-Y
    -- Z - forward, X - negative right, Y - negative up
    local lossy_offset = Vector3f.new(location_diff.y/2, -VertDiff-0+location_diff.z/2, -location_diff.x/2)
    -- Apply the offset to the weapon using motion controller state
	
    UEVR_UObjectHook.get_or_add_motion_controller_state(PMesh):set_hand(1)
    UEVR_UObjectHook.get_or_add_motion_controller_state(PMesh):set_location_offset(lossy_offset)
    UEVR_UObjectHook.get_or_add_motion_controller_state(PMesh):set_permanent(true)
 -- print(default_transform.Translation.x.. "   " ..default_transform.Translation.y .. "    " ..default_transform.Translation.z)
end

pcall(function()
uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--Read Gamepad stick input 
	local ThumbLX = state.Gamepad.sThumbLX
	local ThumbLY = state.Gamepad.sThumbLY
	local ThumbRX = state.Gamepad.sThumbRX
	local ThumbRY = state.Gamepad.sThumbRY
	RTrigger= state.Gamepad.bRightTrigger
	
	
	
	
end)

uevr.params.sdk.callbacks.on_early_calculate_stereo_view_offset(
function(device, view_index, world_to_meters, position, rotation, is_double)
	
	
	
	--if not ResetTick == LastResetTick then
--print(rotation.x)
	
	
	LastResetTick=LastResetTick+1
	if LastResetTick==2 then
		LastResetTick=0
	end
	
	if LastResetTick==1 then
	
	
	
	--print(isFakeDiff)
	--print("ResetTick: "..ResetTick)
	--print(LastTickRot)
	--print(isShooting)
	
			--if LastTickRot~=rotation.x then
			if rotation.x -LastTickRot ~=0 and isFakeDiff == false then
			
				RotDiff = rotation.x -LastTickRot
				LastTickRot = rotation.x
				RotDiffFake=RotDiff
			elseif rotation.x -LastTickRot ~=0 and isFakeDiff then
				RotDiff = RotDiffFake
			end
	RotDiff = math.abs(RotDiff)
	if RotDiff > 0.15 then
		RotDiff= 0.15
	end
	--print(RotDiff)		
	--print("                            ")
	--print("                            ")
	--print("                            ")
	end
			--print("RotDiff    :"..RotDiff)
		
		--if isShooting and not isFakeDiff then
		--	uevr.params.vr.set_mod_value("VR_AimMethod" , "0")
		--elseif isShooting and isFakeDiff then
		--	uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
		--elseif not isShooting then
		--	uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
		--end
end)



uevr.sdk.callbacks.on_post_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)

	


end)

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
		pawn = api:get_local_pawn(0)
	pcall(function()
		UsedPrimary=pawn.InventoryComp.LastEquippedWeapon.HeatTime
	end)	
	
	pcall(function()
		EquippedWpn=pawn.InventoryComp.LastEquippedWeapon
	end)
	
	pcall(function()	
		if string.find(EquippedWpn:get_fname():to_string(), "Secondary") then
			isSecondary=true
		elseif string.find(EquippedWpn:get_fname():to_string(), "Primary") then
			isSecondary=false
		end
	end)	
		--print(WeaponType:get_fname():to_string())
	
		local CurrentPrimary = pawn.Mesh1P
		local CurrentSecondary=pawn.Mesh1P
		VertDiff = math.tan(RotDiffOut*math.pi/180)* 30
		local VertDiffsecondary= math.tan(RotDiffOut*math.pi/180)* 42
	
	--print(VertDiff)
	--print(isFakeDiff)
	--print(RotDiff)
	--print(RotDiffOut)
--	print("                            ")
--	print("                            ")
--	print("                            ")
		--print("pre:   " ..RotDiffPre)
	if isShooting  then
		if isFakeDiff == false then
			RotDiffOut = RotDiffOut+RotDiff
			RotDiffLast= RotDiffOut
			RotDiffPre=RotDiff
			
			VertDiffOut= VertDiffOut + VertDiff
			VertDiffLast =VertDiffOut
				
			VertDiffsecondaryOut=VertDiffsecondaryOut+ VertDiffsecondary
			VertDiffsecondaryLast=VertDiffsecondaryOut
		--	
		
		--print("pre:   " ..RotDiffPre)
			VertTickCount=0
		elseif isFakeDiff == true then
			RotDiffOut = RotDiffOut 
			RotDiffLast= RotDiffOut
			
			VertTickCount=0
		end
	end
	

	
	if BackTickCount <= 20 then 
		BackTickCount=BackTickCount+1
	end
	
	if UsedPrimary <= 0.02 then
		BackTickCount=0
		Backrecoil =2
		BackrecoilSecondary = 2
		BackrecoilLast=Backrecoil
		BackrecoilSecondaryLast=BackrecoilSecondary
	end
		local durationb=5
		
	if BackTickCount<durationb then
		local decrementBack  = BackrecoilLast / durationb
		local decrementBackSecondary = BackrecoilSecondaryLast/durationb
		Backrecoil = 2 - decrementBack * BackTickCount
		BackrecoilSecondary = 2 - decrementBackSecondary *BackTickCount
	elseif BackTickCount >= durationb then 
		Backrecoil=0
		BackrecoilSecondary=0
		BackrecoilLast=0
	end
		
	
		
	
	
	if UsedPrimary > 0.1 and isSecondary ==false then		
		local durationa=20
		if VertTickCount<durationa then
			local decrementVert = VertDiffLast / (durationa)
			--local decrementVertSecondary= VertDiffsecondaryLast/duration
			local decrementRot  = RotDiffLast / durationa 
			VertTickCount=VertTickCount+1
			
			VertDiffOut = VertDiffLast-decrementVert*VertTickCount
			--VertDiffsecondaryOut= VertDiffsecondaryLast - decrementVertSecondary*VertTickCount
			RotDiffOut = RotDiffLast - decrementRot*VertTickCount

		elseif VertTickCount>=durationa then
				VertDiffOut=0 
				RotDiffOut=0
			--	VertDiffsecondaryOut=0
			
		end
		
	elseif	UsedPrimary > 0.08 and isSecondary ==true then		
		local duration=30
		if VertTickCount<duration then
			--local decrementVert = VertDiffLast / (duration)
			local decrementVertSecondary= VertDiffsecondaryLast/duration
			local decrementRot  = RotDiffLast / duration 
			VertTickCount=VertTickCount+1
			
			--VertDiffOut = VertDiffLast-decrementVert*VertTickCount
			VertDiffsecondaryOut= VertDiffsecondaryLast - decrementVertSecondary*VertTickCount
			RotDiffOut = RotDiffLast - decrementRot*VertTickCount

		elseif VertTickCount>=duration then
				--VertDiffOut=0 
				RotDiffOut=0
				VertDiffsecondaryOut=0
				--RotSecondaryExtraOut=0
		end
	end	
	
	if UsedPrimary<= 0.08 then
		RotSecondaryExtra = 3
		ExtraTickCount=0
	end
	if	UsedPrimary > 0.1 and isSecondary ==true then		
		local durationc=10
		if ExtraTickCount<durationc then
			--local decrementVert = VertDiffLast / (duration)
			local decrementVertSecondary= RotSecondaryExtra/durationc
			--local decrementRot  = RotDiffLast / duration 
			ExtraTickCount=ExtraTickCount+1
			
			--VertDiffOut = VertDiffLast-decrementVert*VertTickCount
			RotSecondaryExtraOut= RotSecondaryExtra - decrementVertSecondary*ExtraTickCount
			--RotDiffOut = RotDiffLast - decrementRot*VertTickCount

		elseif VertTickCount>=durationc then
				--VertDiffOut=0 
				--RotDiffOut=0
				RotSecondaryExtraOut=0
				--RotDiffOut=0
		end
	end	
	
	local finalRotSecondary= RotDiffOut+RotSecondaryExtraOut
	
	if isUpRecoilActive then
		if not isSecondary then
			local FinalAngle=tostring(PositiveIntegerMask(DefaultOffset)/1000000+RotDiffOut)
			uevr.params.vr.set_mod_value("VR_ControllerPitchOffset", FinalAngle)
		elseif isSecondary then
			local FinalAngle=tostring(PositiveIntegerMask(DefaultOffset)/1000000+finalRotSecondary)
			uevr.params.vr.set_mod_value("VR_ControllerPitchOffset", FinalAngle)
		end
	end
	--print(RotDiffOut)
	
	--if isUpRecoilActive then
	--UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentPrimary):set_location_offset(Vector3d.new(40 , 140 - VertDiffOut*0.8, 20))
	----print(VertDiffOut)
	--UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentSecondary):set_location_offset(Vector3d.new(40 , 140 - VertDiffsecondaryOut/5, 20))
	--elseif not isUpRecoilActive then
	--UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentPrimary):set_location_offset(Vector3d.new(-5 + Backrecoil, -5 , -1))
	--UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentSecondary):set_location_offset(Vector3d.new(-7.369999885559082 + BackrecoilSecondary, -6.860 , -1))
	--end
	
	
	PrimaryMesh= pawn.InventoryComp.SpawnedGear.Primary.ItemMesh
	update_weapon_offset( PrimaryMesh)
	
	
	
	
end)


uevr.sdk.callbacks.on_post_engine_tick(
	function(engine, delta)

	ResetTick=ResetTick+1
	
	
	
	if ResetTick==4 then
		ResetTick=0
		isFakeDiff=true
	elseif ResetTick~=0 then
		isFakeDiff=false
	end
	
	
	if isUpRecoilActive then
		if UsedPrimary <= 0.02 and RTrigger >=200  then
			uevr.params.vr.set_mod_value("VR_AimMethod" , "0")
			isShooting=true
			if ResetTick==0 then
				if isRhand then
				uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
				else
				uevr.params.vr.set_mod_value("VR_AimMethod" , "3")
				end
			end
			
		elseif UsedPrimary>0.2 then
			isShooting=false
			if isRhand then
				uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
				else
				uevr.params.vr.set_mod_value("VR_AimMethod" , "3")
			end
		end
	end
	
	
	local RotDiffPost=RotDiff

end)

end)
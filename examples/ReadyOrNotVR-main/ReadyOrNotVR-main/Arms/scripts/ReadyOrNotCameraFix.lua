
local api = uevr.api

  local pawn = api:get_local_pawn(0) 
  local camera_component_c = api:find_uobject("Class /Script/Engine.CameraComponent")
  local wpn_meshL = nil           --last equipped mesh name(class string)
  local wpn_meshL_ID =nil		  --last equipped mesh	(class mesh)
  local EquipState = 0
  local pawnMeshIsCrouched = false
--  local transform_c = api:find_uobject("ScriptStruct /Script/CoreUObject.Transform")
 -- local my_transform = StructObject.new(transform_c)
  local CrouchedHeightOffset = 35
  local StandingHeightOffset = 72.955
  local Optiwand_position = nil
  local OptiwandInUse = false
  local DefaultOffset= uevr.params.vr:get_mod_value("VR_ControllerPitchOffset")
  function PositiveIntegerMask(text)
	return text:gsub("[^%-%d]", "")
end
-- variables for math transform --
--function QuatToEuler(q1)
--	local SingTest= q1.x * q1.y + q1.z * q1.w
--	local e1={}
--	--e1.x= math.atan(0.2,0.1)
--	if SingTest>0.499 then -- singularty at north pole
--		e1.y=2*math.atan2(q1.x,q1.w)
--		e1.z=math.pi/2
--		e1.x=0
--		print("np")
--		
--	elseif SingTest<-0.499 then--singularity at southpole
--		e1.y=-2* math.atan2(q1.x,q1.w)
--		e1.z=-math.pi/2
--		e1.x=0
--		print("sp")
--	else
--		sqx = q1.x*q1.x
--		sqy =q1.y*q1.y
--		sqz =q1.z*q1.z
--		atn1=(2* q1.y * q1.w - 2* q1.x * q1.z )
--		atn2=(1 - 2*sqy - 2*sqz)
--		--print(atn1)
--		--print(atn2)
--		
--		e1.x= math.atan(2*q1.x*q1.w-2*q1.y*q1.z , 1 - 2*sqx - 2*sqz)---math.pi   --pitch
--		e1.y= -math.atan(atn1,atn2)									    --yaw
--		e1.z = -math.asin(2*SingTest)								    --roll
--		
--	end
--	return e1
--end
--
--local hmd_rotation = {}
--
--function EulerToQuad(e2)
--    --Assuming the angles are in radians.
--	--local e2= vec3.new(e2.x,e2.y,e2.z)
--	local c1 = math.cos(e2.y/2)
--	local s1 = math.sin(e2.y/2)
--	local c2 = math.cos(e2.x/2)
--	local s2 = math.sin(e2.x/2)
--	local c3 = math.cos(e2.z/2)
--	local s3 = math.sin(e2.z/2)
--	local c1c2 = c1*c2
--	local s1s2 = s1*s2
--	
--	
--    w =c1c2*c3 - s1s2*s3
--  	x =c1c2*s3 + s1s2*c3
--	y =s1*c2*c3 + c1*s2*s3
--	z =c1*s2*c3 - s1*c2*s3
--	
--	local Check= Vector4f.new(-w,-x,-y,-z)
--	return Check
--end


--callback loop based on input--


uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--Read Gamepad stick input 
	local ThumbLX = state.Gamepad.sThumbLX
	local ThumbLY = state.Gamepad.sThumbLY
	local ThumbRX = state.Gamepad.sThumbRX
	local ThumbRY = state.Gamepad.sThumbRY
	local RTrigger= state.Gamepad.bRightTrigger
	if ThumbRY >= -30000 then 
	state.Gamepad.sThumbRY=0
	end
	
	
	
end)
	
	

--callback based on View calculation tick--
local RotSave=0
local RotDiff=0
local RotationXStart=0
local RotationXCur=0
local LastTickRot=0
uevr.params.sdk.callbacks.on_early_calculate_stereo_view_offset(

function(device, view_index, world_to_meters, position, rotation, is_double)
--print(rotation.x)
	    

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
		
		if pawn ~= nil and OptiwandInUse==false then
			pawn_pos = pawn.RootComponent:K2_GetComponentLocation()
		--	print(pawn:get_fname():to_string())
			position.x = pawn_pos.x 
			position.y = pawn_pos.y -- +5
			if pawnMeshIsCrouched==true then
				position.z = pawn_pos.z + CrouchedHeightOffset
			elseif pawnMeshIsCrouched == false then
				position.z = pawn_pos.z + StandingHeightOffset
			end
		end
	

end)

--uevr.sdk.callbacks.on_post_calculate_stereo_view_offset(
--function(device, view_index, world_to_meters, position, rotation, is_double)
--uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
--
--end)

local UsedPrimary=1

--uevr.sdk.callbacks.on_xinput_set_state(
--function(retval, user_index, state)
--print(state.wRightMotorSpeed)
--	if UsedPrimary <= 0.04 then
--		state.wRightMotorSpeed 	= 0000
--		state.wLeftMotorSpeed 	= 0000
--	elseif UsedPrimary >0.04 then
--		state.wRightMotorSpeed = 0
--		state.wLeftMotorSpeed = 0
--	end
--end)
local LeftController=uevr.params.vr.get_left_joystick_source()
local RightController= uevr.params.vr.get_right_joystick_source()
--local VertTickCount=0
--local VertDiffLast=0
--local RotDiffLast=0
--local VertDiffsecondaryLast=0
--		
--local VertDiffOut =0
--local VertDiffPreTick=0
--local VertDiffsecondaryPreTick=0
--local RotDiffPretick=0
--
--local ResetTick=0
--local TT = 0
--callback always

uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)

--uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
--	if ResetTick==3 then
--		VertDiffPreTick=VertDiff
--	end
end)



uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)




--repawning just to be safe
		pawn = api:get_local_pawn(0)
		--end
	--	print(pawn:get_fname():to_string())																						--DEBUG1
	--	print(UEVR_UObjectHook.exists(pawn))-- d2
		local lplayer = api:get_player_controller(0)
--		UsedPrimary=pawn.InventoryComp.LastEquippedWeapon.HeatTime
		
		--HapticFeedback
		--external now
		-----------------------
--			local CurrentPrimary = pawn.InventoryComp.SpawnedGear.Primary.ItemMesh
--			local CurrentSecondary=pawn.InventoryComp.SpawnedGear.Secondary.ItemMesh
--			local VertDiff = math.tan(RotDiff*math.pi/180)* 25
--			local VertDiffsecondary= math.tan(RotDiff*math.pi/180)* 42
--			
--		
--		if UsedPrimary <= 0.17 and ResetTick<4 then
--			uevr.params.vr.set_mod_value("VR_AimMethod" , "0")
--			TrState=1
--			ResetTick=ResetTick + 1
--			
--			--VertDiffPreTick=VertDiff
--			VertDiffsecondaryPreTick=VertDiffsecondary
--			RotDiffPretick=RotDiff
--			
--		elseif UsedPrimary <= 0.17 and ResetTick == 4 then
--			uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
--			ResetTick=0
--			TrState=2
--		elseif UsedPrimary >0.17 then
--			TrState=0
--			uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
--			ResetTick=0
--		end
		
		--print(UsedPrimary)
		
--Uobject Weapon recoil offset
		
			
--			if TrState == 1 then
--				
--				VertDiffOut= VertDiffOut + VertDiff
--				VertDiffLast =VertDiffOut
--				
--				VertDiffsecondaryOut=VertDiffsecondaryOut+ VertDiffsecondary
--				VertDiffsecondaryLast=VertDiffsecondaryOut
--
--				
--				RotDiffOut = RotDiffOut + RotDiff
--				RotDiffLast = RotDiffOut
--
--				VertTickCount=0
--			elseif TrState==2 then
--				VertDiffOut=VertDiffOut+VertDiffPreTick
--				VertDiffLast=VertDiffOut
--				
--				VertDiffsecondaryOut=VertDiffsecondaryOut+VertDiffPreTick
--				VertDiffsecondaryLast=VertDiffsecondaryOut
--				
--				RotDiffOut=RotDiffOut+RotDiffPretick
--				RotDiffLast=RotDiffOut
--				
--			elseif TrState==0  then
--				--VertDiffOut=VertDiffLast
--				--VertDiffsecondary=VertDiffLastSecondary
--				--RotDiff= RotDiffLast
--			--elseif TrState==0 and CdState==1 then
--				local duration=20
--				if VertTickCount<duration then
--					local decrementVert = VertDiffLast / (duration)
--					local decrementVertSecondary= VertDiffsecondaryLast/duration
--					local decrementRot  = RotDiffLast / duration 
--					VertTickCount=VertTickCount+1
--					
--					VertDiffOut = VertDiffLast-decrementVert*VertTickCount
--					
--					RotDiffOut = RotDiffLast - decrementRot*VertTickCount
--					
--					VertDiffsecondaryOut= VertDiffsecondaryLast - decrementVertSecondary*VertTickCount
--					
--										
--				elseif VertTickCount>=duration then
--						VertDiffOut=0 
--						RotDiff=0
--						VertDiffsecondary=0
--				end
--			end
--			
--			
--			local FinalAngle=tostring(PositiveIntegerMask(DefaultOffset)/1000000+RotDiffOut)
--			uevr.params.vr.set_mod_value("VR_ControllerPitchOffset", FinalAngle)
--			--print(VertDiffLast.. "   ".. VertTickCount .."   " .. VertDiff .. "    ".. VertDiffOut )
--			UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentPrimary):set_location_offset(Vector3d.new(-5, -5 - VertDiffOut, -1))
--			UEVR_UObjectHook.get_or_add_motion_controller_state(CurrentSecondary):set_location_offset(Vector3d.new(-7.369999885559082, -6.860 - VertDiffsecondaryOut/2, -1))
			
			
			
--Uobject EquippedWeapon	
			local EquWpn = pawn.InventoryComp.SpawnedGear.LongTactical
					if EquWpn == nil then print ("equ is nil")
					elseif string.find(EquWpn:get_fname():to_string(),"Mirrorgun")then
					--debug3
		--			print("mirrorgun found")
						if EquWpn.bInUse then
							UEVR_UObjectHook.set_disabled(true)
							OptiwandInUse=true
			--				print("UEVR Off")
						else UEVR_UObjectHook.set_disabled(false)
				--			print("uevr on")
							OptiwandInUse=false
						end
					end
					--bInUse																					--declare Mesh class
																													--local MeshC= api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
																													--		--Debug4	
																													--		print(MeshC)	
--Find Mesh of Char0
			local pawnMesh= pawn.Mesh

			pawnMeshIsCrouched = pawnMesh.AnimScriptInstance.BaseCharacterRef.bIsCrouched
	

			
end)	
			
--check if item is equipped
	--		local isEquippedTrueFalse = EquWpn:IsEquipped()
	--		print(isEquippedTrueFalse)
			--local pawnLocation= pawn:K2_GetActorLocation()
			--print(pawnLocation.z)
	--		if isEquippedTrueFalse ==true and EquipState == 0 then
	--			--pawn:AddComponentByClass(camera_component_c, false, my_transform, false)
	--			--lplayer:call("SetViewTargetWithBlend", pawn, 0.5, 0, 0.5, false)
	--									--UEVR_UObjectHook.set_disabled(true)
	--									--for i, Wmesh in ipairs(mirrorgun_mesh) do
	--									--	if 	Wmesh:get_fname():to_string() == "ItemMesh" then
	--									--		Wcheck= Wmesh
	--									--		UEVR_UObjectHook.get_or_add_motion_controller_state(Wcheck)
	--									--		print(Wcheck)
	--									--	end
	--									--end
	--			--UEVR_UObjectHook.get_motion_controller_state(pawn):set_hand(2)						
	--			EquipState=1
	----			print(UEVR_UObjectHook.exists(pawn))
	--		elseif EquipState==1 and isEquippedTrueFalse ==true then
	--			EquipState=1
	--		else 
	--			UEVR_UObjectHook.set_disabled(false)
	--			EquipState=0	
	--		end
	--		print(EquipState)
	--		print(Wcheck)
--find all components of EquippedWeapon that are a skeleton mesh and puts it into array					
			--local mirrorgun = EquWpn:K2_GetComponentsByClass(MeshC)
            --print(skeletal_meshes)																			--debug4
			--
			--
--find mesh --class in EquippedWeapon array
			--for i, Wmesh in ipairs(skeletal_meshes) do
			--	if Wmesh:get_fname():to_string() == "WeaponMesh" then
--dummy vari--able, cant remember if needed xD				
			--		Wcheck= Wmesh
			--		print(Wcheck:GetOwner():get_fname():to_string())
			--		print("+")
			--		print(Wmesh:GetOwner():get_fname():to_string())
			--		print("+")
			--		print(wpn_meshL)
--compare cu--rrent weapon to last weapon equipped(last time loop was triggered)					
			--		if not (Wcheck:GetOwner():get_fname():to_string() == wpn_meshL) then
			--			--print("ha")
--if it´s no--t the first time then wont be nil and turns last equipped weapon invisible
			--			if wpn_meshL_ID ~= nil then
			--				wpn_meshL_ID:call("SetRenderInMainPass",(false))
			--			end
--overwrites-- last equipped weapon with current weapon for next loop						
			--			wpn_meshL = Wmesh:GetOwner():get_fname():to_string()
			--			wpn_meshL_ID=Wmesh
			--			print("new")
			--			print(wpn_meshL_ID:GetOwner():get_fname())
			--			print("found arms " .. Wmesh:get_full_name())
			--			print(Wmesh:GetOwner():get_fname())
			--			WpnName = Wmesh:GetOwner()
			--			
			--			
--applying i--ndividual location and rotation based on weapon name						
			--			
			--if string.find(WpnName:get_fname():to_string(), "Mirrorgun") then
			--print ("The word Mirrorgun was found.")
			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(18, 0, 0))
			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--elseif string.find(WpnName:get_fname():to_string(), "MosinNagantM38") then
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(18, 0, 0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--elseif string.find(WpnName:get_fname():to_string(), "PPSh41") then
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(18, 0, 0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--elseif string.find(WpnName:get_fname():to_string(), "DP27") then
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(18, 0, 0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--elseif string.find(WpnName:get_fname():to_string(), "G43") then
			--			--print ("The word G43 was found.")
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -8, 0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--elseif string.find(WpnName:get_fname():to_string(), "SVT40") then
			--			--print ("The word SVt was found.")
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -8, 45))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			--else
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -8, 0))
			--			--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
			--			end  
			--		end
			--	break
			--	end
			--end		

			
	--	end
	--end


--)
 
--fpvmesh:SetRenderInMainPass(false)

--local api = uevr.api
--
--local pawn = api:get_local_pawn(0)
--if pawn == nil then print("pawn is nil") end
--
--local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
--if skeletal_mesh_c == nil then print("skeletal_mesh_c is nil") end
--
--local skeletal_meshes = pawn:K2_GetComponentsByClass(skeletal_mesh_c)
--
--local guns_mesh = nil
--for i, mesh in ipairs(skeletal_meshes) do
--    if mesh:get_fname():to_string() == "ArmsMesh" then
--        guns_mesh = mesh
--        print("found arms " .. mesh:get_full_name())
--        break
--    end
--end

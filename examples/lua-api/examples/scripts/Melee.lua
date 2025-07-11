	--CONFIG
	require(".\\Subsystems\\UEHelper")
	require(".\\Config\\CONFIG")
	--local MeleePower = 500 --Default = 1000
	---------------------------------------
	local MeleeDistance=1 -- 30cm + MeleeDistance per meter,e.g. 30cm+ 0.4*1m = 70cm
	local api = uevr.api
	local vr = uevr.params.vr
	--local params = uevr.params
	local callbacks = uevr.params.sdk.callbacks
	local pawn = api:get_local_pawn(0)
	local lHand_Pos =UEVR_Vector3f.new()
	local lHand_Rot =UEVR_Quaternionf.new()
	local rHand_Pos =UEVR_Vector3f.new()
	local rHand_Rot =UEVR_Quaternionf.new()
	local rHand_Joy =UEVR_Vector2f.new()
	local PosZOld=0
	local PosYOld=0
	local PosXOld=0
	local PosZOldR=0
	local PosYOldR=0
	local PosXOldR=0
	local tickskip=0
	local PosDiff = 0
	local PosDiffR = 0
	
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

--Helper functions
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
local swinging_fast = nil

local melee_data = {
    right_hand_pos_raw = UEVR_Vector3f.new(),
    right_hand_q_raw = UEVR_Quaternionf.new(),
    right_hand_pos = Vector3f.new(0, 0, 0),
    last_right_hand_raw_pos = Vector3f.new(0, 0, 0),
    last_time_messed_with_attack_request = 0.0,
    first = true,
}

local isHit1=false
local isHit2=false
local isHit3=false
local isHit4=false
local isHit5=false



--Library
local kismet_system_library = find_static_class("Class /Script/Engine.KismetSystemLibrary")

local UGameplayStatics_library= find_static_class("Class /Script/Engine.GameplayStatics")
local game_engine_class = find_required_object("Class /Script/Engine.GameEngine")

local zero_color = nil
local color_c = find_required_object("ScriptStruct /Script/CoreUObject.LinearColor")
local    actor_c = find_required_object("Class /Script/Engine.Actor")
local zero_color = StructObject.new(color_c)
	
local hitresult_c = find_required_object("ScriptStruct /Script/Engine.HitResult")
local GameplayStaticsDefault= find_required_object("GameplayStatics /Script/Engine.Default__GameplayStatics")
local VWeaponClass= find_static_class("Class /Script/Altar.VWeapon")
local reusable_hit_result1 = StructObject.new(hitresult_c)
local reusable_hit_result2=  StructObject.new(hitresult_c)
local reusable_hit_result3 = StructObject.new(hitresult_c)
local reusable_hit_result4 = StructObject.new(hitresult_c)
local reusable_hit_result5 = StructObject.new(hitresult_c)

local DeltaCheck=0
local Mouse1=false
local DeltaAimMethod =0
local isAimMethodSwitched=false
local Hittest = nil

local isBlock=false
local DeltaBlock=0
local DeltaBlock2Activator=0
local DeltaBlockActivator=0
local ShieldAngle=0
local HeadAngle=0
local BoxX=10
local BoxZ=1
local AttackCount=0
local isAttacking=false
local BoxYLast=0
local BoxY=0
local tgm=false
local AttackDelta=0
uevr.sdk.callbacks.on_pre_engine_tick(
function(engine, delta)
--print(isRiding)
if not isRidng then
	DeltaCheck=DeltaCheck+delta
	--print(DeltaCheck)
	pawn = api:get_local_pawn(0)
	--print(isRiding)
	--local rHandIndex = uevr.params.vr.get_right_controller_index()
	uevr.params.vr.get_pose(2, lHand_Pos, lHand_Rot)
	
	local PosXNew=lHand_Pos.x
	local PosYNew=lHand_Pos.y
	local PosZNew=lHand_Pos.z
	
	PosDiff = math.sqrt((PosXNew-PosXOld)^2+(PosYNew-PosYOld)^3+(PosZNew-PosZOld)^2)*10000
	PosZOld=PosZNew
	PosYOld=PosYNew
	PosXOld=PosXNew
	
	uevr.params.vr.get_pose(1, rHand_Pos, rHand_Rot)
	local PosXNewR=rHand_Pos.x
	local PosYNewR=rHand_Pos.y
	local PosZNewR=rHand_Pos.z
	
	PosDiffR = math.sqrt((PosXNewR-PosXOldR)^2+(PosYNewR-PosYOldR)^3+(PosZNewR-PosZOldR)^2)*10000
	PosZOldR=PosZNewR
	PosYOldR=PosYNewR
	PosXOldR=PosXNewR
	--print(PosDiff)
--Praydog VARIUANT:	use swinging_fast
	--vr.get_pose(vr.get_right_controller_index(), melee_data.right_hand_pos_raw, melee_data.right_hand_q_raw)
	--
    ---- Copy without creating new userdata
    --melee_data.right_hand_pos:set(melee_data.right_hand_pos_raw.x, melee_data.right_hand_pos_raw.y, melee_data.right_hand_pos_raw.z)
	--
    --if melee_data.first then
    --    melee_data.last_right_hand_raw_pos:set(melee_data.right_hand_pos.x, melee_data.right_hand_pos.y, melee_data.right_hand_pos.z)
    --    melee_data.first = false
    --end
	--
    --local velocity = (melee_data.right_hand_pos - melee_data.last_right_hand_raw_pos) * (1 / delta)
	--
    ---- Clone without creating new userdata
    --melee_data.last_right_hand_raw_pos.x = melee_data.right_hand_pos_raw.x
    --melee_data.last_right_hand_raw_pos.y = melee_data.right_hand_pos_raw.y
    --melee_data.last_right_hand_raw_pos.z = melee_data.right_hand_pos_raw.z
    --melee_data.last_time_messed_with_attack_request = melee_data.last_time_messed_with_attack_request + delta


	--local vel_len = velocity:length()
	--    
	--if velocity.y < 0 then
	--swinging_fast = vel_len >= 2.5
	--end
--
	if not isRiding then
	pawn.CapsuleComponent.CapsuleRadius=10
	end
	local _class ={VWeaponClass}
	--print(pawn.WeaponsPairingComponent.WeaponActor:GetOverlappingComponents())
	local _actors = {}
	local _ShieldComp ={}
	---pcall(function()
	--pawn.WeaponsPairingComponent.WeaponActor.VHitBox:TriggerTrapBegin()
  if pawn.WeaponsPairingComponent.WeaponActor ~=nil then
	if pawn.WeaponsPairingComponent.WeaponActor.VHitBox ~=nil  then
		pawn.WeaponsPairingComponent.WeaponActor.VHitBox:SetCollisionEnabled(1)
		pawn.WeaponsPairingComponent.WeaponActor.VHitBox:SetCollisionObjectType(22)
		--print(pawn.Mesh.AnimScriptInstance.bAttackingRequest)
		if pawn.Mesh.AnimScriptInstance.bAttackingRequest then
			isAttacking=true
		end
		if isAttacking  then
			BoxX=40
			BoxZ=40
		--	print("yes")
			pawn.WeaponsPairingComponent.WeaponActor.VHitBox.RelativeLocation.X=-40
			AttackCount=AttackCount+delta
		else	
			pawn.WeaponsPairingComponent.WeaponActor.VHitBox.RelativeLocation.X=10+ExtraBlockRange
			BoxX=10+ExtraBlockRange
			BoxZ=1
		end
		if AttackCount>1 then
			AttackCount=0
			isAttacking=false
		end
			if pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.Y -BoxYLast==00 then 
				BoxY = pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.Y -00
			else BoxY=pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.Y end
			
				BoxYLast=BoxY
			

			
			 
			local BoxVector = Vector3f.new( BoxX, BoxY+00,BoxZ)
			
			pawn.WeaponsPairingComponent.WeaponActor.VHitBox:SetBoxExtent(BoxVector,true)
			--pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.X=80 
		--end
		--pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.Z=15
		--pawn.WeaponsPairingComponent.WeaponActor.VHitBox.BoxExtent.Y=50
		pawn.WeaponsPairingComponent.WeaponActor.VHitBox:GetOverlappingComponents(_actors)
	--	pawn.WeaponsPairingComponent.ShieldActor:GetOverlappingComponents(_ShieldComp)
	end
  end

	--print(_actors)
	--pawn.WeaponsPairingComponent.WeaponActor.VHitBox:SetRenderInMainPass(true)
	for i, comp in ipairs(_actors) do
		
	
			if not string.find(comp:GetOwner():get_fname():to_string(),"Player") then
				-- print(comp:GetOwner():get_fname():to_string())
				if comp:GetOwner():GetOwner()~=nil then
					if not string.find(comp:GetOwner():GetOwner():get_fname():to_string(),"Player") then	
						
						--print(comp:GetOwner():get_fname():to_string())
						if comp:GetOwner().bIsEquipped ~=nil and not string.find(comp:GetOwner():get_fname():to_string(),"Shield") then
							print(comp:GetOwner().bIsEquipped)--get_fname():to_string())
							if comp:GetOwner().bIsEquipped  then
					--print("Blocked")			
								--if not isAimMethodSwitched then 
									isBlock=true
									--pawn:SendAttack(3,5)
									pawn:SendBlockHit()
									DeltaBlock=0
									break
								--end
							end
						end
					end
			
				end
			end	
	end
	for i, comp in ipairs(_ShieldComp) do
		
	
			if not string.find(comp:GetOwner():get_fname():to_string(),"Player") then
				-- print(comp:GetOwner():get_fname():to_string())
				if comp:GetOwner():GetOwner()~=nil then
					if not string.find(comp:GetOwner():GetOwner():get_fname():to_string(),"Player") then	
						
					--	print(comp:GetOwner():get_fname():to_string())
						if comp:GetOwner().bIsEquipped ~=nil then
							print("Shield")--get_fname():to_string())
							if comp:GetOwner().bIsEquipped then
					--print("Blocked")			
								--if not isAimMethodSwitched then 
									isBlock=true
									pawn:SendBlockHit()
									break
								--end
							end
						end
					end
			
				end
			end	
	end
	
	--print(" ")
	--Hitscan
	local WeaponMesh=nil
	local PMesh=nil
	if pawn.WeaponsPairingComponent.WeaponActor ~=nil then
		if pawn.WeaponsPairingComponent.WeaponActor.MainStaticMeshComponent ~=nil then
			PMesh=	pawn.WeaponsPairingComponent.WeaponActor.MainStaticMeshComponent
			WeaponMesh=pawn.WeaponsPairingComponent.WeaponActor.MainStaticMeshComponent
		end
	end
	
	if left_hand_component:K2_GetComponentRotation().y <0 then 
		ShieldAngle=left_hand_component:K2_GetComponentRotation().y+360
		--print (left_hand_component:K2_GetComponentRotation().y+360)
	else ShieldAngle=left_hand_component:K2_GetComponentRotation().y
	--print(left_hand_component:K2_GetComponentRotation().y)
	end
	if hmd_component:K2_GetComponentRotation().y <0 then 
		HeadAngle=hmd_component:K2_GetComponentRotation().y+360
		--print (hmd_component:K2_GetComponentRotation().y+360)
	else HeadAngle=hmd_component:K2_GetComponentRotation().y
--	print(hmd_component:K2_GetComponentRotation().y)
	end
	--print(left_hand_component:K2_GetComponentRotation.y)
	if WeaponMesh ~=nil then--Weapon condition
		--local MeleeMesh= right_hand_component:K2_GetComponentLocation()--pawn.CurrentMeleeWeapon.StaticMesh
		local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)
		local viewport = game_engine.GameViewport
		local world = viewport.World
		local ignore2_actors = {}
		local Array_Objects ={}
		local Upvector=WeaponMesh:GetUpVector()
		local RightVector= WeaponMesh:GetRightVector()
	--	local MeshLocation1=WeaponMesh:K2_GetComponentLocation()+RightVector*20+WeaponMesh:GetForwardVector()*30
	--	local endPos1=MeshLocation1+(WeaponMesh:GetForwardVector())*8192
	--	local MeshLocation2=WeaponMesh:K2_GetComponentLocation()+RightVector*-20+WeaponMesh:GetForwardVector()*30
	--	local endPos2=MeshLocation2+(WeaponMesh:GetForwardVector())*8192
	--	local MeshLocation3=WeaponMesh:K2_GetComponentLocation()+Upvector*20+WeaponMesh:GetForwardVector()*30
	--	local endPos3=MeshLocation3+(WeaponMesh:GetForwardVector())*8192
	--	local MeshLocation4=WeaponMesh:K2_GetComponentLocation()+Upvector*-20+WeaponMesh:GetForwardVector()*30
	--	local endPos4=MeshLocation4+(WeaponMesh:GetForwardVector())*8192
		local MeshLocation5=WeaponMesh:K2_GetComponentLocation()+WeaponMesh:GetForwardVector()*30
		local endPos5=MeshLocation5+(WeaponMesh:GetForwardVector())*8192
	--	GameplayStaticsDefault:FindCollisionUV(reusable_hit_result1, 2,UV, true)
	--reusable_hit_result2.Actor= 
	--	local hit1 = kismet_system_library:LineTraceSingle(world, MeshLocation1, endPos1, 0, true, ignore2_actors, 0, reusable_hit_result1, true, zero_color, zero_color, 1.0)
	--	local hit2 = kismet_system_library:LineTraceSingle(world, MeshLocation2, endPos2,0, true, ignore2_actors, 0, reusable_hit_result2, true, zero_color, zero_color,  1.0)
	--	--local FHitRes= UGameplayStatics_library:BreakHitResult(reusable_hit_result2)
	--	local hit3 = kismet_system_library:LineTraceSingle(world, MeshLocation3, endPos3, 0, true, ignore2_actors, 0, reusable_hit_result3, true, zero_color, zero_color, 1.0)
	--	local hit4 = kismet_system_library:LineTraceSingle(world, MeshLocation4, endPos4, 0, true, ignore2_actors, 0, reusable_hit_result4, true, zero_color, zero_color, 1.0)
		local hit5 = kismet_system_library:LineTraceSingle(world, MeshLocation5, endPos5, 0, true, ignore2_actors, 0, reusable_hit_result5, true, zero_color, zero_color, 1.0)
	--	local hit6= world:LineTraceSingleByChannel()
	--	if hit1 and reusable_hit_result1.Distance < 100*MeleeDistance then
	--		isHit1=true
	--	else isHit1=false
	--	end
	--	
	--	if hit2 and reusable_hit_result2.Distance < 100*MeleeDistance then
	--		isHit2=true
	--	else isHit2=false
	--	end
	--	if hit3 and reusable_hit_result3.Distance < 100*MeleeDistance then
	--		isHit3=true
	--	else isHit3=false
	--	end
	--	if hit4 and reusable_hit_result4.Distance < 100*MeleeDistance then
	--		isHit4=true
	--	else isHit4=false
	--	end
		if hit5 and reusable_hit_result5.Distance < 50*MeleeDistance then
			isHit5=true
		else isHit5=false
		end
		--if reusable_hit_result2.Actor ~=nil then
	Hittest = reusable_hit_result2
--print(Hittest[8])
--print(Hittest[3])
--print(Hittest[4])
--print(Hittest[5])
--print(Hittest.HitObjectHandle)
--	print(reusable_hit_result2.Actor)
	--print(Hittest.Component)
--	print(Hittest.Item)
--	print(Hittest.bBlockingHit)
-- --print(api:get_item(Hittest.Item))
--	print(" ")
	--end
		
		--print(reusable_hit_result2.Distance)
		--print(isHit1)
		--print(isHit2)
		--print(isHit3)
		--print(isHit4)
		--print(isHit5)
		--print("  ")
	end
	if Mouse1==true then
		
		if DeltaCheck >=0.3 then
		--uevr.params.vr.set_mod_value("VR_AimMethod", "2")
		Mouse1 =false
		DeltaCheck=0
		--isHit1=false
		--	isHit2=false
		--	isHit3=false
		--	isHit4=false
		--	uevr.params.vr.set_mod_value("VR_AimMethod", "2")
		--uevr.params.vr.set_mod_value("VR_ControllerPitchOffset", "-21")
		end
	end

	--DeltaAimMethod=DeltaAimMethod+delta
if isBow ==false and isRiding==false then
uevr.params.vr.set_mod_value("VR_AimMethod", "2")
	--if isHit5  then
	--DeltaAimMethod=DeltaAimMethod+delta
	--	if DeltaAimMethod > 0.1 and isAimMethodSwitched==false then
	--	isAimMethodSwitched=true
	--	uevr.params.vr.set_mod_value("VR_AimMethod", "0")
	--	DeltaAimMethod=0
	--	end
	--end
	--if DeltaAimMethod>0.5 or PMesh.AnimScriptInstance.bAttackingRequest or PMesh.AnimScriptInstance.bCanExitAttack then
	--	uevr.params.vr.set_mod_value("VR_AimMethod", "2")
	--	isAimMethodSwitched=false
	--	
	--end
	if right_hand_component:K2_GetComponentRotation().z > -105 and right_hand_component:K2_GetComponentRotation().z<-75 and PosDiff<100 then
		DeltaBlockActivator=DeltaBlockActivator+delta
		if DeltaBlockActivator > 0.15 and PosDiff<100 then
			if SwordSidewaysIsBlock then
				isBlock=true
			end
		end
	else 
		DeltaBlockActivator=0
		
	end
	--print (ShieldAngle-HeadAngle)
	if ShieldAngle-HeadAngle > 60 and ShieldAngle-HeadAngle< 100 and pawn.WeaponsPairingComponent.ShieldActor ~=nil then
		DeltaBlock2Activator=DeltaBlock2Activator+delta
		if DeltaBlock2Activator > 0.15   then
		isBlock=true
		end
	elseif ShieldAngle-HeadAngle <-260 and ShieldAngle-HeadAngle >-295 and pawn.WeaponsPairingComponent.ShieldActor ~=nil then
		DeltaBlock2Activator=DeltaBlock2Activator+delta
		if DeltaBlock2Activator > 0.15 then
		isBlock=true
		end
	else 
		DeltaBlock2Activator=0		
	end
			
	
	if isBlock then
	--uevr.params.vr.set_mod_value("VR_AimMethod", "1")
	DeltaBlock=DeltaBlock+delta
		if DeltaBlock >1.15 then
			pawn:SendBlock(false)
		isBlock=false
		DeltaBlock=0
		
		end
	end
elseif isBow and RTrigger ~= 0 then uevr.params.vr.set_mod_value("VR_AimMethod", "0")
elseif isBow and RTrigger == 0 then uevr.params.vr.set_mod_value("VR_AimMethod", "0")

end
AttackDelta=AttackDelta+delta
else uevr.params.vr.set_mod_value("VR_AimMethod", "0") 
pawn.CapsuleComponent.CapsuleRadius=50
end
end)

local Prep=false
local PrepR=false

uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


--print(isBlock)
if isBlock and PosDiff<MeleePower then
	--print("trogger")
	state.Gamepad.bLeftTrigger=255
end
--local TriggerR = state.Gamepad.bRightTrigger
--if PosDiff >= MeleePower and Prep == false then
--	Prep=true
--elseif PosDiff <=150 and Prep ==true then
--	SendKeyDown('0x01')
--	Prep=false
--	Mouse1=true
--end
--state.Gamepad.bRightTrigger=0



--if PosDiffR >= MeleePower and PrepR == false then
--	Prep=true
--elseif PosDiffR <=10 and PrepR ==true then
--	state.Gamepad.bRightTrigger= 255
--	PrepR=false
--end	

--if isHit1 or isHit2 or isHit3 or isHit4 or isHit5 and Mouse1==false then

if isWeaponDrawn and isBow==false then
	if PosDiff >= MeleePower and Mouse1==false and AttackDelta>0.2 then
	
	AttackDelta=0
	--uevr.params.vr.set_mod_value("VR_AimMethod", "1")
		if state.Gamepad.bRightTrigger==255 then
			state.Gamepad.bRightTrigger=0
		elseif state.Gamepad.bRightTrigger==0 then
			state.Gamepad.bRightTrigger=255
		end
		DeltaCheck=0
		--isHit1=false
		--isHit2=false
		--isHit3=false
		--isHit4=false
		Mouse1=true
		Prep=false
		--print("Collision Hit")
	end

end





--Read Gamepad stick input for rotation compensation
	
	
 
	--testrotato.Y= CurrentPressAngle


		--RotationOffset
	--ConvertedAngle= kismet_math_library:Quat_MakeFromEuler(testrotato)
	--print("x: " .. ConvertedAngle.X .. "     y: ".. ConvertedAngle.Y .."     z: ".. ConvertedAngle.Z .. "     w: ".. ConvertedAngle.W)





end)
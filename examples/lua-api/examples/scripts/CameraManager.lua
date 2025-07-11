require(".\\Subsystems\\Trackers")
require(".\\Subsystems\\UEHelper")
require(".\\Config\\CONFIG")
local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks
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

local hitresult_c = find_required_object("ScriptStruct /Script/Engine.HitResult")
local reusable_hit_result1 = StructObject.new(hitresult_c)
local CamAngle=RightRotator
local AttackDelta=0
local HandVector= Vector3f.new(0.0,0.0,0.0)
local HmdVector = Vector3f.new(0.0,0.0,0.0)
local VecAlpha  = Vector3f.new(0,0,0)
local Alpha  	= nil
local AlphaDiff
local LastState= isBow
local ConditionChagned=false

uevr.sdk.callbacks.on_pre_engine_tick(
function(engine, delta)
--print(isRiding
if not isRiding then
	uevr.params.vr.set_mod_value("VR_RoomscaleMovement", "true")
	if isBow then
	
	uevr.params.vr.set_mod_value("VR_MovementOrientation", "0")
	
	local pawn = api:get_local_pawn(0)
	local player =api:get_player_controller(0)
	local CameraManager=player.PlayerCameraManager
	local CameraComp= CameraManager.ViewTarget.Target.CameraComponent
	
	
	--print(isBow)
	
	-- Diff_Rotator_LR
	if isBow and RTrigger ~= 0 then
		
			AttackDelta=0
			
		CamAngle=Diff_Rotator_LR
		
		
	end
	if RTrigger ==0 and AttackDelta < 2 then
		AttackDelta=AttackDelta+delta
	end 
	if isBow and RTrigger ==0 and AttackDelta> 1 then
		CamAngle=RightRotator
	elseif not isBow  and AttackDelta>1 then CamAngle=RightRotator
	elseif AttackDelta <= 1 then CamAngle=Diff_Rotator_LR
	end
	
	local CamPitch=-CamAngle.x
	local CamYaw=CamAngle.y+180
	if not isBow then
		CamYaw=CamAngle.y
		CamPitch=CamAngle.x
	end
	if isBow and RTrigger == 0 and AttackDelta>1 then
		CamYaw=CamAngle.y
		CamPitch=CamAngle.x
	elseif isBow and AttackDelta<=1 then 
		CamPitch=-CamAngle.x
		CamYaw=CamAngle.y+180
	end
	----pcall(function()
	player:ClientSetRotation(Vector3f.new(CamPitch,CamYaw,0),true)
	--end)
	
		HandVector= right_hand_component:GetForwardVector()
		HmdVector= hmd_component:GetForwardVector()
		--VecAlpha = (HandVector.x - HmdVector.x, HandVector.y - HmdVector.y, HandVector.z - HmdVector.z)
		local VecAlphaX= HandVector.x - HmdVector.x
		local VecAlphaY= HandVector.y - HmdVector.y
		local Alpha1
		local Alpha2
		if HandVector.x >=0 and HandVector.y>=0 then	
		Alpha1 =math.pi/2-math.asin( HandVector.x/ math.sqrt(HandVector.y^2+HandVector.x^2))
		--print("Quad1")
		elseif HandVector.x <0 and HandVector.y>=0 then
		--print("Quad2")
		Alpha1 =math.pi/2-math.asin( HandVector.x/ math.sqrt(HandVector.y^2+HandVector.x^2))
		elseif HandVector.x <0 and HandVector.y<0 then
		--print("Quad3")
		Alpha1 =math.pi+math.pi/2+math.asin( HandVector.x/ math.sqrt(HandVector.y^2+HandVector.x^2))
		elseif HandVector.x >=0 and HandVector.y<0 then
		--print("Quad4")
		Alpha1 =3/2*math.pi+math.asin( HandVector.x/ math.sqrt(HandVector.y^2+HandVector.x^2))
		end
		
		if HmdVector.x >=0 and HmdVector.y>=0 then	
		Alpha2 =math.pi/2-math.asin( HmdVector.x/ math.sqrt(HmdVector.y^2+HmdVector.x^2))
		--print("Quad1")
		elseif HmdVector.x <0 and HmdVector.y>=0 then
		--print("Quad2")
		Alpha2 =math.pi/2-math.asin( HmdVector.x/ math.sqrt(HmdVector.y^2+HmdVector.x^2))
		elseif HmdVector.x <0 and HmdVector.y<0 then
		--print("Quad3")
		Alpha2 =math.pi+math.pi/2+math.asin( HmdVector.x/ math.sqrt(HmdVector.y^2+HmdVector.x^2))
		elseif HmdVector.x >=0 and HmdVector.y<0 then
		--print("Quad4")
		Alpha2 =3/2*math.pi+math.asin( HmdVector.x/ math.sqrt(HmdVector.y^2+HmdVector.x^2))
		end
		
		
		AlphaDiff= Alpha2-Alpha1
		if isBow and RTrigger ~= 0 then
			AlphaDiff=AlphaDiff-math.pi*20/180
		end
	elseif HeadBasedMovement then uevr.params.vr.set_mod_value("VR_MovementOrientation", "1")
	elseif HeadBasedMovement==false then uevr.params.vr.set_mod_value("VR_MovementOrientation", "2")
	end
else uevr.params.vr.set_mod_value("VR_MovementOrientation", "0")
uevr.params.vr.set_mod_value("VR_RoomscaleMovement", "false")
 end

if LastState == not isBow then
	LastState=isBow
	ConditionChagned=true
	print("ConditionChagned")
end
end)

local DecoupledYawCurrentRot = 0
local RXState=0
local SnapAngle
 



uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)
if isBow  then

--Read Gamepad stick input for rotation compensation
if HeadBasedMovement then
	state.Gamepad.sThumbLX= ThumbLX*math.cos(-AlphaDiff)- ThumbLY*math.sin(-AlphaDiff)
			
	state.Gamepad.sThumbLY= math.sin(-AlphaDiff)*ThumbLX + ThumbLY*math.cos(-AlphaDiff)
end



	SnapAngle = PositiveIntegerMask(uevr.params.vr:get_mod_value("VR_SnapturnTurnAngle"))
	if SnapTurn then
		if ThumbRX >200 and RXState ==0 then
			DecoupledYawCurrentRot=DecoupledYawCurrentRot + SnapAngle
			RXState=1
		elseif ThumbRX <-200 and RXState ==0 then
			DecoupledYawCurrentRot=DecoupledYawCurrentRot - SnapAngle
			RXState=1
		elseif ThumbRX <= 200 and ThumbRX >=-200 then
			RXState=0
		end
 
	
	else
		
		SmoothTurnRate = PositiveIntegerMask(uevr.params.vr:get_mod_value("VR_SnapturnTurnAngle"))/90
	
	
		local rate = state.Gamepad.sThumbRX/32767
					rate =  rate*rate*rate
		if ThumbRX >2200  then
			DecoupledYawCurrentRot=DecoupledYawCurrentRot + SmoothTurnRate * rate
			
		elseif ThumbRX <-2200  then
			DecoupledYawCurrentRot=DecoupledYawCurrentRot + SmoothTurnRate * rate
		
		end
	end

end
end)



local PreRot
local DiffRot
local DecoupledYawCurrentRotLast=0
uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
PreRot=rotation.y
DiffRot= HmdRotator.y - RightRotator.y

if isBow  then	
	rotation.y = DecoupledYawCurrentRot
	
	if ConditionChagned then
	 ConditionChagned=false
	-- vr.recenter_view()
	-- rotation.y=DecoupledYawCurrentRotLast	
	end
	--vr.recenter_view()
else 
	if ConditionChagned then
	local player =api:get_player_controller(0)
	--rotation.y=DecoupledYawCurrentRotLast
	player:ClientSetRotation(Vector3f.new(HmdRotator.x , DecoupledYawCurrentRotLast	,0),true)
	ConditionChagned=false
	vr.recenter_view()
	end
	----rotation.y=DecoupledYawCurrentRotLast
	DecoupledYawCurrentRot=HmdRotator.y - DiffRot
end
end)

uevr.sdk.callbacks.on_post_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)
	--print(DecoupledYawCurrentRot)
local pawn=api:get_local_pawn(0)
if isRiding and FirstPersonRiding then
	pawn.Rider.Mesh:SetVisibility(false,true)
	
	NewLoc=pawn.Rider.HeadwearChildActorComponent.ChildActor.RootComponent:K2_GetComponentLocation()
	--pawn:GetForwardVector()
--	NewRot=pawn:GetActorRotation()
	position.x=NewLoc.x --+pawn.Mesh:GetForwardVector().x*10
	position.y=NewLoc.y --pawn.Mesh:GetForwardVector().y*10
	position.z=NewLoc.z +140
	--rotation=NewRot
else
	--pawn.Mesh:SetVisibility(false,true)
end
	
if isBow  then	
	DecoupledYawCurrentRotLast=rotation.y	
else-- if ConditionChagned then
	--print("ok2")
	-- ConditionChagned=false
	---- vr.recenter_view()
	-- rotation.y=DecoupledYawCurrentRotLast	
	--end
end
end)
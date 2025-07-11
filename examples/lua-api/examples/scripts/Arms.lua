--require(".\\Config\\CONFIG")
require(".\\Subsystems\\Trackers")
require(".\\Subsystems\\UEHelper")
local api = uevr.api
local params = uevr.params
local callbacks = params.sdk.callbacks
local vr=uevr.params.vr

local function find_required_object(name)
	local obj = uevr.api:find_uobject(name)
	if not obj then
		print("Cannot find " .. name)
		return nil
	end

	return obj
end
local find_static_class = function(name)
    local c = find_required_object(name)
    return c:get_class_default_object()
end

local testfind= find_static_class("Class /Script/Engine.PoseableMeshComponent")

local pawn = api:get_local_pawn(0)
local lossy_offset= Vector3f.new(0,math.pi/2,0)
local glove_mesh = nil
local master_mat = nil
local has_found_overlays = false
local can_disable_overlays = false
local goggles = nil
local masks = nil

local last_level = nil
local kismet_math_library = find_static_class("Class /Script/Engine.KismetMathLibrary")

local loadout = nil
local in_loadout = false
local check_refill = false
local last_refill = false
local might_be_refilling = false
local ArrowMeshOld=nil



local function ResetShotArrows()
	if ArrowMeshOld~=nil then
		if ArrowMeshOld~=ArrowMesh then
		-- print(ArrowMeshOld:GetOwner():get_fname():to_string())
		end
	end
end

local function update_weapon_offset(Body_mesh)
    if not Body_mesh then print("nil") return end
	
   -- local attach_socket_name = weapon_mesh.AttachSocketName
	local PairingComponent= Body_mesh
	local WeaponMesh=nil
	local ShieldMesh=nil
	local ArrowMesh=nil
	local BowMesh=nil
	local QuiverMesh=nil
	--x=pitch 
	
	ResetShotArrows()
	if not isRiding then
		if PairingComponent.WeaponActor~=nil then
			if PairingComponent.WeaponActor.MainStaticMeshComponent ~=nil then
				if PairingComponent.WeaponActor.MainStaticMeshComponent.AttachSocketName:to_string()=="Weapon_Socket" then
				WeaponMesh=PairingComponent.WeaponActor.MainStaticMeshComponent
				UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_hand(1)
				UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_rotation_offset(Vector3f.new(0,-30/190*math.pi,-math.pi/2))
				UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_location_offset(Vector3f.new(0.5,0,1.5))
				--print("foundWeapon")
					if isWeaponDrawn then 
						WeaponMesh:SetVisibility(true)
						UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_permanent(true)
					else WeaponMesh:SetVisibility(false)
						UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_permanent(false)
					end
					--UEVR_UObjectHook.get_or_add_motion_controller_state(WeaponMesh):set_permanent(true)
				end
				if PairingComponent.WeaponActor.ScabbardMeshComponent ~= nil then
					ScabbardMesh=PairingComponent.WeaponActor.ScabbardMeshComponent
				--if isWeaponDrawn then
					ScabbardMesh:SetVisibility(false)
				end
			end	
			if PairingComponent.WeaponActor.MainSkeletalMeshComponent ~=nil then
				if PairingComponent.WeaponActor.MainSkeletalMeshComponent.AttachSocketName:to_string()== "Torch_Socket" then
					BowMesh=PairingComponent.WeaponActor.MainSkeletalMeshComponent
		--			print("foundShield")
					UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_hand(0)
					if isWeaponDrawn then
						BowMesh:SetVisibility(true)
						
					else BowMesh:SetVisibility(false) 
					end
					
					if RTrigger >0 then
						UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_rotation_offset(Vector3f.new(Diff_Rotator_LR.y/180*math.pi-LeftRotator.y/180*math.pi,-Diff_Rotator_LR.x/180*math.pi-LeftRotator.x/180*math.pi+math.pi,math.pi/2))--math.pi,math.pi,0))
						UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_location_offset(Vector3f.new(0,-3.4,1))
					else
						UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_rotation_offset(Vector3f.new(0,0,math.pi/2))
						UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_location_offset(Vector3f.new(0,3.4,1))
					end
					UEVR_UObjectHook.get_or_add_motion_controller_state(BowMesh):set_permanent(false)
				end
				
			end
		end
		if PairingComponent.ShieldActor~=nil then
			if PairingComponent.ShieldActor.MainStaticMeshComponent.AttachSocketName:to_string()=="Shield_Socket" then
				ShieldMesh=PairingComponent.ShieldActor.MainStaticMeshComponent
					if isWeaponDrawn and not isBow then
						ShieldMesh:SetVisibility(true, true)
						--ShieldMesh:SetRenderInMainPass(true)
					else ShieldMesh:SetVisibility(false, true)
						--ShieldMesh:SetRenderInMainPass(false)
					end
					if isBow then ShieldMesh:SetVisibility(false, true) end
		--		print("foundShield")
				UEVR_UObjectHook.get_or_add_motion_controller_state(ShieldMesh):set_hand(0)
				UEVR_UObjectHook.get_or_add_motion_controller_state(ShieldMesh):set_rotation_offset(Vector3f.new(math.pi,math.pi,0))
				UEVR_UObjectHook.get_or_add_motion_controller_state(ShieldMesh):set_location_offset(Vector3f.new(2,-6,-30))
			end
				if isWeaponDrawn and not isBow then
						ShieldMesh:SetVisibility(true, true)
						--ShieldMesh:SetRenderInMainPass(true)
					else ShieldMesh:SetVisibility(false, true)
						--ShieldMesh:SetRenderInMainPass(false)
					end
					if isBow then ShieldMesh:SetVisibility(false, true) end
		end
		--print(isBow)
			
		if PairingComponent.QuiverActor~=nil then --string.find(Mesh:get_fname():to_string(),"Drawn Arrow") then
			if PairingComponent.QuiverActor.MainStaticMeshComponent.AttachSocketName:to_string()=="Quiver_Socket" then
				QuiverMesh= PairingComponent.QuiverActor.MainStaticMeshComponent
				QuiverMesh:SetVisibility(false,true)
			end
		end
		if pawn.DrawnArrowMeshComponent~=nil then --string.find(Mesh:get_fname():to_string(),"Drawn Arrow") then
		ArrowMesh=pawn.DrawnArrowMeshComponent
			--if pawn.DrawnArrowMeshComponent.AttachSocketName:to_string()=="Quiver_Socket" then
				UEVR_UObjectHook.get_or_add_motion_controller_state(ArrowMesh):set_hand(1)
				if isBow then
				UEVR_UObjectHook.get_or_add_motion_controller_state(ArrowMesh):set_rotation_offset(Vector3d.new(Diff_Rotator_LR.x/180*math.pi+RightRotator.x/180*math.pi,-Diff_Rotator_LR.y/180*math.pi+RightRotator.y/180*math.pi+math.pi,RightRotator.z/180*math.pi))
				UEVR_UObjectHook.get_or_add_motion_controller_state(ArrowMesh):set_location_offset(Vector3f.new(0,0,67))
				UEVR_UObjectHook.get_or_add_motion_controller_state(ArrowMesh):set_permanent(true)
				else
				UEVR_UObjectHook.get_or_add_motion_controller_state(ArrowMesh):set_permanent(false)
				end
			--end	
		end
	
	
		--if VisibleHelmet then
		--	if pawn.HeadwearChildActorComponent.ChildActor.StaticMesh~=nil or pawn.HeadwearChildActorComponent.ChildActor.RootSkeletalMeshComponent ~=nil then
		--		pawn.HeadwearChildActorComponent:SetVisibility(true)
		--		pcall(function()
		--		pawn.HeadwearChildActorComponent.ChildActor.StaticMesh:SetVisibility(true)
		--		end)
		--		pawn.HeadwearChildActorComponent.ChildActor.RootSkeletalMeshComponent:SetVisibility(true)
		--	end
		--end
		ArrowMeshOld=ArrowMesh
	end
	--print(Diff_Rotator_LR.x)
	--	
	
end

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
--print(testfind:get_fname())
 pawn = api:get_local_pawn(0)
 local BodyMesh=pawn.WeaponsPairingComponent
 update_weapon_offset(BodyMesh)
end)
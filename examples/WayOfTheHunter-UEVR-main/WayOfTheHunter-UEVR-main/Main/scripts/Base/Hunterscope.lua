require(".\\Base\\Subsystems\\Trackers")
require(".\\Config\\CONFIG")
require(".\\Base\\Subsystems\\UEHelper")

local api = uevr.api
local vr = uevr.params.vr

local emissive_material_amplifier = 2.0 
local fov = 18.0

-- Static variables
local emissive_mesh_material_name = "Material /Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"
local reticle_mesh_material_name = "Material /Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"--"MaterialInstanceConstant /Game/TEH/Gear/Sights/Scope01/PPI_Scope01_Sight.PPI_Scope01_Sight"
local sightTexture_name = "Texture2D /Game/TEH/Gear/Sights/Scope01/T_Scope01_Reticle_KEX.T_Scope01_Reticle_KEX"
local sightTexture_name2= "Texture2D /Game/TEH/Gear/Sights/Scope02/T_Scope02_Reticle_KEX.T_Scope02_Reticle_KEX"
local sightTexture_name3= "Texture2D /Game/TEH/Gear/Sights/Scope03/T_Scope03_Reticle_KEX.T_Scope03_Reticle_KEX"
local sightTexture_name4= "Texture2D /Game/TEH/Gear/Sights/Scope04/T_Scope04_Reticle_KEX.T_Scope04_Reticle_KEX"
local sightTexture_name5= "Texture2D /Game/TEH/Gear/Sights/Scope05/T_Scope05_Reticle_KEX.T_Scope05_Reticle_KEX"
local sightTexture_name6= "Texture2D /Game/TEH/Gear/Sights/Scope06/T_Scope06_Reticle_KEX.T_Scope06_Reticle_KEX"
local CurrentScope=nil
local dynamic_materialReticle=nil
local ftransform_c = nil
local flinearColor_c = nil
local hitresult_c = nil
local game_engine_class = nil
local Statics = nil
local Kismet = nil
local KismetMaterialLibrary = nil
local AssetRegistryHelpers = nil
local actor_c = nil
local staic_mesh_component_c = nil
local staic_mesh_c = nil
local scene_capture_component_c = nil
local MeshC = nil
local StaticMeshC = nil
local CameraManager_c = nil


-- Instance variables
local scope_actor = nil
local scope_plane_component = nil
local reticle_plane_component= nil
local scene_capture_component = nil
local render_target = nil
local reusable_hit_result = nil
local temp_vec3 = Vector3d.new(0, 0, 0)
local temp_vec3f = Vector3f.new(0, 0, 0)
local zero_color = nil
local zero_transform = nil


local wanted_tex=nil

local function find_required_object(name)
    local obj = uevr.api:find_uobject(name)
    if not obj then
        error("Cannot find " .. name)
        return nil
    end
    return obj
end

local function find_required_object_no_cache(class, full_name)
    local matches = class:get_objects_matching(false)
    for i, obj in ipairs(matches) do
        if obj ~= nil and obj:get_full_name() == full_name then
            return obj
        end
    end
    return nil
end

local find_static_class = function(name)
    local c = find_required_object(name)
    return c:get_class_default_object()
end

local function init_static_objects()
    -- Try to initialize all required objects
	sightTexture_name = "Texture2D /Game/TEH/Gear/Sights/Scope01/T_Scope01_Reticle_KEX.T_Scope01_Reticle_KEX"
	
    ftransform_c = find_required_object("ScriptStruct /Script/CoreUObject.Transform")
    if not ftransform_c then return false end
    
    flinearColor_c = find_required_object("ScriptStruct /Script/CoreUObject.LinearColor")
    if not flinearColor_c then return false end
    
    hitresult_c = find_required_object("ScriptStruct /Script/Engine.HitResult")
    if not hitresult_c then return false end
    
    game_engine_class = find_required_object("Class /Script/Engine.GameEngine")
    if not game_engine_class then return false end
    
    Statics = find_static_class("Class /Script/Engine.GameplayStatics")
    if not Statics then return false end
    
    Kismet = find_static_class("Class /Script/Engine.KismetRenderingLibrary")
    if not Kismet then return false end
    
    KismetMaterialLibrary = find_static_class("Class /Script/Engine.KismetMaterialLibrary")
    if not KismetMaterialLibrary then return false end
    
    AssetRegistryHelpers = find_static_class("Class /Script/AssetRegistry.AssetRegistryHelpers")
    if not AssetRegistryHelpers then return false end
    
    actor_c = find_required_object("Class /Script/Engine.Actor")
    if not actor_c then return false end
    
    staic_mesh_component_c = find_required_object("Class /Script/Engine.StaticMeshComponent")
    if not staic_mesh_component_c then 
	print("false")
	return false end

    staic_mesh_c = find_required_object("Class /Script/Engine.StaticMesh")
    if not staic_mesh_c then return false end
    
    scene_capture_component_c = find_required_object("Class /Script/Engine.SceneCaptureComponent2D")
    if not scene_capture_component_c then return false end
    
    MeshC = api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
    if not MeshC then return false end
    
    StaticMeshC = api:find_uobject("Class /Script/Engine.StaticMeshComponent")
    if not StaticMeshC then return false end

    CameraManager_c = find_required_object("Class /Script/Engine.PlayerCameraManager")
    if not CameraManager_c then return false end

    -- Initialize reusable objects
    reusable_hit_result = StructObject.new(hitresult_c)
    if not reusable_hit_result then return false end
    
    zero_color = StructObject.new(flinearColor_c)
    if not zero_color then return false end
    
    zero_transform = StructObject.new(ftransform_c)
    if not zero_transform then return false end
    zero_transform.Rotation.W = 1.0
    zero_transform.Scale3D = temp_vec3:set(1.0, 1.0, 1.0)

    return true
end

local function reset_static_objects()
    ftransform_c = nil
    flinearColor_c = nil
    hitresult_c = nil
    game_engine_class = nil
    Statics = nil
    Kismet = nil
    KismetMaterialLibrary = nil
    AssetRegistryHelpers = nil
    actor_c = nil
    staic_mesh_component_c = nil
    staic_mesh_c = nil
    scene_capture_component_c = nil
    MeshC = nil
    StaticMeshC = nil
    CameraManager_c = nil

    
    reusable_hit_result = nil
    zero_color = nil
    zero_transform = nil
end

local function validate_object(object)
    if object == nil or not UEVR_UObjectHook.exists(object) then
        return nil
    else
        return object
    end
end

local function destroy_actor(actor)
    if actor ~= nil and not UEVR_UObjectHook.exists(actor) then
        pcall(function() 
            if actor.K2_DestroyActor ~= nil then
                actor:K2_DestroyActor()
            end
        end)
    end
    return nil
end


local function spawn_actor(world_context, actor_class, location, collision_method, owner)

    local actor = Statics:BeginDeferredActorSpawnFromClass(world_context, actor_class, zero_transform, collision_method, owner)

    if actor == nil then
        print("Failed to spawn actor")
        return nil
    end

    Statics:FinishSpawningActor(actor, zero_transform)
    print("Spawned actor")

    return actor
end

local function get_scope_mesh(parent_mesh)
    if not parent_mesh then return nil end

    local child_components = parent_mesh.AttachChildren
    if not child_components then return nil end

    for _, component in ipairs(child_components) do
        if component:is_a(StaticMeshC) and string.find(component:get_fname():to_string(), "scope") then
            return component
        end
    end

    return nil
end
local function Get_Scope_Object(parent_Obj)
	if not parent_Obj then return nil end
	local child_Obj_array= parent_Obj.m_attachments
	local CurrentScopeObj= SearchSubObjectArrayForObject(child_Obj_array, "Scope") 
	if CurrentScopeObj== nil then
		CurrentScopeObj= SearchSubObjectArrayForObject(child_Obj_array, "Collimator")
	end
	if CurrentScopeObj~=nil then
		return CurrentScopeObj
	end
end



local function get_equipped_weapon(pawn)
    if not pawn then return nil end
    local sk_mesh = pawn.Mesh
    if not sk_mesh then return nil end
    local anim_instance = sk_mesh.AnimScriptInstance
    if not anim_instance then return nil end
    local weapon_mesh = anim_instance.WeaponData.WeaponMesh
    return weapon_mesh
end

local function get_render_target(world)
    render_target = validate_object(render_target)
    if render_target == nil then
        render_target = Kismet:CreateRenderTarget2D(world, 1024, 1024, 6, zero_color, false)
    end
    return render_target
end

local function spawn_scope_plane(world, owner, pos, rt)
    local local_scope_mesh = scope_actor:AddComponentByClass(staic_mesh_component_c, false, zero_transform, false)
    if local_scope_mesh == nil then
        print("Failed to spawn scope mesh")
        return
    end

    local wanted_mat = api:find_uobject(emissive_mesh_material_name)
    if wanted_mat == nil then
        print("Failed to find material")
        return
    end
    wanted_mat:set_property("BlendMode", 0)
	wanted_mat:set_property("TwoSided", false)
	wanted_mat:set_property("ShadingModel", 0 )
         wanted_mat.bDisableDepthTest = true
    --     --wanted_mat.MaterialDomain = 0
    --     --wanted_mat.ShadingModel = 0

    local plane = find_required_object_no_cache(staic_mesh_c, "StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
    -- local plane = find_required_object("StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
    -- local plane = find_required_object_no_cache("StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")

    if plane == nil then
        print("Failed to find plane mesh")
        api:dispatch_custom_event("LoadAsset", "StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
        return
    end
    local_scope_mesh:SetStaticMesh(plane)
    local_scope_mesh:SetVisibility(false)
    -- local_scope_mesh:SetHiddenInGame(false)
    local_scope_mesh:SetCollisionEnabled(0)

    local dynamic_material = local_scope_mesh:CreateDynamicMaterialInstance(0, wanted_mat, "ScopeMaterial")

    dynamic_material:SetTextureParameterValue("LinearColor", rt)
    local color = StructObject.new(flinearColor_c)
    color.R = emissive_material_amplifier
    color.G = emissive_material_amplifier
    color.B = emissive_material_amplifier
    color.A = emissive_material_amplifier
    dynamic_material:SetVectorParameterValue("Color", color)
    scope_plane_component = local_scope_mesh
end

local function spawn_reticle_plane(world, owner, pos, tex)
    local local_reticle_mesh = scope_actor:AddComponentByClass(staic_mesh_component_c, false, zero_transform, false)
	if local_reticle_mesh == nil then
        print("Failed to spawn local_reticle_mesh")
        return
    end

    local wanted_mat = api:find_uobject(reticle_mesh_material_name)
    if wanted_mat == nil then
        print("Failed to find Reticle material")
        return
    end
    wanted_mat:set_property("BlendMode", 3)
	wanted_mat:set_property("TwoSided", false)
    --     wanted_mat.bDisableDepthTest = true
    --     --wanted_mat.MaterialDomain = 0
    --wanted_mat:set_property("ShadingModel", 0 )
	--wanted_mat:set_property("MaterialDomain", 0 )

    local plane = find_required_object_no_cache(staic_mesh_c, "StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
    -- local plane = find_required_object("StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
    -- local plane = find_required_object_no_cache("StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")

    if plane == nil then
        print("Failed to find plane mesh")
        api:dispatch_custom_event("LoadAsset", "StaticMesh /Engine/BasicShapes/Cylinder.Cylinder")
        return
    end
    local_reticle_mesh:SetStaticMesh(plane)
    local_reticle_mesh:SetVisibility(false)
    -- local_scope_mesh:SetHiddenInGame(false)
    local_reticle_mesh:SetCollisionEnabled(0)

		
    dynamic_materialReticle = local_reticle_mesh:CreateDynamicMaterialInstance(0, wanted_mat, "ReticleMaterial")
	--local dynamic_texture = wanted_tex:CreateTransient(100,100

	CurrentScope=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm())
		pcall(function()
		if string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope01") or string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Collimator01") then
			wanted_tex= api:find_uobject(sightTexture_name)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope02") then
			wanted_tex= api:find_uobject(sightTexture_name2)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope03") then
			wanted_tex= api:find_uobject(sightTexture_name3)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope04") then
			wanted_tex= api:find_uobject(sightTexture_name4)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope05") then
			wanted_tex= api:find_uobject(sightTexture_name5)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope06") then
			wanted_tex= api:find_uobject(sightTexture_name6)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		end
		end)
		if wanted_tex == nil then
        print("Failed to find Reticle Texture2D")
        return
	end
   -- dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
	--dynamic_material.Opacity:SetTextureParameterValue("LinearColor", wanted_tex)
    local color = StructObject.new(flinearColor_c)
    color.R = 2
    color.G = 2
    color.B = 2
    color.A = 2
    dynamic_materialReticle:SetVectorParameterValue("Color", color)
    reticle_plane_component = local_reticle_mesh
end
-- local function create_emissive_mat(component, materialSocketName)
--     -- local wanted_mat = api:find_uobject(emissive_mesh_material_name)
--     -- if wanted_mat == nil then
--     --     print("Failed to find material")
--     --     return
--     -- end
--     -- wanted_mat.BlendMode = 0
--     -- wanted_mat.TwoSided = 1
--     local index = component:GetMaterialIndex(materialSocketName)
--     -- local dynamic_material = component:CreateDynamicMaterialInstance(index, wanted_mat, "ScopeMaterial")
--     local materials = component:GetMaterials()
--     local materal = materials[index]
--     materal:SetTextureParameterValue("SightMask ", render_target)
--     material.ShadingModel = 0
--     material.BlendMode = 0
--     -- dynamic_material:SetTextureParameterValue("LinearColor", render_target)
-- end


local function spawn_scene_capture_component(world, owner, pos, fov, rt)
    scene_capture_component = scope_actor:AddComponentByClass(scene_capture_component_c, false, zero_transform, false)
    if scene_capture_component == nil then
        print("Failed to spawn scene capture")
        return
    end
    scene_capture_component.TextureTarget = rt
    scene_capture_component.FOVAngle = fov
    scene_capture_component:SetVisibility(false)
end

local function spawn_scope(game_engine, pawn)
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

    if not pawn then
        print("pawn is nil")
        return
    end

    local rt = get_render_target(world)

    if rt == nil then
        print("Failed to get render target destroying actors")
        rt = nil
        scope_actor = destroy_actor(scope_actor)
        scope_plane_component = nil
        scene_capture_component = nil
		reticle_plane_component = nil
        return
    end

    local pawn_pos = pawn:K2_GetActorLocation()
    if not validate_object(scope_actor) then
        scope_actor = destroy_actor(scope_actor)
        scope_plane_component = nil
        scene_capture_component = nil
		reticle_plane_component = nil
        scope_actor = spawn_actor(world, actor_c, temp_vec3:set(0, 0, 0), 1, nil)
        if scope_actor == nil then
            print("Failed to spawn scope actor")
            return
        end
    end

    if not validate_object(scope_plane_component) then
        print("scope_plane_component is invalid -- recreating")
        spawn_scope_plane(world, nil, pawn_pos, rt)
		--spawn_reticle_plane(world, nil, pawn_pos, sightTexture_name)
    end
	if not validate_object(reticle_plane_component) then
        print("reticle_plane_component is invalid -- recreating")
        spawn_reticle_plane(world, nil, pawn_pos, sightTexture_name)
    end
    if not validate_object(scene_capture_component) then
        print("spawn_scene_capture_component is invalid -- recreating")
        spawn_scene_capture_component(world, nil, pawn_pos, fov, rt)
    end

end


local scope_mesh = nil
local last_scope_state = false


local function attach_components_to_weapon(weapon_mesh)
    if not weapon_mesh then return end
	print("Attach new")
	if not string.find(api:get_local_pawn(0).m_currentEquipment:get_fname():to_string(),"Bino") and not string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Collimator")  then
	
		-- Attach scene capture to weapon
		if scene_capture_component ~= nil then
			-- scene_capture:DetachFromParent(true, false)
			-- "AimSocket"
			print("Attaching scene_capture_component to weapon:" .. weapon_mesh:get_fname():to_string())
			scene_capture_component:K2_AttachToComponent(
				weapon_mesh,
				"Muzzle",
				2, -- Location rule
				2, -- Rotation rule
				0, -- Scale rule
				true -- Weld simulated bodies
			)
			scene_capture_component:K2_SetRelativeRotation(temp_vec3:set(0, 0, 90), false, reusable_hit_result, false)
			scene_capture_component:SetVisibility(false)
		end
		
		-- Attach plane to weapon
		if scope_plane_component then
			
		scope_mesh = Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).FP_Scope--get_scope_mesh(weapon_mesh)
			if scope_mesh == nil then
				print("Failed to find scope mesh")
				return
			end
			-- OpticCutoutSocket
			scope_plane_component:K2_AttachToComponent(
				scope_mesh,
				"AimSocket",
				2, -- Location rule
				2, -- Rotation rule
				2, -- Scale rule
				true -- Weld simulated bodies
			)
			scope_plane_component:K2_SetRelativeRotation(temp_vec3:set(0, 90, 90), false, reusable_hit_result, false)
			scope_plane_component:K2_SetRelativeLocation(temp_vec3:set(-14.22, 0,0 ), false, reusable_hit_result, false)
			scope_plane_component:SetWorldScale3D(temp_vec3:set(0.03,0.03, 0.00001))
			scope_plane_component:SetVisibility(false)
		end
		if reticle_plane_component then
			scope_mesh = Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).FP_Scope--get_scope_mesh(weapon_mesh)
			if scope_mesh == nil then
				print("Failed to find scope mesh")
				return
			end
			-- OpticCutoutSocket
			reticle_plane_component:K2_AttachToComponent(
				scope_mesh,
				"AimSocket",
				2, -- Location rule
				2, -- Rotation rule
				2, -- Scale rule
				true -- Weld simulated bodies
			)
			reticle_plane_component:K2_SetRelativeRotation(temp_vec3:set(0, 90, 90), false, reusable_hit_result, false)
			reticle_plane_component:K2_SetRelativeLocation(temp_vec3:set(-14.24, 0, 0), false, reusable_hit_result, false)
			reticle_plane_component:SetWorldScale3D(temp_vec3:set(0.03,0.03, 0.00001))
			reticle_plane_component:SetVisibility(false)
		end
	elseif string.find(api:get_local_pawn(0).m_currentEquipment:get_fname():to_string(),"Bino") then
		if scene_capture_component ~= nil then
			-- scene_capture:DetachFromParent(true, false)
			-- "AimSocket"
			print("Attaching scene_capture_component to weapon:" .. weapon_mesh:get_fname():to_string())
			scene_capture_component:K2_AttachToComponent(
				weapon_mesh,
				"Root",
				2, -- Location rule
				2, -- Rotation rule
				0, -- Scale rule
				true -- Weld simulated bodies
			)
			scene_capture_component:K2_SetRelativeRotation(temp_vec3:set(0, 0, 90), false, reusable_hit_result, false)
			scene_capture_component:SetVisibility(false)
			scene_capture_component.FOVAngle = fov
		end
		
		-- Attach plane to weapon
		if scope_plane_component then
			
		scope_mesh = api:get_local_pawn(0).m_currentEquipment.m_meshFirstPerson --get_scope_mesh(weapon_mesh)
			if scope_mesh == nil then
				print("Failed to find scope mesh")
				return
			end
			-- OpticCutoutSocket
			scope_plane_component:K2_AttachToComponent(
				scope_mesh,
				"Root",
				2, -- Location rule
				2, -- Rotation rule
				2, -- Scale rule
				true -- Weld simulated bodies
			)
			scope_plane_component:K2_SetRelativeRotation(temp_vec3:set(0, 90, 90), false, reusable_hit_result, false)
			scope_plane_component:K2_SetRelativeLocation(temp_vec3:set(-5.22, 0,0 ), false, reusable_hit_result, false)
			scope_plane_component:SetWorldScale3D(temp_vec3:set(0.12,0.12, 0.00001))
			scope_plane_component:SetVisibility(false)
		end
	elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Collimator") then	
		print("found Collimator")
		if reticle_plane_component then
			scope_mesh = Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).FP_Scope--get_scope_mesh(weapon_mesh)
			if scope_mesh == nil then
				print("Failed to find scope mesh")
				return
			end
			-- OpticCutoutSocket
			reticle_plane_component:K2_AttachToComponent(
				scope_mesh,
				"AimSocket",
				2, -- Location rule
				2, -- Rotation rule
				2, -- Scale rule
				true -- Weld simulated bodies
			)
			local Start_range=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_rangeDistance
			reticle_plane_component:K2_SetRelativeRotation(temp_vec3:set(0, 90, 90), false, reusable_hit_result, false)
			reticle_plane_component:K2_SetRelativeLocation(temp_vec3:set(Start_range, 0, 1.5), false, reusable_hit_result, false)
			reticle_plane_component:SetWorldScale3D(temp_vec3:set(0.03*Start_range/100,0.03*Start_range/100, 0.00001))
			reticle_plane_component:SetVisibility(false)
		end		
	end
end

local function is_scope_active(pawn)
    if not pawn then return false end
    local optical_scope = true--pawn.PlayerOpticScopeComponent
    if not optical_scope then return false end
    local scope_active = 0--optical_scope:read_byte(0xA8, 1)--
    if pawn.m_adsBlendAlpha ~=0 then
		scope_active=1
	end
	if scope_active > 0 then
        return true
    end
    return false
end

local function switch_scope_state(pawn)
    current_scope_state = is_scope_active(pawn)
    -- if current_scope_state == last_scope_state then
    --     return
    -- end
    last_scope_state = current_scope_state
    if scope_plane_component ~= nil then
        scope_plane_component:SetVisibility(current_scope_state)
    end
    if scene_capture_component ~= nil then
        scene_capture_component:SetVisibility(current_scope_state)
    end
	if reticle_plane_component ~=nil then
		reticle_plane_component:SetVisibility(current_scope_state)
	--	pawn:GetCurrentArm().m_attachments[2].FP_Lens_Back:SetVisibility(not current_scope_state)
	end
	
end

local function UpdateReticleTexture()
	if Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()) ~= CurrentScope then
		CurrentScope=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm())
		print(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string().." equipped")
		if string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope01") then
			wanted_tex= api:find_uobject(sightTexture_name)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope02") then
			wanted_tex= api:find_uobject(sightTexture_name2)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope03") then
			wanted_tex= api:find_uobject(sightTexture_name3)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope04") then
			wanted_tex= api:find_uobject(sightTexture_name4)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope05") then
			wanted_tex= api:find_uobject(sightTexture_name5)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		elseif string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Scope06") then
			wanted_tex= api:find_uobject(sightTexture_name6)
			dynamic_materialReticle:SetTextureParameterValue("LinearColor", wanted_tex)
		end
	end
	
end
-- Initialize static objects when the script loads
if not init_static_objects() then
    print("Failed to initialize static objects")
end

local current_weapon = nil
local last_level = nil
local weapon_mesh=nil
local weapon_Obj=nil

--ReDo proper FOV CHANGE based on eye to scope distance
local function Get_ScopeHmdDistance()
	local scope_plane_position = scope_plane_component:K2_GetComponentLocation()
	local hmdPos = hmd_component:K2_GetComponentLocation()
	local Diff= math.sqrt((hmdPos.x-scope_plane_position.x)^2+(hmdPos.y-scope_plane_position.y)^2+(hmdPos.z-scope_plane_position.z)^2)
	--if Diff <=2.5 then
	--	Diff=2.5
	--end
	return Diff
end
local function Get_CurrentScopeFOV(c_pawn)
	local CurrentFOVVal
	if c_pawn ~=nil then
		if	c_pawn:GetCurrentArm() ~=nil then		
				CurrentFOVVal=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).CurrentFOV
		end
	end
	return CurrentFOVVal
end

local function Recalculate_FOV(c_pawn)	
	if Get_ScopeHmdDistance()>=5.5 then
		--pcall(function()
		fov= 30*(Get_CurrentScopeFOV(c_pawn)* (2* math.atan(2.5/Get_ScopeHmdDistance())/(90/180*math.pi)))/94	
		--end)
	else 
	--pcall(function()
		fov= 30*(Get_CurrentScopeFOV(c_pawn)* (2* math.atan(2.5/Get_ScopeHmdDistance())/(90/180*math.pi)))/(94-(5.5-Get_ScopeHmdDistance())*3^2.7)	
	--end)
	end
		--print(Get_ScopeHmdDistance())
		scene_capture_component.FOVAngle = fov
end

local function AdjustSceneComponentAngle(c_pawn)
	local ReturnAngle
	if c_pawn ~=nil and not string.find(Get_Scope_Object(c_pawn:GetCurrentArm()):get_fname():to_string(),"Collimator")then
		if	c_pawn:GetCurrentArm() ~=nil then		
				local RearsightX=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_rearFixedPointOffset.X
				local FrontsightX=Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_frontFixedPointOffset.X
				local RearZ = Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_rearFixedPointOffset.z 
				local FrontZ= Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_frontFixedPointOffset.z
				ReturnAngle=math.atan((RearZ-FrontZ)/(RearsightX-FrontsightX))
		end
	end
	scene_capture_component:K2_SetRelativeRotation(temp_vec3:set(ReturnAngle*180/math.pi, 0, 90), false, reusable_hit_result, false)
end

local function UpdateReticleDistance(c_pawn)
	pcall(function()
	if string.find(Get_Scope_Object(c_pawn:GetCurrentArm()):get_fname():to_string(),"Collimator") then
		local range= Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_rangeDistance
		reticle_plane_component:K2_SetRelativeLocation(temp_vec3:set(range, 0, 1.5), false, reusable_hit_result, false)
		reticle_plane_component:SetWorldScale3D(temp_vec3:set(0.03*range/100,0.03*range/100, 0.00001))
	end
	end)
end

local function UpdateReticleVisibility()
pcall(function()
	if current_scope_state == true and string.find(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string(),"Collimator") then
		local HmdPos= hmd_component:K2_GetComponentLocation()
		local ReticlePos= reticle_plane_component:K2_GetComponentLocation()
		local ScopePos=  Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).FP_Scope:K2_GetComponentLocation()
			--ScopePos.y=ScopePos.y-1.5
		local Vec1TempX=ReticlePos.x-HmdPos.x
		local Vec1TempY=ReticlePos.y-HmdPos.y
		local Vec1TempZ=ReticlePos.z-HmdPos.z
		
		local Vec2TempX=ReticlePos.x- ScopePos.x 
		local Vec2TempY=ReticlePos.y- ScopePos.y 
		local Vec2TempZ=ReticlePos.z- ScopePos.z 
		
		local ScalarTemp= Vec1TempX*Vec2TempX + Vec1TempY*Vec2TempY + Vec1TempZ*Vec2TempZ
		local LFactor= math.sqrt(Vec1TempX^2 +Vec1TempY^2+ Vec1TempZ^2)* math.sqrt(Vec2TempX^2+ Vec2TempY^2+ Vec2TempZ^2)
		local Alpha= math.acos(ScalarTemp/LFactor)
		local Range= Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()).m_rangeDistance
		local AlphaMax= 0.0020/(Range/2500) -- 2/180*math.pi --math.atan(2.5/ Range)
		local AlphaMin= 0.0014/(Range/2500)
		
		print("Alpha: ".. Alpha .. "   AlphaMax: " .. AlphaMax)
		if math.abs(Alpha)<= AlphaMax and math.abs(Alpha)>= AlphaMin then
			reticle_plane_component:SetVisibility(true)
		elseif math.abs(Alpha)> AlphaMax  or math.abs(Alpha)< AlphaMin 	then
			reticle_plane_component:SetVisibility(false)
		end
	end
end)
	if isDriving then
		pcall(function()
		reticle_plane_component:SetVisibility(false)
		end)
	end
end
		
local PreUnequipWpn=nil

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
	
        local viewport = engine.GameViewport
        if viewport then
            local world = viewport.World
    
            if world then
                local level = world.PersistentLevel
    
                if last_level ~= level then
                    print("Level changed .. Reseting")
                    destroy_actor(scope_actor)
                    scope_plane_component = nil
                    scene_capture_component = nil
                    render_target = nil
                    scope_mesh = nil
                    reset_static_objects()
                    init_static_objects()
                end
                last_level = level
            end
        end
	
        -- reset_scope_actor_if_deleted()
        local c_pawn = api:get_local_pawn(0)
		
		weapon_mesh=nil
        weapon_Obj = c_pawn.m_currentEquipment --:GetCurrentArm()--get_equipped_weapon(c_pawn)
		
		
		if weapon_Obj ~= nil then
			weapon_mesh=weapon_Obj.m_meshFirstPerson
			weapon_mesh_attach=weapon_Obj.m_mesh
		end
	--	pcall(function()
	--	print("wpn "..weapon_Obj:get_fname():to_string())
	--	
	--	end)
	--
	--	pcall(function()
	--		print("curr "..current_Obj:get_fname():to_string())
	--	end)
	--	pcall(function()
	--	print("pre "..PreUnequipWpn:get_fname():to_string())
	--	end)
	--	print("   ")
		
		if weapon_Obj ~= current_Obj and weapon_Obj ~=nil then
		pcall(function()
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_weapon_attach):set_hand(2)
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_weapon_attach):set_location_offset(Vector3f.new (-100.7699999809265137,-8.020000457763672,-111.579999923706055))
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_weapon_attach):set_permanent(false)
		end)
		elseif weapon_Obj==nil then
		pcall(function()
				UEVR_UObjectHook.get_or_add_motion_controller_state(PreUnequipWpn.m_mesh):set_hand(2)
				UEVR_UObjectHook.get_or_add_motion_controller_state(PreUnequipWpn.m_mesh):set_location_offset(Vector3f.new (-100.7699999809265137,-8.020000457763672,-111.579999923706055))
				UEVR_UObjectHook.get_or_add_motion_controller_state(PreUnequipWpn.m_mesh):set_permanent(false)
		end)		
		end
		if weapon_Obj ~=nil then
		pcall(function()

		
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_location_offset(Vector3f.new (-0.7699999809265137,-8.020000457763672,17.579999923706055))
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_permanent(true)
	if string.find(weapon_Obj:get_fname():to_string(), "Bino") then
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_hand(0)
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_rotation_offset(Vector3f.new (0,0,0))
		if current_scope_state then
			uevr.params.vr.set_mod_value("VR_AimMethod" , "3")
		else
			uevr.params.vr.set_mod_value("VR_AimMethod" , "2")
		end
	else
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_hand(1)
		UEVR_UObjectHook.get_or_add_motion_controller_state(weapon_mesh_attach):set_rotation_offset(Vector3f.new (0,0,0))
		uevr. qparams.vr.set_mod_value("VR_AimMethod" , "2")
	end
	
		end)
		end
        if weapon_mesh then
            -- fix_materials(weapon_mesh)
            local weapon_changed = not current_weapon or weapon_mesh.AnimScriptInstance ~= current_weapon.AnimScriptInstance
            local scope_changed = (not scope_mesh or not scope_mesh.AttachParent) and is_scope_active(c_pawn)
            if weapon_mesh~= current_weapon then -- weapon_changed or scope_changed then
                print("Weapon changed")
                print("Previous weapon: " .. (current_weapon and current_weapon:get_fname():to_string() or "none"))
                print("New weapon: " .. weapon_mesh:get_fname():to_string())
                
		
                -- Update current weapon reference
				--pcall(function()
				PreUnequipWpn=current_Obj
				current_weapon_attach=weapon_mesh_attach
				current_weapon = weapon_mesh
				current_Obj=weapon_Obj
				
			--	end)
		
               
                -- Attempt to attach components
                spawn_scope(engine, c_pawn)
                attach_components_to_weapon(weapon_mesh)
            end
        else
            -- Weapon was removed/unequipped
            if current_weapon then
                print("Weapon unequipped")
                current_weapon = nil
                scope_mesh = nil
                last_scope_state = false
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_Obj.m_mesh):set_hand(2)
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_Obj.m_mesh):set_location_offset(Vector3f.new (-100.7699999809265137,-8.020000457763672,-111.579999923706055))
				UEVR_UObjectHook.get_or_add_motion_controller_state(current_Obj.m_mesh):set_permanent(false)
            end
        end
	
        switch_scope_state(c_pawn)
		pcall(function()
			Recalculate_FOV(c_pawn)
		end)
		pcall(function()
			AdjustSceneComponentAngle(c_pawn)
		end)
		pcall(function()
		UpdateReticleTexture()
		end)
		UpdateReticleVisibility()
		UpdateReticleDistance(c_pawn)
		--print(Get_Scope_Object(api:get_local_pawn(0):GetCurrentArm()):get_fname():to_string())
	--	fov= 1/(0.2*((c_pawn:GetCurrentArm().m_attachments[2].ZoomLevelIndex)+1))
	--	
	--	scene_capture_component.FOVAngle = fov
	
	if isDriving==false then
		pcall(function()
		if c_pawn:IsHoldingBreath() then
			uevr.params.vr.set_mod_value("UObjectHook_AttachLerpSpeed" , "2.000000")
		else 
			uevr.params.vr.set_mod_value("UObjectHook_AttachLerpSpeed" , "15.000000")
		end
		end)
	end
	
	
	
end)


uevr.sdk.callbacks.on_script_reset(function()
    print("Resetting")
    destroy_actor(scope_actor)
    scope_plane_component = nil
    scene_capture_component = nil
    render_target = nil
    scope_mesh = nil
    reset_static_objects()
end)

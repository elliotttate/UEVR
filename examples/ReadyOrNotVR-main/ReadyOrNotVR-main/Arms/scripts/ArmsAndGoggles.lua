require("CONFIG")
--local hide_arms = true
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

local pawn = api:get_local_pawn(0)
local lossy_offset=Vector3f.new(0,math.pi/2,0)
local glove_mesh = nil
local master_mat = nil

local last_level = nil
local UserWidget_c = find_required_object("Class /Script/UMG.UserWidget")

local function get_arm_material()
	local pawn = api:get_local_pawn()

	if pawn == nil then
		return
	end

	local customization_fp_meshes = pawn.CustomizationFirstPersonMeshes
	
	if customization_fp_meshes ~= nil then
		for _, mesh in ipairs(customization_fp_meshes) do
			if string.find(mesh:get_full_name(), "Glove") then
				glove_mesh = mesh
			end
		end
	end
	
	local arm_materials = glove_mesh.OverrideMaterials

	if arm_materials == nil or #arm_materials == 0 then
        return nil
    end

	for _, material in ipairs(arm_materials) do
		local mat_parent = material.Parent
		if string.find(mat_parent:get_full_name(), "Arms_Light") then
			master_mat = mat_parent.Parent
			return master_mat
		end
	end

	return nil
end

local matching_widgets = {}

local function find_goggles_widget()
	local pawn = api:get_local_pawn(0)
	
	if UserWidget_c ~= nil then
		local widgets = UserWidget_c:get_objects_matching(false)

		if widgets == nil or #widgets == 0 then
			return nil
		end

		for _, widget in ipairs(widgets) do
			if string.find(widget:get_full_name(), "Goggles") and widget:GetOwningPlayerPawn() == pawn then
				print(widget:get_full_name())
				table.insert(matching_widgets, widget)
			end
		end

		return matching_widgets
	end

	return nil
end

local goggles_disabled = false

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
	local game_engine_class = api:find_uobject("Class /Script/Engine.GameEngine")
    local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

    local viewport = game_engine.GameViewport
	
    if viewport == nil then
        return
    end

    local world = viewport.World

    if world == nil then
        return
    end

    local level = world.PersistentLevel

    if level == nil then
        return
    end

    if level ~= last_level then
		goggles_disabled = false
	end

	last_level = level

	pawn = api:get_local_pawn(0)

	if pawn == nil then
		return
	end

    local CurrentPitch=pawn.Mesh1P.AnimScriptInstance.AnimGraphNode_PivotBone.Rotation.Roll
	local CurrentRoll =pawn.Mesh1P.AnimScriptInstance.AnimGraphNode_PivotBone_2.Rotation.Pitch
	lossy_offset.x=-CurrentPitch*math.pi/180
	lossy_offset.z=CurrentRoll*math.pi/180
	UEVR_UObjectHook.get_or_add_motion_controller_state(pawn.Mesh1P):set_rotation_offset(lossy_offset)
	

	--Remove goggle overlay
	--if goggles_disabled == false then
	--	local goggles = find_goggles_widget()
	--
	--	if goggles ~= nil then
	--		for _, goggle in ipairs(goggles) do
	--			goggle:SetVisibility(1)
	--			goggles_disabled = true
	--		end
	--	end
	--end

	--Remove arms if true
	if hide_arms then
		local arm_mat = get_arm_material()
		if arm_mat ~= nil then
			arm_mat.BlendMode = 2
		end
	end
end)
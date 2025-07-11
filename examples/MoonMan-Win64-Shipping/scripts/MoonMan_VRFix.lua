local api = uevr.api
local vr = uevr.params.vr
local params = uevr.params
local callbacks = params.sdk.callbacks

local FLOATING_HANDS = true

local mesh = false
local Fpmesh = false
local Astromesh = false
local Oxygenmesh = false
local Widgetmesh = false
local Flashlightmesh = false
local Powertoolmesh = false
local normal_operation = false
local astrotool_active = false
local IsMainMenu = false
local IsInspecting = false
local weaponConnected = false
local ase_mode = false
local IsPanning = false
local IsCinematic = false
local SuitUnlocked  = false
local Climbing = false
local IsViewTarget = false
local ActorEnableCollision = false
local Gravity = false
local UsingAstrotool = false
local menu_debounce = 0
local force_astro = false
local pan_scanning_enabled = false
local attempt_ase_mode = false

local uevrUtils = require("libs/uevr_utils")
uevrUtils.setLogLevel(LogLevel.Debug)
uevrUtils.initUEVR(uevr)
local flickerFixer = require("libs/flicker_fixer")
local controllers = require("libs/controllers")
local animation = require("libs/animation")
local hands = require("libs/hands")


-- Create left and right hands
local handParams = 
{
	Arms = 
	{
		Left = 
		{
			Name = "LowerArm_L", -- Replace this with your findings from Step 1
			Rotation = {360, 450, -135},	-- Replace this with your findings from Step 7
			Location = {1.6, -22.2, 3.6},	-- Replace this with your findings from Step 7
			Scale = {1, 1, 1},			
			AnimationID = "left_hand"
		},
		Right = 
		{
			Name = "LowerArm_R", -- Replace this with your findings from Step 1
			Rotation = {0, 270, 90},	-- Replace this with your findings from Step 7
			Location = {-2.4, -33.0, 1.4},	-- Replace this with your findings from Step 7		
			Scale = {1, 1, 1},			
			AnimationID = "right_hand"
		}
	}
}

-- Attach AstroTool PDA to the left arm
function attachWeaponToController()
	
	local mesh = pawn.AstroToolHolder
	
	if mesh ~= nil then
		mesh:SetVisibility(true, false)
		mesh:SetHiddenInGame(false, true)
		weaponConnected = controllers.attachComponentToController(0, mesh)
		uevrUtils.set_component_relative_transform(mesh, {X=4,Y=-2.3,Z=2.6}, {Pitch=0,Yaw=-0,Roll=-123})
	end

end

-- Check if camera has been hijacked outside of cutscenes
function isInCinematic()
    local val = false
    local list = uevrUtils.find_all_instances("BlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/Interactables/Activatables/BPC_Activatable_Cinematic.BPC_Activatable_Cinematic_C")
	
	if list ~= nil then
	
		for _, object in pairs(list) do
			if object.bControlled == true then    
				val = true
			end    
		end
	end
	
    return val
end


function on_level_change(level)
	-- print("Level changed\n")
	if FLOATING_HANDS == true then
		flickerFixer.create()
		controllers.onLevelChange()
		controllers.createController(0)
		controllers.createController(1)
		controllers.createController(2)
		hands.reset()
	end
end

-- Create left and right hands for interfacing with the AstroTool PDA during normal gameplay
function on_lazy_poll()
	if FLOATING_HANDS == true then

		if IsMainMenu == false and IsCinematic == false and IsPanning == false and Climbing == false and ase_mode == false and IsViewTarget == true and SuitUnlocked == true and attempt_ase_mode == false and Gravity == true then
			if not hands.exists() then			
					hands.create(pawn.Mesh, handParams)
					attachWeaponToController()	
			end	
		elseif UsingAstrotool == true or force_astro == true then
			if not hands.exists() then			
					hands.create(pawn.Mesh, handParams)
					attachWeaponToController()	
			end			
		else
			if hands.exists() then
			--hands.hideHands(true) -- Use destroyHands as hideHands causes infinite loading screens
			hands.destroyHands()
			hands.reset()
			end
		end		
		
	end

end


uevr.sdk.callbacks.on_early_calculate_stereo_view_offset(function(device, view_index, world_to_meters, position, rotation, is_double)

	local pawn = api:get_local_pawn()

	if pawn == nil then
		return
	end
		
	local pawn_pos = nil
	
	pawn_pos = pawn.RootComponent:K2_GetComponentLocation()
		
	if string.find(tostring(pawn:get_full_name()), "BP_Astronaut_Frozen_C") then
		-- Do Nothing
	elseif  string.find(tostring(pawn:get_full_name()), "BP_ASE_C") then	
		-- Do Nothing
	elseif UsingAstrotool == true then
		if FLOATING_HANDS == true then
			position.x = pawn_pos.x
			position.y = pawn_pos.y
			position.z = pawn_pos.z + 80.0 -- Fix camera to the pawn to prevent player from getting rocketed out of map
		end
	elseif normal_mode == true then
		position.x = pawn_pos.x
		position.y = pawn_pos.y
		position.z = pawn_pos.z + 80.0 -- Fix camera to the pawn to prevent the arm from moving the camera
	end

end)


uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)

    local game_engine_class = api:find_uobject("Class /Script/Engine.GameEngine")
    local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

    local viewport = game_engine.GameViewport
    if viewport == nil then
        --print("Viewport is nil")
        return
    end
    local world = viewport.World

    if world == nil then
        --print("World is nil")
        return
    end

    if world ~= last_world then
        --print("World changed")
    end

    last_world = world

    local level = world.PersistentLevel

    if level == nil then
        --print("Level is nil")
        return
    end
	
	--print("Level name: " .. level:get_full_name())
	
	if string.find(tostring(level:get_full_name()), "LaunchSite_GB2") then
		pan_scanning_enabled = true -- Only check if camera has been hijacked in the first map, polling other levels causes the game to crash
	else
		pan_scanning_enabled = false
	end
    	
    local pawn = api:get_local_pawn(0)	
	
	-- When menu attack counter reaches zero, we know player has exited menu
	if menu_debounce ~= 0 then
	    menu_debounce = menu_debounce-1
	    IsMainMenu = true
	else
		IsMainMenu = false	
	end
	
    if string.find(tostring(pawn:get_full_name()), "BP_Astronaut_Frozen_C") then
		vr.set_mod_value("VR_MotionControlsInactivityTimer", "9999.000000")
	
		vr.set_mod_value("VR_AimMethod", "0")
		vr.set_mod_value("VR_RoomscaleMovement", "0")
		vr.set_mod_value("VR_DecoupledPitch", "0")
		vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
		
		vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
		vr.set_mod_value("VR_CameraRightOffset", "0.000000")
		vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
		vr.set_mod_value("VR_LerpCameraYaw", "false")
		vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
		
		local mesh = pawn.Mesh	
		mesh:SetRenderInMainPass(false)
		mesh:SetRenderCustomDepth(false)
		
		ase_mode = false
		astrotool_active = false
		normal_operation = false
	
	elseif string.find(tostring(pawn:get_full_name()), "BP_ASE_C") then
		vr.set_mod_value("VR_MotionControlsInactivityTimer", "9999.000000")
				
		vr.set_mod_value("VR_AimMethod", "0")
		vr.set_mod_value("VR_RoomscaleMovement", "0")
		vr.set_mod_value("VR_DecoupledPitch", "0")
		vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
		
		vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
		vr.set_mod_value("VR_CameraRightOffset", "0.000000")
		vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
		vr.set_mod_value("VR_LerpCameraYaw", "false")
		vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
		
		local pawn = api:get_local_pawn()
		
		--FPMesh
		local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
		if skeletal_mesh_c ~= nil then
			local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
			for i, mesh in ipairs(skeletal_meshes) do
				if mesh:get_fname():to_string() == "Helmet_FP" then
					Fpmesh = mesh
					--print(tostring(Fpmesh:get_full_name()))						
					break
				end
			end
		end						
					
		--AstroToolMesh
		local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
		if skeletal_mesh_c ~= nil then
			local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
			for i, mesh in ipairs(skeletal_meshes) do
				if mesh:get_fname():to_string() == "BP_AstroTool" then
					Astromesh = mesh						
					break
				end
			end
		end		

		--Hide meshes when flying the robot
		if Fpmesh ~= nil then
			Fpmesh:SetVisibility(false)
		end
					
		if Astromesh ~= nil then
			Astromesh:SetVisibility(false)
		end		
		
		ase_mode = true
		astrotool_active = false
		normal_operation = false		
		
		if hands.exists() then
			hands.destroyHands()
			hands.reset()
		end					

		--print("ASE mode")				
    else
        if pawn ~= nil then
		
			local pawn = api:get_local_pawn()
			local mesh = pawn.Mesh
		
			ase_mode = false
			
			--FPMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "Helmet_FP" then
						Fpmesh = mesh
						--print(tostring(Fpmesh:get_full_name()))						
						break
					end
				end
			end				
			
			--AstroToolMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "BP_AstroTool" then
						Astromesh = mesh						
						break
					end
				end
			end			
			
			--OxygenMeterMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "OxygenMeter" then
						Oxygenmesh = mesh				
						break
					end
				end
			end	
			
			--PowerToolMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.ChildActorComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "PowerTool" then
						Powertoolmesh = mesh				
						break
					end
				end
			end	
			
			--WidgetMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/UMG.WidgetComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "Widget" then
						Widgetmesh = mesh						
						break
					end
				end
			end	
			
			--FlashLightBarMesh
			local skeletal_mesh_c = api:find_uobject("Class /Script/UMG.WidgetComponent")
			if skeletal_mesh_c ~= nil then
				local skeletal_meshes = skeletal_mesh_c:get_objects_matching(false)
				
				for i, mesh in ipairs(skeletal_meshes) do
					if mesh:get_fname():to_string() == "FlashLightBar" then
						Flashlightmesh = mesh						
						break
					end
				end
			end	
			
			local player_anim_instance = mesh.AnimScriptInstance

			if player_anim_instance ~= nil then			
				
				UsingAstrotool = player_anim_instance.bUsingAstrotool
				Climbing = player_anim_instance.bClimbing
				IsViewTarget = pawn.bIsViewTarget
				ActorEnableCollision = pawn.bActorEnableCollision
				Gravity = pawn.bGravity
				
				SuitUnlocked = pawn.AstronautSuitUnlocked				
				IsCinematic = player_anim_instance.bIsCinematic
				
				-- Check if camera has been hijacked so we can fix the aim mode
				if pan_scanning_enabled == true then
					IsPanning = isInCinematic()
				end			
				
				-- AstroTool fix for Zero-G
				if force_astro == true then
					player_anim_instance.bUsingAstrotool = true					
				end				
	
				-- General VR fixes 
				if IsPanning == true then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
				
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						Fpmesh:SetVisibility(false)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)	
					end	
					
					pawn.bFirstPerson = true
					mesh:SetRenderInMainPass(false)
					mesh:SetRenderCustomDepth(false)
					
					astrotool_active = false
					normal_operation = false
					--print("IsPanning")
				elseif IsMainMenu == true then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
				
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						Fpmesh:SetVisibility(false)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)	
					end	
					
					pawn.bFirstPerson = false
					mesh:SetRenderInMainPass(false)
					mesh:SetRenderCustomDepth(false)			
					
					astrotool_active = false
					normal_operation = false
					--print("IsMainMenu")				
				
				elseif IsCinematic == true then				
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
				
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						Fpmesh:SetVisibility(false)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)	
					end	
					
					pawn.bFirstPerson = false
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)					
			
					astrotool_active = false
					normal_operation = false
					--print("IsCinematic")
					
				elseif UsingAstrotool == true then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
					if FLOATING_HANDS == false then	
					   vr.set_mod_value("VR_CameraForwardOffset", "-5.000000")
					   vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					   vr.set_mod_value("VR_CameraUpOffset", "30.000000")
					   
					   vr.set_mod_value("VR_LerpCameraYaw", "false")
					   vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					else
					   vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					   vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					   vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					   
					   vr.set_mod_value("VR_LerpCameraYaw", "true")
					   vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					end
						
					if Astromesh ~= nil then
						Astromesh:SetVisibility(true)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						if FLOATING_HANDS == true then	
							Fpmesh:SetVisibility(true)
						else
							Fpmesh:SetVisibility(false)
						end
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)
					end	
					
					pawn.bFirstPerson = true
					
					if FLOATING_HANDS == true then
						mesh:SetRenderInMainPass(false)
						mesh:SetRenderCustomDepth(false)
					else
						mesh:SetRenderInMainPass(true)
						mesh:SetRenderCustomDepth(true)					
					end
					
					normal_operation = false
					--print("UsingAstrotool")
					
				elseif Gravity == false then
					vr.set_mod_value("VR_AimMethod", "0")
					
					if IsInspecting == true then
						vr.set_mod_value("VR_RoomscaleMovement", "0")
					else
						vr.set_mod_value("VR_RoomscaleMovement", "1")
					end
					
					vr.set_mod_value("VR_DecoupledPitch", "0")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
					
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						if SuitUnlocked == true then
							Fpmesh:SetVisibility(true)
						else
							Fpmesh:SetVisibility(false)
						end
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end		

					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end

					if Powertoolmesh ~= nil then
						if SuitUnlocked == true then
							Powertoolmesh:SetVisibility(true)
						else
							Powertoolmesh:SetVisibility(false)
						end
					end				
						
					pawn.bFirstPerson = true
					mesh:SetRenderInMainPass(false)	
					mesh:SetRenderCustomDepth(false)				
					
					astrotool_active = false
					normal_operation = false
					--print("Gravity")
					
				elseif Climbing == true then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
					
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						Fpmesh:SetVisibility(false)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)
					end	
															
					pawn.bFirstPerson = false
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)
					
					astrotool_active = false
					normal_operation = false
					--print("Climbing")
					
				elseif IsViewTarget == false then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "0")
						
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
					vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(true)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						Fpmesh:SetVisibility(false)
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then						
						Powertoolmesh:SetVisibility(false)
					end	
						
					pawn.bFirstPerson = true
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)
					
					astrotool_active = false
					normal_operation = false
					--print("IsViewTarget")
					
				elseif ActorEnableCollision == false then
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "0")
					vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
						
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
				    vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
					
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
					
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end
					
					if Fpmesh ~= nil then
						if SuitUnlocked == true then
							Fpmesh:SetVisibility(true)
						else
							Fpmesh:SetVisibility(false)
						end
					end

					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then
						if SuitUnlocked == true then
							Powertoolmesh:SetVisibility(true)
						else
							Powertoolmesh:SetVisibility(false)
						end
					end	
						
					pawn.bFirstPerson = true
					mesh:SetRenderInMainPass(false)
					mesh:SetRenderCustomDepth(false)
					
					astrotool_active = false
					normal_operation = false
					--print("ActorEnableCollision")
					
				else
					if IsInspecting == true then
						vr.set_mod_value("VR_RoomscaleMovement", "0")
					else
						vr.set_mod_value("VR_RoomscaleMovement", "1")
					end
					
					vr.set_mod_value("VR_DecoupledPitch", "1")					
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")
					
				    vr.set_mod_value("VR_LerpCameraYaw", "false")
				    vr.set_mod_value("VR_LerpCameraSpeed", "0.000000")
						
					if Astromesh ~= nil then
						Astromesh:SetVisibility(false)
					end
						
					if Oxygenmesh ~= nil then
						Oxygenmesh:SetVisibility(false)
					end				
					
					if Fpmesh ~= nil then
						if SuitUnlocked == true then
							Fpmesh:SetVisibility(true)						
						else
							Fpmesh:SetVisibility(false)
							
						end
					end
					
					if Widgetmesh ~= nil then
						Widgetmesh:SetVisibility(false)
					end
					
					if Flashlightmesh ~= nil then
						Flashlightmesh:SetVisibility(false)
					end
					
					if Powertoolmesh ~= nil then
						if SuitUnlocked == true then
							Powertoolmesh:SetVisibility(true)
						else
							Powertoolmesh:SetVisibility(false)
						end
					end	
					
					astrotool_active = false
					normal_operation = true	
					--print("Normal gameplay")
					
				end		
			end	
		end
	end
end)
		
uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	if (state ~= nil) then
	
		local pawn = api:get_local_pawn()
		local mesh = pawn.Mesh	
		
		if IsViewTarget == false then
		
			-- Toggle cursor for puzzles
			if state.Gamepad.bLeftTrigger ~= 0 then
				vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "1")
			else
				vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
			end
		end	
		
		if normal_operation == true then
		
			-- Toggle cursor for puzzles
			if state.Gamepad.bLeftTrigger ~= 0 then
				vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "1")
			else
				vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
			end
		
			-- Toggle ASE with Y; This binding does not work without manually toggling bFirstPerson
			if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y ~= 0) then 
				pawn.bFirstPerson = false	
				mesh:SetRenderInMainPass(true)						
				mesh:SetRenderCustomDepth(true)
				vr.set_mod_value("VR_AimMethod", "0")
				attempt_ase_mode = true
			else
				pawn.bFirstPerson = true	
				mesh:SetRenderInMainPass(false)						
				mesh:SetRenderCustomDepth(false)
				attempt_ase_mode = false
				
				if IsInspecting == true then
					vr.set_mod_value("VR_AimMethod", "0")		
				else
					vr.set_mod_value("VR_AimMethod", "2")		
				end				
			end
			
		end	
	
		if state.Gamepad.bLeftTrigger ~= 0 then
			if state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB ~= 0 then
				state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_BACK
			end
		end	
	
		-- Swap X and B to fix button prompts for VR controllers
		if state.Gamepad.wButtons & 0x4000 ~= 0 and block_b == false then
            block_x = true
            state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_X)
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_B
            --print("X to B Button")
        else
            block_x = false
        end

        if state.Gamepad.wButtons & 0x2000 ~= 0 and block_x == false then
            block_b = true
            state.Gamepad.wButtons = state.Gamepad.wButtons & ~(XINPUT_GAMEPAD_B)
            state.Gamepad.wButtons = state.Gamepad.wButtons | XINPUT_GAMEPAD_X
            --print("B to X Button")
        else
            block_b = false
        end			
		
	end
end)

-- Check if menu is active
hook_function("WidgetBlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/UI/WB_MainMenu.WB_MainMenu_C", "Get_Astrotool_Counter_Visibility_0", true, 
	function(fn, obj, locals, result)
		--print("WB_MainMenu_C Get_Astrotool_Counter_Visibility_0")
		menu_debounce = 5
		return true
	end
, nil, true)

-- Check if player is examining object
hook_function("BlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/Interactables/Inspectables/BP_Inspect_Base.BP_Inspect_Base_C", "InspectingStart", true, 
	function(fn, obj, locals, result)
		--print("BP_Inspect_Base_C InspectingStart")
		IsInspecting = true
		return true
	end
, nil, true)

-- Check if player has stopped examining object
hook_function("BlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/Interactables/Inspectables/BP_Inspect_Base.BP_Inspect_Base_C", "InspectingStop", true, 
	function(fn, obj, locals, result)
		--print("BP_Inspect_Base_C InspectingStop")
		IsInspecting = false
		return true
	end
, nil, true)

-- Check if player is using AstroTool PDA
hook_function("BlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/AstroTool/BP_AstroTool.BP_AstroTool_C", "OnEnterFocusAstroTool", true, 
	function(fn, obj, locals, result)
		--print("BP_AstroTool_C OnEnterFocusAstroTool")
		force_astro = true
		return true
	end
, nil, true)

-- Check if player has stopped using AstroTool PDA
hook_function("BlueprintGeneratedClass /Game/DeliverUsTheMoon/Core/AstroTool/BP_AstroTool.BP_AstroTool_C", "OnLeaveMonitor_Event", true, 
	function(fn, obj, locals, result)
		--print("BP_AstroTool_C OnLeaveMonitor_Event")
		force_astro = false
		return true
	end
, nil, true)


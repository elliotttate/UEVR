local api = uevr.api
local vr = uevr.params.vr
local params = uevr.params
local callbacks = params.sdk.callbacks

local CurrentLevel = nil
local IsInCar = nil
local IsDialogCameraZoom = nil
local IsHandRaised = nil
local IsHandRaisedState2 = nil
local IsCutscene = nil
local IsDialog = nil
local IsInMenu = nil
local IsCrouched = nil

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)

    local game_engine_class = api:find_uobject("Class /Script/Engine.GameEngine")
    local game_engine = UEVR_UObjectHook.get_first_object_by_class(game_engine_class)

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

    if world ~= last_world then
        print("World changed")
    end

    last_world = world

    local level = world.PersistentLevel

    if level == nil then
        print("Level is nil")
        return
    end
	
	print("Level name: " .. level:get_full_name())
	
	if string.find(tostring(level:get_full_name()), "main_menu.main_menu.PersistentLevel") then
		vr.set_mod_value("VR_AimMethod", "0")
		print("Main menu found")
		return
	end		
    	
	local pawn = api:get_local_pawn()
	local mesh = pawn.Mesh	

    if pawn ~= nil then
			
		if string.find(tostring(pawn:get_full_name()), "BP_InteractionsCharacter_C") then	
		
			IsCrouched = pawn._CrouchEnabled
		
			local controller_instance = pawn.Controller
			
			if controller_instance ~= nil then	
				print("Controller instance found")	
				IsCutscene = controller_instance._IsInCutscene
				IsDialog = controller_instance._IsInDialog
			end
			
				
			local level_instance = pawn.ZoneBasedLevelStreamer
				
			if level_instance ~= nil then	
                print("Level instance found")					
				CurrentLevel = level_instance.currentPresetName			
				local strCurrentLevel = CurrentLevel:to_string()
					
				if string.find(strCurrentLevel, "Car") then	
					IsInCar = true		
				else	
					IsInCar = false	
				end	
				
				if string.find(strCurrentLevel, "None") then	
					IsInMenu = true		
				else	
					IsInMenu = false	
				end	
			end			
				
				
			local player_dialog_instance = pawn.DialogCameraZoom

			if player_dialog_instance ~= nil then				
				IsDialogCameraZoom = player_dialog_instance._Enabled					
			end

			local player_anim_instance = mesh.AnimScriptInstance

			if player_anim_instance ~= nil then				
				IsHandRaised = player_anim_instance.HandRaised
					
				if IsInCar == true or IsInMenu == true then -- Intro detection
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)	
					UEVR_UObjectHook.set_disabled(true)						
						
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "0")
											
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")	

					print("IsInCar")	

				elseif IsCutscene == true then -- Cutscene detection	
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)	
					UEVR_UObjectHook.set_disabled(false)	
						
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "0")
											
					vr.set_mod_value("VR_CameraForwardOffset", "-15.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")	
						
					print("Cutscene detected")		
					
				elseif IsDialogCameraZoom == true or IsDialog == true then	-- Dialog detection
					mesh:SetRenderInMainPass(true)
					mesh:SetRenderCustomDepth(true)	
					UEVR_UObjectHook.set_disabled(false)	
						
					vr.set_mod_value("VR_AimMethod", "0")
					vr.set_mod_value("VR_RoomscaleMovement", "0")
					vr.set_mod_value("VR_DecoupledPitch", "1")
											
					vr.set_mod_value("VR_CameraForwardOffset", "15.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")	
						
					print("Dialog detected")						
						
				elseif IsHandRaised == true then -- HUD detection
					
                       if IsHandRaisedState2 == true then
						if os.clock() - button_held_time >= 3 then 
							vr.set_mod_value("VR_AimMethod", "0")
						end				

						print("Hand raised state 2")							
					else						
						mesh:SetRenderInMainPass(true)
						mesh:SetRenderCustomDepth(true)	
						UEVR_UObjectHook.set_disabled(false)	
							
						vr.set_mod_value("VR_AimMethod", "2")
						vr.set_mod_value("VR_RoomscaleMovement", "0")
						vr.set_mod_value("VR_DecoupledPitch", "1")
						
						if IsCrouched == false then							
							vr.set_mod_value("VR_CameraForwardOffset", "15.000000")
							vr.set_mod_value("VR_CameraRightOffset", "0.000000")
							vr.set_mod_value("VR_CameraUpOffset", "0.000000")	
						else
							vr.set_mod_value("VR_CameraForwardOffset", "35.000000")
							vr.set_mod_value("VR_CameraRightOffset", "0.000000")
							vr.set_mod_value("VR_CameraUpOffset", "-10.000000")	
						end	
							
						button_held_time = os.clock()								
						IsHandRaisedState2 = true
							
						print("Hand raised state 1")
					end						
				else
					mesh:SetRenderInMainPass(false)
					mesh:SetRenderCustomDepth(false)
					UEVR_UObjectHook.set_disabled(false)	
						
					vr.set_mod_value("VR_AimMethod", "2")
					vr.set_mod_value("VR_RoomscaleMovement", "1")
					vr.set_mod_value("VR_DecoupledPitch", "1")
						
					vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
					vr.set_mod_value("VR_CameraRightOffset", "0.000000")
					vr.set_mod_value("VR_CameraUpOffset", "0.000000")	
						
					IsHandRaisedState2 = false
						
					print("Normal gameplay")
				end
			end		
		end	
	else
		vr.set_mod_value("VR_AimMethod", "0")
		vr.set_mod_value("VR_RoomscaleMovement", "0")
		vr.set_mod_value("VR_DecoupledPitch", "0")		
		vr.set_mod_value("VR_MotionControlsInactivityTimer", "9999.000000")	
	end
end)
		
uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	if (state ~= nil) then
	
		if state.Gamepad.bLeftTrigger ~= 0 then
			vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "1")
		else
			vr.set_mod_value("FrameworkConfig_AlwaysShowCursor", "0")
		end
	
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

UEVR_UObjectHook.activate()

local api = uevr.api
local m_VR = uevr.params.vr

local pawn = api:get_local_pawn(0)
local lplayer = api:get_player_controller(0)

local function set2d()

			print('set2d')

			m_VR.set_mod_value("VR_AimMethod", 0)
			m_VR.set_mod_value("VR_DesktopRecordingFix_V2", "true")
			m_VR.set_mod_value("VR_DecoupledPitch", "false")
			m_VR.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			m_VR.set_mod_value("FrameworkConfig_AlwaysShowCursor", "true")
			m_VR.set_mod_value("VR_2DScreenMode", "true")
			
			m_VR.recenter_view()
			m_VR.recenter_horizon()
			
end

local function set3d()

			print('set3d')

			m_VR.set_mod_value("VR_AimMethod", 0)
			m_VR.set_mod_value("VR_DesktopRecordingFix_V2", "true")
			m_VR.set_mod_value("VR_DecoupledPitch", "false")
			m_VR.set_mod_value("VR_DecoupledPitchUIAdjust", "true")
			m_VR.set_mod_value("FrameworkConfig_AlwaysShowCursor", "false")
			m_VR.set_mod_value("VR_2DScreenMode", "false")
			
			m_VR.recenter_view()
			m_VR.recenter_horizon()
			
end

function hideShowUI(is_show)

	local allMesh_c = api:find_uobject("Class /Script/LGUI.UIDrawcallMesh")
	local allMesh_obj = UEVR_UObjectHook.get_first_object_by_class(allMesh_c)

	for i = 1, 100 do

		if allMesh_obj.AttachParent ~= nil then
			allMesh_obj = allMesh_obj.AttachParent
		else
			break
		end

	end
	
	if allMesh_obj ~= nil then
	
		print(allMesh_obj)
	
		if is_show then
			
			if allMesh_obj.SetWidth ~= nil and allMesh_obj.SetHeight ~= nil then
				allMesh_obj:SetWidth(2560)
				allMesh_obj:SetHeight(1440)
			end
			
		elseif not is_show then
			
			if allMesh_obj.SetWidth ~= nil and allMesh_obj.SetHeight ~= nil then
				allMesh_obj:SetWidth(2560)
				allMesh_obj:SetHeight(1440 * 2)
			end
			
		end
		
	end

end

local counter = 0
local is_dialogue = false
local OnEnableBP_seted = false
local OnUIInteractionStateChangedBP = nil

function detect_dialog()
	if lplayer.Character ~= nil then
	
		local LGUIBehaviour_c = api:find_uobject("Class /Script/LGUI.LGUIBehaviour")

		if LGUIBehaviour_c ~= nil then
		
			print(LGUIBehaviour_c)
		
			OnEnableBP_seted = true
			print('OnEnableBP_seted')

			OnUIInteractionStateChangedBP = LGUIBehaviour_c:find_function("OnUIInteractionStateChangedBP")
			
			if OnUIInteractionStateChangedBP ~= nil then
			
				print(OnUIInteractionStateChangedBP)

				OnUIInteractionStateChangedBP:set_function_flags(OnUIInteractionStateChangedBP:get_function_flags() | 0x400) -- Mark as native
				OnUIInteractionStateChangedBP:hook_ptr(function(fn, obj, locals, result)
				
					if obj ~= nil then
						if string.sub(obj:get_fname():to_string(), 0, 8) == "TsUiBlur" then
							is_dialogue = true
							print('OnUIInteractionStateChangedBP')
						end
						
						if string.sub(obj:get_fname():to_string(), 0, 34) == "TsUiAutoPlayLevelSequenceComponent" then
							is_dialogue = true
							print('OnUIInteractionStateChangedBP')
						end
						
						if string.sub(obj:get_fname():to_string(), 0, 25) == "TsUiNavigationPanelConfig" then
							is_dialogue = true
							print('OnUIInteractionStateChangedBP')
						end
						
						if string.sub(obj:get_fname():to_string(), 0, 30) == "TsUiNavigationBehaviorListener" then
							--is_dialogue = true
							print('OnUIInteractionStateChangedBP')
						end
					end
					
					--print(obj:get_fname():to_string() .. ' : ' .. counter)
					
					--counter = counter + 1
					
					return false
				end)
				
			end
			
		end
	
	end
end

uevr.sdk.callbacks.on_script_reset(function()

	if OnUIInteractionStateChangedBP ~= nil then
		OnUIInteractionStateChangedBP:set_function_flags(OnUIInteractionStateChangedBP:get_function_flags() & ~0x400) -- Unmark as native
	end
	
end)

local View_Target = nil
local View_Target_old = ""
local Is_Move_old = 0
local Is_Move = 0

local Is_3d = false
set2d()
hideShowUI(true)

local c_ = 0

uevr.sdk.callbacks.on_xinput_get_state(function(retval, user_index, state)

	c_ = c_ + 1
	
	lplayer = api:get_player_controller(0)
	pawn = api:get_local_pawn(0)
	
	if lplayer ~= nil and pawn ~= nil then
		local gamepad = state.Gamepad
	  
		local DPAD_LEFT = gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT ~= 0
		local START = gamepad.wButtons & XINPUT_GAMEPAD_START ~= 0
		
		if not OnEnableBP_seted then detect_dialog() end
		
		if pawn.CapsuleComponent ~= nil then Is_Move = pawn.CapsuleComponent.RelativeLocation.X + pawn.CapsuleComponent.RelativeLocation.Y + pawn.CapsuleComponent.RelativeLocation.Z end
		
		local bShowMouse = lplayer.bShowMouseCursor
		
		View_Target = lplayer:GetViewTarget():get_fname():to_string()
		
		if View_Target ~= View_Target_old then 
		
			print("View_Target: " .. View_Target)
			View_Target_old = View_Target
			
		end
		
		if DPAD_LEFT or START or bShowMouse or is_dialogue or string.sub(View_Target, 0, 13) == "BP_CineCamera" then
		
			if Is_3d then
				c_ = 0
				Is_3d = false
				set2d()
				hideShowUI(true)
			end
			
		end
			
		if c_ > 200 and Is_Move ~= Is_Move_old then
			if string.sub(View_Target, 0, 11) == "CameraActor" then
			
				if not Is_3d then
					Is_3d = true
					is_dialogue = false
					set3d()
					hideShowUI(false)
				end	
				
			end
		end
		
		Is_Move_old = Is_Move
	
	end
  
end)
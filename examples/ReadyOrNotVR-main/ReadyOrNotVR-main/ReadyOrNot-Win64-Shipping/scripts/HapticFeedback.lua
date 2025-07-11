
--YOU CAN EDIT THIS LINE true/false
local HapticFeedbackActive=true
-----------------------------------
local api = uevr.api

local pawn = api:get_local_pawn(0) 

local LeftController=uevr.params.vr.get_left_joystick_source()
local RightController= uevr.params.vr.get_right_joystick_source()

uevr.sdk.callbacks.on_pre_engine_tick(
	function(engine, delta)
pawn = api:get_local_pawn(0)

if HapticFeedbackActive and pawn ~=nil then


--repawning just to be safe
		
		--end
	--	print(pawn:get_fname():to_string())																						--DEBUG1
	--	print(UEVR_UObjectHook.exists(pawn))-- d2
		--local lplayer = api:get_player_controller(0)
		
		local UsedPrimary=pawn.InventoryComp.LastEquippedWeapon.HeatTime
		
		--HapticFeedback
		if UsedPrimary <= 0.02 then
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.04, 1.0, 255.0, LeftController)
			uevr.params.vr.trigger_haptic_vibration(0.0, 0.04, 1.0, 255.0, RightController)
		
		end
end
end)
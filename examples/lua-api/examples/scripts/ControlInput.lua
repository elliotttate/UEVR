require(".\\Subsystems\\UEHelper")

uevr.sdk.callbacks.on_xinput_get_state(
function(retval, user_index, state)


if Ybutton then
	unpressButton(state,XINPUT_GAMEPAD_Y)
	pressButton(state,XINPUT_GAMEPAD_LEFT_SHOULDER)
end
if isMenu==false then
	if lThumb then
	unpressButton(state,XINPUT_GAMEPAD_LEFT_THUMB)
	--pressButton(state,XINPUT_GAMEPAD_LEFT_SHOULDER)
	end
	if lShoulder then
		unpressButton(state,XINPUT_GAMEPAD_LEFT_SHOULDER)
		pressButton(state,XINPUT_GAMEPAD_LEFT_THUMB)
	end
	
	if ThumbRY > 30000 then
		pressButton(state,XINPUT_GAMEPAD_Y)
	end
	if ThumbRY < -30000 then
		pressButton(state,XINPUT_GAMEPAD_B)
	end
end
end)
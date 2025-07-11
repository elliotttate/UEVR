--print("Initializing hello_world.lua")
--
uevr.sdk.callbacks.on_xinput_get_state(
	function(retval, user_index, state)
	
	print(state.Gamepad.bRightTrigger)
	if (state.Gamepad.bRightTrigger ~= nil) then
		if (state.Gamepad.bRightTrigger >= 200) then
local api = uevr.api

  pawn = api:get_local_pawn(0)
--if pawn == nil then print("pawn is nil") 
  wpn_meshL = nil
--end

--uevr.sdk.callbacks.on_pre_engine_tick(
--	function(engine, delta)
--	)

		
		
local EquWpn = pawn.EquipmentComponent.EquippedWeapon
			if EquWpn == nil then print ("equ is nil")
			else
			print(EquWpn:get_fname())
			end
	local MeshC= api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
	        print(MeshC)
	local skeletal_meshes = EquWpn:K2_GetComponentsByClass(MeshC)
            --print(skeletal_meshes)
			
	
	
		for i, Wmesh in ipairs(skeletal_meshes) do
			if Wmesh:get_fname():to_string() == "WeaponMesh" then
				--local Wcheck= Wmesh
				--print(Wcheck:GetOwner():get_fname():to_string())
				--print("+")
				--print(Wmesh:GetOwner():get_fname():to_string())
				--print("+")
				--print(wpn_meshL)
				--if Wcheck == Wmesh then
				   -- print("ha")
					--wpn_meshL:call("SetRenderInMainPass",(false))
					--wpn_meshL = Wmesh
					wpn_meshL=Wmesh:GetOwner():get_fname():to_string()
					print("found arms " .. Wmesh:get_full_name())
					print(Wmesh:GetOwner():get_fname())
					WpnName = Wmesh:GetOwner()
					
					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_hand(2)
					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(1.337, 1.0, 1.0))--=Vector3d.new(0,9,9)
					
					--local WmeshRot= UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh)
					--print(WmeshRot)
					
					
					if string.find(WpnName:get_fname():to_string(), "StG44") then
					print ("The word StG44 was found.")
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(18, 0, 0))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
					elseif string.find(WpnName:get_fname():to_string(), "G43") then
					print ("The word G43 was found.")
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -8, 0))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
					else
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -8, 0))
					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
					end  
				--end
			break
			end
		end		




----uevr.sdk.callbacks.on_xinput_get_state(
----	function(retval, user_index, state)
----	
----	print(state.Gamepad.bRightTrigger)
----	if (state.Gamepad.bRightTrigger ~= nil) then
----		if (state.Gamepad.bRightTrigger >= 200) then
----	--print("hi")
----		--if pawn == nil then
----	pawn = api:get_local_pawn(0)
----		--end
----	print(pawn)
----
----		
----
----
----
----
----	
----	local EquWpn = pawn.EquipmentComponent.EquippedWeapon
----			if EquWpn == nil then print ("equ is nil")
----			else
----			print(EquWpn:get_fname())
----			end
----	local MeshC= api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
----	        print(MeshC)
----	local skeletal_meshes = EquWpn:K2_GetComponentsByClass(MeshC)
----            --print(skeletal_meshes)
----			
----	
----	
----		for i, Wmesh in ipairs(skeletal_meshes) do
----			if Wmesh:get_fname():to_string() == "WeaponMesh" then
----				 Wcheck= Wmesh
----				print(Wcheck:GetOwner():get_fname():to_string())
----				print("+")
----				print(Wmesh:GetOwner():get_fname():to_string())
----				print("+")
----				print(wpn_meshL)
----				if  Wcheck:GetOwner():get_fname():to_string() == wpn_meshL then
----				    print("ha")
----					--wpn_meshL:call("SetRenderInMainPass",(false))
----					--wpn_meshL = Wmesh
----					
----					print("found arms " .. Wmesh:get_full_name())
----					print(Wmesh:GetOwner():get_fname())
----					WpnName = Wmesh:GetOwner()
----					
----					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_hand(2)
----					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
----					--UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(1.337, 1.0, 1.0))--=Vector3d.new(0,9,9)
----					
----					--local WmeshRot= UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh)
----					--print(WmeshRot)
----					
----					
----					if string.find(WpnName:get_fname():to_string(), "StG44") then
----					print ("The word StG44 was found.")
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.6903455270798549,0.15304591873303094, 0.6903455270798549, -0.15304591873303094))
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -5, 0))
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
----					else 
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_rotation_offset(Vector4f.new(0.9762960071199334,0.21643961393810288,0,0))
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_location_offset(Vector3d.new(0, -5, 0))
----					UEVR_UObjectHook.get_or_add_motion_controller_state(Wmesh):set_permanent(true)
----					end  
----				end
----			break
----			end
----		end		
	--local WpnName = Wmesh:GetOwner()

	
	
	--StG44
	
	
	
	
	--if Wmesh:	
	--UEVR_UObjectHook.get_motion_controller_state(Wmesh)
			
		end
	end
--
--end )
end)
--)
 
--fpvmesh:SetRenderInMainPass(false)

--local api = uevr.api
--
--local pawn = api:get_local_pawn(0)
--if pawn == nil then print("pawn is nil") end
--
--local skeletal_mesh_c = api:find_uobject("Class /Script/Engine.SkeletalMeshComponent")
--if skeletal_mesh_c == nil then print("skeletal_mesh_c is nil") end
--
--local skeletal_meshes = pawn:K2_GetComponentsByClass(skeletal_mesh_c)
--
--local guns_mesh = nil
--for i, mesh in ipairs(skeletal_meshes) do
--    if mesh:get_fname():to_string() == "ArmsMesh" then
--        guns_mesh = mesh
--        print("found arms " .. mesh:get_full_name())
--        break
--    end
--end

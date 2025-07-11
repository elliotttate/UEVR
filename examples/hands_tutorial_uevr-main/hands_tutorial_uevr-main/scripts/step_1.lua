--[[
	1) Figure out from the UEVR UI, which property is the SkeletalMeshComponent that contains the hands
	2) Use that property in the hands.debug() call in on_lazy_poll()
	3) Run UEVR and press the Spawn Debug Console button under LuaLoader Main
	4) Enter a game (the hands may not spawn in the Intro screen) and you should see a SkeletalMeshComponent	
		connected to your right hand. The hands will likely be above your head so look up.
	5) Make sure the SkeletalMeshComponent is facing forward when your hand is facing forward. If it is not then
		you will have to make an extra adjustment on line 44 using the correct values for your specific game until the mesh is facing forward. 
		The SkeletalMeshComponent should be "mostly" facing forward. IN TOW, for example, the mesh will look a bit skewed but it is facing forward.
		If the mesh is at a 90 or 180 degree angle then that needs to be adjusted
	6) While keeping your hand steady, move your head around and see if the SkeletalMeshComponent attached to your hand appears to shift slightly, even
		though your hand is not moving. If this is the case then the game is using some type of FOV adjustment of the SkeletalMeshComponent
		that may be needed for the flat game but is a problem in VR. This FOV adjustment will have to be corrected on the SkeletalMeshComponent
		before hands will work properly. TOW does have this issue. If you are running this tutorial in TOW, uncomment line 49 to correct 
		the issue then run UEVR again to see the fix. Note that if you are running under RoboCop and replace "ForegroundPriorityEnabled"
		with "UsePanini" then this will fix RoboCop. But this string replacement may not work for all games so you will need to figure a fix for
		your particular game.
	7) In the console window that was spawned, you will see a list of bones printed out. Copy and paste that list
		of bones into a text document for future reference
	8) Shut down the game, rename this file to step_1.luax and rename step_2.luax to step_2.lua
	9) Open step_2.lua to continue the tutorial
]]--

local uevrUtils = require("libs/uevr_utils")
uevrUtils.setLogLevel(LogLevel.Debug)
uevrUtils.initUEVR(uevr)
local flickerFixer = require("libs/flicker_fixer")
local controllers = require("libs/controllers")
local animation = require("libs/animation")
local hands = require("libs/hands")

function on_level_change(level)
	print("Level changed\n")
	flickerFixer.create()
	controllers.onLevelChange()
	controllers.createController(0)
	controllers.createController(1)
	controllers.createController(2)
	hands.reset()
end

function on_lazy_poll()
	if not hands.exists() then
		--hands.setOffset({X=0, Y=0, Z=0, Pitch=0, Yaw=90, Roll=0})	
		
		hands.debug(pawn.FPVMesh)	-- Replace this with your findings from Step 1		
	end
	if hands.exists() then
		--uevrUtils.fixMeshFOV(hands.getHandComponent(1), "ForegroundPriorityEnabled", 0.0, true, true, false)
	end
end


print("Initializing UE4SS_hook.lua")

UEVR_UObjectHook.activate()

local api = uevr.api;
local pc_class = api:find_uobject("Class /Script/Engine.PlayerController")
local pc = UEVR_UObjectHook.get_first_object_by_class(pc_class)
pc:SendToConsole("UEVR")


print("\n---------------- Initializing RTypeFinal2.lua ----------------")

UEVR_UObjectHook.activate()

local api = uevr.api;
local uobjects = uevr.types.FUObjectArray.get()
local vr = uevr.params.vr


-- print("Printing first 5 UObjects")
-- for i=0, 5 do
--     local uobject = uobjects:get_object(i)

--     if uobject ~= nil then
--         print(uobject:get_full_name())
--     end
-- end

local once = true
local last_world = nil
local last_level = nil
local last_pawn = nil

uevr.sdk.callbacks.on_post_engine_tick(function(engine, delta)

end)

local spawn_once = true
local last_check_time = 0
local interval = 10
local isInStage1 = false
local inStageCamOffset = "18000.000000"
local animationCamOffset =  "20.000000"

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)

    --[[if spawn_once then
        local cheat_manager_c = api:find_uobject("Class /Script/Engine.CheatManager")
        local cheat_manager = UEVR_UObjectHook.get_first_object_by_class(cheat_manager_c)

        print(tostring(cheat_manager_c))

        cheat_manager:Summon("Something_C")

        spawn_once = false
    end]]
    last_check_time = last_check_time + delta

    if last_check_time >= interval then
        local pawn = uevr.api:get_local_pawn(0)
        if pawn then
            print("[INFO] pawn: ".. pawn:get_full_name()) 
            print("[INFO] VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
            -- local controller = pawn.Controller
            -- if controller ~= nil then
            --     print("Controller: " .. controller:get_full_name())
            -- end

        end
        last_check_time = 0
    end

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
        -- print("\n\n*** World changed ***")
        -- print("World name: " .. world:get_full_name())
        last_world = world
    end

    local level = world.PersistentLevel
    if level == nil then
        print("Level is nil")
        return
    end

    if level ~= last_level then
        print("*** Level changed *** ")
        local level_name = level:get_full_name()
        print("Level name: " .. level_name)


        --[[ 
        Level name examples:
        Level /Game/Level/st_01_02/stage_01_02_root.stage_01_02_root.PersistentLevel <-- Stage mode
        Level /ST_0119/Level/st_01_19/Stage_01_19_root.Stage_01_19_root.PersistentLevel <-- Competition mode
        Level /Game/Level/title/title.title.PersistentLevel
        Level /Game/Level/Hangar/Hangar_pre.Hangar_pre.PersistentLevel
        Level /Game/Level/Museum/Museum.Museum.PersistentLevel
        Level /Game/Level/Hangar/PilotCustom/PilotCustom_BG01.PilotCustom_BG01.PersistentLevel
        ]]
        isInStage1 = false
        if string.match(level_name, "^Level /Game/Level/st_01_01/stage_.+") then
            print("In Stage 1 !")
            isInStage1 = true
        elseif string.match(level_name, "^Level /Game/Level/st_%d+_%d+/stage_.+") then
            print("In Stage !")
            print("Set Camera.")
            vr.set_mod_value("VR_CameraForwardOffset", inStageCamOffset)
            print("New VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
        elseif string.match(level_name, "^Level /ST_%d+/Level/st_%d+_%d+/.+") then
            print("In Competition !")
            print("Set Camera.")
            vr.set_mod_value("VR_CameraForwardOffset", inStageCamOffset)
            print("New VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
        else
            print("Out Stage !")
            -- Only reset camera when exist stage
            print("Reset Camera.")
            vr.set_mod_value("VR_CameraForwardOffset", "0.000000")
            print("New VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
        end

 


        last_level = level
    end

    if isInStage1 then
        local pawn = uevr.api:get_local_pawn(0)
        if pawn and pawn ~= last_pawn then
            local pawn_name = pawn:get_full_name()
            print("pawn Chaged: ".. pawn_name) 
            -- cutscene
            -- Pawn /Game/Level/st_01_01/stage_01_01_root.stage_01_01_root.PersistentLevel.Pawn_2147383444
            if string.match(pawn_name, "^Pawn /.+") then
                print("Set Animation Camera.")
                vr.set_mod_value("VR_CameraForwardOffset", animationCamOffset)
                print("New VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
            else 
            -- if string.match(pawn_name, "^P%d+ /.+") then -- P001/P027/P042 ...etc
                print("Set Stage Camera.")
                vr.set_mod_value("VR_CameraForwardOffset", inStageCamOffset)
                print("New VR_CameraForwardOffset: " .. string.gsub(vr:get_mod_value("VR_CameraForwardOffset"), "%c", ""))
            end
            -- local controller = pawn.Controller
            -- if controller ~= nil then
            --     print("Controller: " .. controller:get_full_name())
            -- end

            last_pawn=pawn
        end
    end

    -- if once then
    --     -- print("executing stat fps")
    --     -- uevr.api:execute_command("stat fps")
    --     once = false

    --     print("executing stat unit")
    --     uevr.api:execute_command("stat unit")

    --     print("GameEngine class: " .. game_engine_class:get_full_name())
    --     print("GameEngine object: " .. game_engine:get_full_name())
    -- end
end)

uevr.sdk.callbacks.on_script_reset(function()
    print("Resetting RTypeFinal2.lua")
end)
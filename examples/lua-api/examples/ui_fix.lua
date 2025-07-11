print("Initializing hello_world.lua")

UEVR_UObjectHook.activate()

local api = uevr.api

local game_engine_class = api:find_uobject("Class /Script/Engine.GameEngine")
local red_hud_c = api:find_uobject("Class /Script/RED.REDHUDActor")

if game_engine_class == nil then
    print("Failed to find GameEngine class")
    return
end

if red_hud_c == nil then
    print("Failed to find REDHUDActor class")
    return
end

uevr.sdk.callbacks.on_pre_engine_tick(function(engine, delta)
    local red_hud = red_hud_c:get_first_object_matching(false)

    if red_hud == nil then return end

    -- Is a WidgetComponent
    local top_hud = red_hud.TopHUD
    if top_hud == nil then return end

    local widget = top_hud.Widget
    if widget == nil then return end

    widget:AddToViewport(0)

    local mesh_component = red_hud.StaticMeshComponent

    if mesh_component ~= nil then
        mesh_component:SetRenderInMainPass(false)
    end
end)
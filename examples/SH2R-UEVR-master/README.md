# SH2R-UEVR

UEVR improvements/motion controls for SILENT HILL 2 (2024)


## Install

First, make sure that you are using the very latest **NIGHTLY** version of UEVR, you can get that here: https://github.com/praydog/UEVR-nightly/releases/latest/ or using [Rai Pal](https://github.com/Raicuparta/rai-pal)

Get the [latest release zip](https://github.com/praydog/SH2R-UEVR/releases/latest) and click "Import Config" in UEVR, browse to the zip and click it.

If you have any previous UEVR profile for this game, you may need to delete it as it may conflict with what this is attempting to do.

## Features
### First Person
  * Smooth movement
  * Camera is corrected to fix interactions and audio direction

### Motion controls
* #### Ranged weapon aiming
  * Two handing of rifle and shotgun
  * Properly projected crosshair in 3D space
* #### Melee swings
  * Fully immersive and accurate to weapon's size, even with custom models
  * Contextual hit reactions
    * Hitting enemies in the legs has a chance to incapacitate enemies for a short time, causing them to kneel in pain
    * Physics impulses are accurately applied in the hit areas, causing limbs to move upon hits
* #### Items
  * Items are attached to right hand when inspecting or receiving them (like ammo or syringes)
  * The map is attached to both hands to simulate holding a map in real life

### Other features
* Roomscale movement
* Automatic disabling of VR features in cutscenes and full body events (though still fully 6DOF and stereoscopic)

Inverse kinematics has been loosely explored, but not working correctly. So body has been hidden for now, and re-enabled in cutscenes and events.

## For curious modders

This uses a hybrid C++ plugin and Lua script, and should serve as a good example of adding VR functionality.

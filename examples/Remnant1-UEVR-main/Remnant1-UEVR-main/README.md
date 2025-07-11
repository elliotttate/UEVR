# Remnant1-UEVR

UEVR improvements/motion controls for Remnant: From the Ashes


## Install

1. Make sure that you are using the very latest **NIGHTLY** version of UEVR, you can get that here: https://github.com/praydog/UEVR-nightly/releases/latest/ or using [Rai Pal](https://github.com/Raicuparta/rai-pal)

2. Get the [latest release zip](https://github.com/deterministicj/Remnant1-UEVR/releases/latest) and click "Import Config" in UEVR, browse to the zip and click it.

If you have any previous UEVR profile for this game, you will need to delete it as it may conflict with what this is attempting to do.

## Features
### First Person
  * Moved to first person with modifications made to fix problems resulting from it

### Motion controls
  * 6DOF motion control aiming
  * Fixed weapon fire trace
  * Hide weapon while not aiming
  * Disable in cutscenes


## Configuration

Enable (true)/Disable (false) LT to RB swap
  - Default Enabled. Within remnant-controls.lua, change local swapltrb = true on line 4 to true or false

** Experimental ** Melee gesture controls
  - Default Disabled. Within remnant-controls.lua, change local melee_swing = false on line 9 to true
- When enabled:
    - melee can be comboed using gestures
    - right to left, left to right, up to down is the combo
    - RT melee is disabled
    - Currently no way to power swing melee using this

## FAQ

#### When I use the mod I get LUA script errors and/or my weapon is in 3DOF instead of 6DOF

This is because you are not using the latest Nightly of UEVR required for the mod, you can get that [here](https://github.com/praydog/UEVR-nightly/releases/latest/)

#### Does co-op work?

Co-op was not tested at all, but may still work just fine

## Information

This mod uses 4 parts:

    1. UEVR profile
        - hiding meshes and setting up first person camera view
    2. remnant.dll, remnant.txt - C++ plugin w/ config file
        - Corrects weapon trace with config file for offset if needed
    3. remnant-main.lua - Main LUA script
        - Handling of mod when not in gameplay
        - Disabling pawn pitch rotation
        - Hiding vignette
        - Motion controls
    4. remnant-controls.lua - Controls LUA script
        - Modifies controls for better experience
        - Adds right stick deadzone which fixes problems

### Credits

Special thanks to:
- Markmon - For UEVR plugin development help, coding help, brainstorming, various fixes and features, and testing
- CJ117 - For various fixes and features as well as testing
- Praydog - For UEVR, his great SH2-UEVR mod example, and various help with mod development
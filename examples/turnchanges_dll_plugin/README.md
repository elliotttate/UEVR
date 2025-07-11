# TurnChanges DLL Plugin for UEVR

A DLL plugin that replicates the functionality from TurnChangesToPlugin as a drop-in UEVR plugin.

## Features

### ✅ L3+R3 Aim Method Toggle
- **Hold L3+R3 for 1 second** to toggle between current aim method and GAME mode
- **Visual feedback** with progress logging
- **Memory restoration** - remembers your previous aim method when toggling back from GAME mode
- **Based on actual TurnChangesToPlugin implementation** using `headlocked_begin_held` logic

### ✅ New DPad Methods (6 & 7)
- **Method 6**: Right Joystick Press + Left Joystick (Disables R3)
- **Method 7**: Left Joystick Press + Right Joystick (Disables L3)
- **Conflict prevention** - automatically disables L3/R3 when used for DPad control
- **Proper joystick-to-DPad conversion** with deadzone handling

### ⚠️ Dropdown Limitation
Since this is a plugin (not a fork), the new DPad methods **won't appear in the GUI dropdown**. 
However, you can still:
- Set them via config files
- Use the Lua console: `uevr.params.vr:set_mod_value("VR_DPadShiftingMethod", "6")` 
- Use external tools to modify the config

## Installation

1. **Copy** `TurnChangesPlugin.dll` to your game's `UEVR\plugins\` folder
2. **Launch** your game with UEVR
3. **The plugin will automatically load** and add the functionality

## Usage

### L3+R3 Aim Toggle
1. **Hold both L3 and R3** on your VR controllers 
2. **Keep holding for 1 second** (progress shown in logs)
3. **Aim method will toggle** between current method and GAME mode
4. **Release early** to cancel the toggle

### New DPad Methods
1. **Set DPad method to 6 or 7** (via config or Lua console)
2. **For Method 6**: Hold R3, use left joystick for DPad input
3. **For Method 7**: Hold L3, use right joystick for DPad input
4. **R3/L3 buttons are disabled** when used for DPad to prevent conflicts

## Technical Details

This plugin properly implements:
- **XInput interception** via `on_xinput_get_state`
- **State tracking** similar to TurnChangesToPlugin's XInputContext
- **Proper timing logic** for L3+R3 hold detection
- **Joystick axis conversion** to DPad with 0.5 deadzone
- **Button conflict prevention** by masking L3/R3 when active

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Logging

The plugin logs all actions to `UEVR.log`:
- L3+R3 hold progress and toggle events
- DPad method activation/deactivation
- Aim method changes with names

## Compatibility

- **UEVR Version**: Any version with Plugin API support
- **Games**: All games supported by UEVR
- **VR Headsets**: All OpenVR/OpenXR compatible headsets

## Credits

Based on analysis of TurnChangesToPlugin implementation by the UEVR community. 
# Extended DPad Plugin for UEVR

## Overview

This DLL plugin for UEVR adds **two new DPad methods** (6 & 7) that use joystick click buttons as modifiers. The plugin enables the TurnChangesToPlugin functionality without requiring modifications to the main UEVR codebase.

## ✅ Successfully Built and Ready to Use

- **Plugin Size**: 134KB
- **Build Status**: ✅ SUCCESS
- **Location**: `build/Release/AltDPadPlugin.dll`

## New DPad Methods

The plugin adds these methods to UEVR's DPad system:

- **Method 6**: `Right Joystick Press + Left Joystick (Disables R3)`
  - Hold R3 button and use left stick for DPad input
  - R3 button functionality is disabled while active

- **Method 7**: `Left Joystick Press + Right Joystick (Disables L3)`
  - Hold L3 button and use right stick for DPad input
  - L3 button functionality is disabled while active

## How It Works

### Core Functionality
- **XInput Interception**: Monitors controller input in real-time
- **Config Monitoring**: Watches UEVR config for methods 6 & 7
- **Automatic Setup**: Sets method 6 as default after 3 seconds
- **Conflict Prevention**: Disables clicked buttons when DPad is active

### Technical Approach
Since modifying UEVR's dropdown menu directly proved complex, this plugin:
1. **Provides the functionality** regardless of what's shown in the dropdown
2. **Monitors the config** for when users set VR_DPadShiftingMethod to 6 or 7
3. **Handles the new methods** through XInput state modification
4. **Logs helpful information** about how to use the new methods

## Installation

1. **Copy the plugin**:
   ```bash
   copy "build/Release/AltDPadPlugin.dll" "C:/path/to/your/game/UEVR/plugins/"
   ```

2. **Launch your game** with UEVR injected

3. **Enable the plugin**:
   - Open UEVR overlay (Insert key)
   - Go to "Plugins" tab
   - Enable "AltDPadPlugin"

## Usage

### Quick Start
1. **Install the plugin** (see above)
2. **The plugin automatically sets method 6** as default after 3 seconds
3. **Use R3 + left stick** for DPad input
4. **Change methods** using UEVR console or config file

### Manual Configuration

#### Via UEVR Console
Open UEVR console and type:
```
VR_DPadShiftingMethod = 6  # R3 + Left Stick
VR_DPadShiftingMethod = 7  # L3 + Right Stick
```

#### Via Config File
Edit `UEVR.cfg` and set:
```ini
VR_DPadShiftingMethod=6
```

#### Via UEVR Dropdown (Fallback)
The main UEVR dropdown will still show the original 6 methods (0-5), but if you set the config value to 6 or 7, the plugin will handle it correctly.

## Controller Behavior

### Method 6 (R3 + Left Stick)
- **Activation**: Hold R3 and move left stick
- **DPad Output**: Left stick movement → DPad directions  
- **Button Handling**: R3 disabled, left stick axis zeroed
- **Conflict Prevention**: Only active when R3 held without L3

### Method 7 (L3 + Right Stick)
- **Activation**: Hold L3 and move right stick
- **DPad Output**: Right stick movement → DPad directions
- **Button Handling**: L3 disabled, right stick axis zeroed  
- **Conflict Prevention**: Only active when L3 held without R3

## Plugin Status & Logging

The plugin provides detailed logging in UEVR.log:

```
UEVR Config Hook Plugin initializing...
Config Hook Plugin initialized successfully!
This plugin enables extended DPad methods 6 & 7
Method 6: R3 + Left Stick  |  Method 7: L3 + Right Stick
Configure via UEVR console: VR_DPadShiftingMethod = 6 or 7
[ConfigHook] Extended DPad methods are now available!
[ConfigHook] Set default method to 6 (R3 + Left Stick)
[ConfigHook] Extended DPad method 6 selected: Right Joystick Press + Left Joystick (Disables R3)
```

## Build Information

### Successful Build Details
- **CMake Configuration**: ✅ Completed
- **Compilation**: ✅ No errors
- **Linking**: ✅ Successful
- **Output**: `AltDPadPlugin.dll` (134,656 bytes)
- **Dependencies**: Windows API (psapi, user32, kernel32)

### Build Command
```bash
cmake -B build
cmake --build build --config Release --target alt_dpad_plugin
```

## Troubleshooting

### Plugin Not Loading
- Ensure DLL is in `<Game>/UEVR/plugins/` directory
- Check UEVR.log for initialization messages
- Verify UEVR version supports plugins (2.38+)

### Methods Not Working
- Check UEVR.log to confirm plugin loaded
- Verify VR_DPadShiftingMethod is set to 6 or 7
- Test controller L3/R3 buttons work in other contexts
- Ensure only one joystick click held at a time

### Config Issues
- Plugin sets method 6 as default after 3 seconds
- Manually set via console: `VR_DPadShiftingMethod = 6`
- Check UEVR has write permissions for config file

## Technical Implementation

### Core Components
- **Config Monitoring**: Checks UEVR config every 500ms
- **XInput Processing**: Intercepts controller state 60+ times per second  
- **State Management**: Tracks button states and method changes
- **Conflict Resolution**: Prevents simultaneous L3+R3 activation

### Performance
- **Minimal Overhead**: Only processes when methods 6/7 are active
- **Efficient Polling**: Config checks limited to 500ms intervals
- **No GUI Overhead**: Text-based logging and config interaction

### Memory Usage
- **Static Memory**: Fixed-size arrays and simple state variables
- **No Dynamic Allocation**: Avoids memory leaks and fragmentation
- **Small Footprint**: ~134KB DLL with minimal runtime overhead

## Compatibility

- **UEVR Versions**: 2.38+ (tested)
- **VR Headsets**: All OpenVR/OpenXR compatible
- **Controllers**: Any with clickable joysticks (L3/R3)
- **Games**: All UEVR-supported games
- **Other Plugins**: No known conflicts

## Future Enhancements

Potential improvements:
- GUI configuration panel for real-time method switching
- Custom deadzone settings for joystick-to-DPad conversion
- Haptic feedback when DPad modes activate
- Integration with UEVR's dropdown menu (requires core modifications)

## Credits

- Based on TurnChangesToPlugin fork functionality
- Uses UEVR Plugin API for seamless integration
- Implements methods 6 & 7 from the original concept
- Thanks to praydog for the UEVR framework and plugin system

## License

This plugin follows the same license as UEVR itself.

---

**Ready to use!** Copy `build/Release/AltDPadPlugin.dll` to your UEVR plugins folder and enjoy the new DPad methods. 
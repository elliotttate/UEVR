# UEVR Button Remapper Plugin

A comprehensive button remapping plugin for UEVR that allows you to remap controller buttons to other buttons or keyboard keys, including support for button combinations.

## Features

- **Button-to-Button Remapping**: Remap any controller button to another controller button
- **Button-to-Key Remapping**: Map controller buttons to keyboard keys
- **Combination Support**: Create mappings that require multiple buttons pressed together
- **Visual Configuration**: Easy-to-use ImGui interface for creating and editing mappings
- **Live Recording**: Record button combinations in real-time
- **Persistent Configuration**: Save and load your mappings from JSON files
- **VR Support**: Full VR rendering support for configuration in VR
- **Enable/Disable Mappings**: Toggle individual mappings without deleting them
- **Block Original Buttons**: Option to block original button inputs when a combo is active
- **VR-First Design**: UI always renders in VR when available, press F9 to toggle visibility
- **Works with UEVR Closed**: Button recording works even when UEVR overlay is closed

## Building

1. Ensure you have Visual Studio 2022 and CMake installed
2. Clone or download this plugin to your UEVR examples directory
3. Run `build.bat` from the plugin directory
4. The compiled DLL will be in `build\bin\Release\ButtonRemapperPlugin.dll`

## Installation

1. Copy `ButtonRemapperPlugin.dll` to your UEVR plugins folder
2. Launch UEVR and inject it into your game
3. The Button Remapper Configuration window will appear

## Usage

### Creating a New Mapping

1. Click "New Mapping" to create a new button mapping
2. Give your mapping a descriptive name
3. Click "Record Combo" and press the button(s) you want to map from
4. Choose whether to output to a controller button or keyboard key
5. Select the desired output from the dropdown
6. Click "Save Config" to save your changes

### Example Mappings

- **Simple Remap**: A → B (pressing A will act as B)
- **Key Mapping**: X → Space (pressing X will press spacebar)
- **Combo Mapping**: Start + Back → F5 (pressing both triggers F5)
- **Multi-button**: LB + RB + A → Escape

### Configuration File

Your mappings are saved to `button_remapper.txt` in the UEVR persistent directory. The format is simple text-based for easy editing:

```
# Button Remapper Configuration
# Format: name|enabled|block_original|combo_buttons|output_type|output_value
# Example: A to Space|1|1|XINPUT_GAMEPAD_A|key|VK_SPACE

A to Space|1|1|XINPUT_GAMEPAD_A|key|VK_SPACE
Quick Save|1|1|XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_BACK|key|VK_F5
X to B|0|1|XINPUT_GAMEPAD_X|button|XINPUT_GAMEPAD_B
```

Each line represents a mapping with pipe-separated values:
- **name**: Display name for the mapping
- **enabled**: 1 for enabled, 0 for disabled
- **block_original**: 1 to block original buttons when combo is active, 0 to pass through
- **combo_buttons**: Comma-separated list of buttons that trigger the mapping
- **output_type**: Either "button" or "key"
- **output_value**: The button or key to output

## Supported Buttons

### Controller Buttons
- A, B, X, Y
- LB (Left Bumper), RB (Right Bumper)
- Start, Back
- L3 (Left Stick Click), R3 (Right Stick Click)
- DPad Up, Down, Left, Right

### Keyboard Keys
- All letters (A-Z)
- Numbers (0-9)
- Function keys (F1-F12)
- Special keys: Space, Enter, Escape, Tab, Ctrl, Shift, Alt

## Controller Navigation

The plugin supports full controller navigation:
- **DPad/Left Stick**: Navigate through UI elements
- **A Button**: Select/Activate
- **B Button**: Cancel/Back (also cancels combo recording)
- **X Button**: Text input
- **Y Button**: Menu
- **LB/RB**: Switch between tabs/sections

## Notes

- Mappings are processed in order, so be careful with overlapping combinations
- When a combo is active, the original buttons are suppressed to prevent conflicts
- The plugin hooks into XInput, so it works with any XInput-compatible controller
- Configuration changes take effect immediately without needing to restart

## Known Issues

- **UI may disappear when UEVR overlay closes**: This is due to a bug in UEVR where `PluginLoader::on_present()` is missing the `override` keyword. Until this is fixed in UEVR, the plugin UI may only be visible when the UEVR overlay is open.

## Usage Tips

- **Recording Combos**: If button inputs aren't detected during combo recording, close the UEVR overlay window first
- **VR Mode**: The UI automatically renders in your VR headset when in VR mode
- **Toggle UI**: Press F9 at any time to show/hide the configuration window
- **Block Original**: Enable "Block original buttons" to prevent the original buttons from triggering when your combo is active

## Troubleshooting

- **Mappings not working**: Ensure the mapping is enabled (checkbox checked)
- **Can't see UI**: Press F9 to toggle visibility (works even with UEVR overlay closed)
- **Button recording not working**: Close the UEVR overlay window and try again
- **Config not saving**: Check write permissions for UEVR persistent directory
- **Buttons conflicting**: Review your mappings for overlapping combinations

## License

This plugin is provided as-is for use with UEVR. Feel free to modify and distribute as needed.
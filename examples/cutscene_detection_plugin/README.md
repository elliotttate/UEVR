# UEVR Cutscene Detection Plugin

A UEVR plugin that automatically detects when the player is in a cutscene and applies VR-friendly modifications to improve the experience.

## Features

### ✅ **Current Working Features**
- **Cutscene Detection**: Simulates cutscene detection (currently using a timer-based simulation)
- **Camera Offset Management**: Stores and restores camera offset values during cutscenes
- **Smooth Transitions**: Configurable lerp duration for smooth camera restoration
- **Decoupled Pitch Control**: Option to disable decoupled pitch during cutscenes
- **Camera Shake Dampening**: Reduces camera shake during cutscenes (UI only)
- **FOV-to-Distance Compensation**: Advanced feature that adjusts camera distance based on FOV changes
- **ImGui Interface**: Clean, VR-friendly UI for configuration and status monitoring
- **Comprehensive Logging**: Detailed logging for debugging and monitoring

### 🔧 **FOV-to-Distance Compensation**
This advanced feature automatically adjusts the camera's forward offset based on changes in the game's field of view:

- **Baseline FOV**: Set your normal gameplay FOV (default: 80°)
- **FOV Offset Scale**: Controls how much the camera moves per degree of FOV change (default: 1.0 cm/deg)
- **Real-time Monitoring**: Shows current FOV, offset, and delta values
- **Manual Controls**: Buttons to log current FOV and refresh baseline
- **Configurable Logging**: Set how often FOV changes are logged

### 📋 **Configuration Options**
- **Enable Smooth Movement**: Smooth camera restoration after cutscenes
- **Lerp Duration**: How long the smooth transition takes (0.1-10.0 seconds)
- **Disable Decoupled Pitch**: Automatically disable decoupled pitch during cutscenes
- **Dampen Camera Shake**: Reduce camera shake during cutscenes
- **Shake Threshold**: Sensitivity for camera shake detection (0.1-20.0 degrees)
- **Enable FOV Compensation**: Turn on FOV-to-distance compensation
- **FOV Log Interval**: How often to log FOV changes (0.1-5.0 seconds)

### 🎮 **Manual Controls**
- **Force Check**: Manually trigger cutscene detection
- **Reset**: Reset all plugin state
- **Force Restore Now**: Immediately restore camera offset
- **Log Current FOV**: Manually log current FOV values
- **Refresh Baseline FOV**: Update baseline FOV to current value

## Installation

1. **Build the Plugin**:
   ```bash
   cd cutscene_detection_plugin
   .\build.bat
   ```

2. **Install**:
   - Copy `build\Release\CutsceneDetectionPlugin.dll` to your game's `UEVR\plugins\` directory
   - The plugin will be automatically loaded when UEVR starts

3. **Usage**:
   - Start your game with UEVR
   - Press the overlay key to access the plugin UI
   - Configure settings as needed
   - The plugin will automatically detect cutscenes and apply modifications

## Technical Details

### Architecture
- **C++ Plugin**: Built using UEVR's plugin framework
- **ImGui Interface**: VR-friendly UI with proper styling
- **Error Handling**: Comprehensive try-catch blocks with detailed logging
- **Thread Safety**: Proper mutex protection for UI operations

### API Integration
- **UEVR API**: Uses `API::get()->param()->vr` for VR functionality
- **Renderer Support**: Compatible with both D3D11 and D3D12
- **Settings Management**: Configurable through ImGui interface

### Cutscene Detection
Currently uses a simulation approach:
- Toggles cutscene state every 300 frames (5 seconds at 60fps)
- In a real implementation, this would check for `CineCameraActor` instances
- Can be extended to use actual Unreal Engine object detection

### FOV Compensation Algorithm
1. **Baseline Establishment**: Set normal gameplay FOV as baseline
2. **Delta Calculation**: `ΔFOV = Current FOV - Baseline FOV`
3. **Offset Computation**: `Offset = ΔFOV × Scale Factor`
4. **Application**: Adjust camera forward offset by computed amount
5. **Clamping**: Limit offset to reasonable bounds (-50 to +50 cm)

## Development Status

### ✅ **Completed**
- Basic plugin framework and UI
- Cutscene detection simulation
- Camera offset management
- Smooth transitions (lerp)
- FOV-to-distance compensation
- Comprehensive error handling
- ImGui interface with VR-friendly styling

### 🔄 **In Progress**
- Real cutscene detection using Unreal Engine objects
- Actual VR API integration for camera offset changes
- Settings persistence through UEVR's mod system

### 📋 **Planned Features**
- **Real Cutscene Detection**: Detect actual `CineCameraActor` instances
- **UObjectHook Integration**: Disable object hooks during cutscenes
- **2D Mode Switching**: Automatically switch to 2D mode for cutscenes
- **Advanced FOV Curves**: Non-linear FOV-to-distance mapping
- **Per-Game Profiles**: Game-specific configuration presets

## Troubleshooting

### Common Issues
1. **Plugin Not Loading**: Ensure the DLL is in the correct `UEVR\plugins\` directory
2. **UI Not Appearing**: Check that ImGui is properly initialized
3. **Settings Not Saving**: Currently uses default values; persistence coming soon

### Logging
The plugin provides extensive logging:
- All major operations are logged with `[CutscenePlugin]` prefix
- Error conditions are logged with full exception details
- FOV compensation provides detailed delta and offset information

### Performance
- Minimal performance impact with comprehensive error handling
- UI updates only when overlay is active
- Cutscene detection runs every frame but is lightweight

## Contributing

This plugin is designed to be extensible. Key areas for contribution:
- Real cutscene detection implementation
- Additional VR API integrations
- Game-specific optimizations
- Enhanced FOV compensation algorithms

## License

This plugin follows the same license as the UEVR framework. 
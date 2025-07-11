# Enhanced FPS Counter Plugin for UEVR

A robust C++ plugin for UEVR that provides real-time FPS monitoring with beautiful ImGui graphing, performance statistics, and comprehensive analysis tools.

## Features

### 🎯 Real-time FPS Monitoring
- **Live FPS Display**: Shows current FPS with color-coded performance indicators
- **Frame Time Analysis**: Displays frame time in milliseconds with VR experience indicators
- **Smooth Graph Visualization**: Beautiful real-time plotting using ImGui

### 📊 Advanced Statistics
- **Min/Max/Average FPS**: Comprehensive performance metrics
- **1% and 0.1% Lows**: Industry-standard performance indicators
- **Performance Ratings**: Automatic classification (Excellent/Good/Fair/Poor)
- **VR Experience Indicators**: Shows if performance is suitable for VR

### 🎨 Beautiful UI
- **VR-Friendly Design**: Optimized for VR headset viewing
- **Color-Coded Performance**: Green/Yellow/Orange/Red based on FPS levels
- **Collapsible Sections**: Organized interface with expandable details
- **Recent Values Table**: Shows last 10 FPS readings with status

### 💾 Data Export
- **Text Export**: Save detailed FPS data to timestamped files
- **CSV Export**: Export data for analysis in spreadsheet applications
- **Auto-save Option**: Automatically save data when plugin is closed

### ⚙️ Customizable Settings
- **Adjustable Graph Length**: 30-300 seconds of history
- **Configurable Display**: Toggle overlay visibility
- **Persistent Settings**: Settings saved between sessions

## Installation

### Prerequisites
- UEVR installed and working
- Visual Studio 2022 with C++ development tools
- CMake 3.24 or higher

### Building the Plugin

1. **Clone or download the plugin files** to your UEVR directory
2. **Open Command Prompt** in the `fps_counter_plugin` directory
3. **Run the build script**:
   ```batch
   build.bat
   ```
4. **Locate the built DLL**: `build\bin\Release\FPSCounterPlugin.dll`

### Manual Build (Alternative)
```batch
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Usage

### Loading the Plugin
1. **Copy the DLL** to your UEVR plugins directory
2. **Restart UEVR** or reload plugins
3. **Open the FPS Counter** from the UEVR overlay menu

### Interface Overview

#### Main Statistics Panel
- **Current FPS**: Live FPS with color coding
- **Frame Time**: Time per frame in milliseconds
- **Min/Max/Average**: Performance range and average
- **1%/0.1% Lows**: Performance consistency indicators

#### Performance Indicators
- **Overall Rating**: Automatic performance classification
- **VR Experience**: Indicates if performance is VR-suitable
- **Motion Sickness Warning**: Alerts for potentially problematic performance

#### FPS Graph
- **Real-time Plot**: Beautiful line graph showing FPS over time
- **Color-coded Performance**: Visual indication of performance levels
- **Recent Values Table**: Detailed breakdown of recent readings

#### Controls
- **Reset Stats**: Clear all statistics and start fresh
- **Toggle Engine FPS**: Toggle Unreal Engine's built-in FPS counter
- **Save Data**: Export current data to text file
- **Export CSV**: Export data in CSV format for analysis

### Settings
- **Graph Length**: Adjust how many seconds of history to display (30-300)
- **Auto-save**: Automatically save data when plugin closes
- **Show in Overlay**: Toggle plugin visibility in UEVR overlay

## Performance Indicators

### FPS Color Coding
- **🟢 Green (90+ FPS)**: Excellent performance
- **🟡 Yellow (60-89 FPS)**: Good performance
- **🟠 Orange (30-59 FPS)**: Fair performance
- **🔴 Red (<30 FPS)**: Poor performance

### VR Experience Levels
- **Smooth VR Experience**: ≤11.1ms frame time (90+ FPS equivalent)
- **Acceptable VR Experience**: ≤16.7ms frame time (60+ FPS equivalent)
- **May cause motion sickness**: >16.7ms frame time

### Performance Ratings
- **Excellent**: Average FPS ≥90
- **Good**: Average FPS 60-89
- **Fair**: Average FPS 30-59
- **Poor**: Average FPS <30

## Data Export

### Text Export Format
```
FPS Data Export
Generated: 2024-01-15 14:30:25

Current FPS: 72.3
Min FPS: 45.2
Max FPS: 89.7
Avg FPS: 68.4
1% Low: 52.1
0.1% Low: 48.3

FPS History:
0,72.3
1,71.8
2,73.1
...
```

### CSV Export Format
```csv
Time(s),FPS,FrameTime(ms),Status
0,72.3,13.8,Good
1,71.8,13.9,Good
2,73.1,13.7,Good
...
```

## Troubleshooting

### Common Issues

**Plugin doesn't load**
- Ensure all dependencies are available (UEVR headers, renderlib, ImGui)
- Check that the DLL was built for the correct architecture (x64)
- Verify UEVR plugin loading is enabled

**Graph not displaying**
- Check if ImGui initialization succeeded
- Ensure renderer type is supported (D3D11/D3D12)
- Verify window handle is valid

**Performance issues**
- Reduce graph length if experiencing lag
- Disable auto-save if not needed
- Check if other plugins are conflicting

### Debug Information
The plugin logs detailed information to UEVR's log system:
- Initialization progress
- ImGui setup status
- Error conditions and exceptions
- Data export confirmations

## Technical Details

### Architecture
- **C++20**: Modern C++ features for robust implementation
- **ImGui**: Immediate mode GUI for responsive interface
- **UEVR Plugin API**: Proper integration with UEVR framework
- **Thread-safe**: Mutex-protected UI rendering

### Performance Considerations
- **Efficient Data Structures**: Circular buffer for FPS history
- **Minimal Overhead**: Lightweight FPS calculation
- **Smart Updates**: Statistics updated only when needed
- **Memory Efficient**: Automatic cleanup of old data

### Supported Renderers
- **DirectX 11**: Full support with ImGui integration
- **DirectX 12**: Full support with ImGui integration
- **Future**: OpenGL/Vulkan support planned

## Contributing

### Development Setup
1. **Fork the repository**
2. **Create a feature branch**
3. **Make your changes**
4. **Test thoroughly**
5. **Submit a pull request**

### Code Style
- Follow the existing code style
- Use proper error handling
- Add comprehensive logging
- Include documentation for new features

## License

This plugin is provided as-is for educational and personal use. Please respect UEVR's licensing terms and conditions.

## Support

For issues, questions, or feature requests:
1. Check the troubleshooting section
2. Review UEVR documentation
3. Check plugin logs for error messages
4. Create an issue with detailed information

---

**Note**: This plugin is designed to work with UEVR and may require updates for compatibility with newer UEVR versions. 
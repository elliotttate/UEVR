# FPS Plotter Plugin for UEVR

This plugin provides advanced FPS plotting capabilities for UEVR using native ImGui plotting functions.

## Features

- **Real-time FPS Monitoring**: Tracks FPS at frame level with high precision
- **Native ImGui PlotLines**: Smooth line charts showing FPS over time
- **Native ImGui PlotHistogram**: Distribution analysis of FPS values
- **Configurable Display**: Toggle different plot types and statistics
- **Auto-scaling**: Automatically adjusts plot ranges based on data
- **Data Export**: Export FPS data to CSV files for external analysis
- **Performance Statistics**: Current, average, min, and max FPS tracking
- **Customizable History**: Adjustable history length (60-1800 frames)

## Building the Plugin

### Prerequisites
- Visual Studio 2019 or later
- CMake 3.16 or later
- UEVR source code with ImGui submodule

### Build Steps
1. Navigate to the plugin directory:
   ```bash
   cd plugins/FPSPlotterPlugin
   ```

2. Create build directory:
   ```bash
   mkdir build
   cd build
   ```

3. Configure with CMake:
   ```bash
   cmake ..
   ```

4. Build the plugin:
   ```bash
   cmake --build . --config Release
   ```

5. The built DLL will be in the `bin/` directory.

## Installation

1. Copy `FPSPlotterPlugin.dll` to your game's `UEVR/plugins/` folder
2. Restart UEVR
3. The plugin will automatically load and display its FPS plotting window

## Usage

### Main Window
The plugin creates a window titled "FPS Plotter Plugin" with the following sections:

- **Controls**: Toggle features and adjust settings
- **Statistics**: Real-time FPS statistics
- **Line Plot**: FPS over time visualization
- **Histogram**: FPS distribution analysis
- **Export**: Data export functionality

### Controls
- **Auto Scale**: Automatically adjust plot ranges based on data
- **Show Line Plot**: Toggle the FPS line chart
- **Show Histogram**: Toggle the FPS distribution histogram
- **Show Stats**: Toggle statistics display
- **Min/Max FPS**: Manual plot range control (when auto-scale is disabled)
- **History Size**: Number of frames to keep in memory (60-1800)

### Data Export
- Enter a filename in the text field
- Click "Export to CSV" to save FPS data
- The CSV file includes frame number, timestamp, and FPS values

### Performance
The plugin is designed to be lightweight and efficient:
- Minimal memory usage with configurable history size
- Efficient data structures for real-time updates
- Non-blocking UI updates

## Technical Details

### FPS Calculation
- Uses high-resolution clock for precise timing
- Calculates FPS as 1/delta_time for each frame
- Handles edge cases (zero delta time)

### Data Storage
- Uses std::deque for efficient front/back operations
- Automatic history size management
- Thread-safe data structures

### ImGui Integration
- Native ImGui::PlotLines for line charts
- Native ImGui::PlotHistogram for distribution analysis
- Responsive UI with collapsible sections

## Troubleshooting

### Plugin Not Loading
- Ensure the DLL is in the correct `UEVR/plugins/` directory
- Check that all dependencies are available
- Verify UEVR version compatibility

### Build Errors
- Ensure ImGui submodule is properly initialized
- Check that all include paths are correct
- Verify Visual Studio and CMake versions

### Performance Issues
- Reduce history size if experiencing lag
- Disable unused plot types
- Check system resources

## License

This plugin is provided under the MIT License. See the source code for full license details.

## Contributing

Contributions are welcome! Please feel free to submit issues, feature requests, or pull requests. 
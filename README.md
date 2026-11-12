# Mini OSD

An ultra-lightweight, zero-injection FPS and hardware monitor overlay for Windows.

Mini OSD provides a sleek, NVIDIA-style one-line HUD showing FPS, GPU usage, GPU temperature, CPU usage, and CPU temperature with negligible system overhead.

---

## ✨ Features

- **Zero-Injection Frame Tracking**: Uses Event Tracing for Windows (ETW) to capture present calls across DXGI, Direct3D 9, and DxgKrnl (supporting DirectX 9/10/11/12, OpenGL, and Vulkan). Safe for games with strict anti-cheat software (no DLL injection, no hooking).
- **GPU Stats via NVML**: Direct integration with `nvml.dll` (ships with NVIDIA drivers) for accurate GPU utilization and temperature readouts.
- **CPU Stats**:
  - CPU usage calculated via `GetSystemTimes`.
  - CPU temperature readout via `PawnIO` driver and `IntelMSR.bin` (using the same MSR IOCTL methods as LibreHardwareMonitor).
- **Discord-Style Window Picker**: Click "Capture..." from the tray menu to open an interactive picker featuring live DWM thumbnails for open windows or select "Entire screen" to follow whatever app is in the foreground.
- **Clean NVIDIA-Style Overlay**: Rendered with per-pixel alpha and soft dark outline using the Windows native **Bahnschrift** font (DIN typography).
- **EcoQoS Efficiency**: Configured for Windows efficiency scheduling (EcoQoS) to ensure it never interferes with game performance.
- **System Tray Controls**: Right-click or left-click the tray icon to toggle individual metrics on or off, pick a target window, hide the overlay, or exit.

---

## 🚀 Usage

1. Download or clone this repository.
2. Run **`osd.exe`** (requires Administrator privileges for ETW tracing and CPU temperature reading).
3. The overlay will appear at the top-right of your screen or active game window.
4. Manage settings via the **System Tray**:
   - **Capture**: Select an active game window or track the foreground window.
   - **Toggle Metrics**: Turn FPS, GPU usage, GPU temp, CPU usage, and CPU temp on or off.
   - **Hide overlay**: Temporarily hide the overlay without exiting.

### Diagnostic Logging
Start `osd.exe` with the `/log` command-line switch to generate an `osd.log` file in the application directory:
```cmd
osd.exe /log
```

---

## 🛠️ Building from Source

### Prerequisites
- [MinGW-w64](https://www.mingw-w64.org/) / GCC or Clang (with `windres` support).
- Windows 10/11 SDK headers and libraries (`advapi32`, `shell32`, `dwmapi`).

### Build Instructions
Run the included build script:
```cmd
build.bat
```
Or execute the compilation commands directly:
```cmd
windres osd.rc -O coff -o osd.res
gcc -Os -s -Wall -mwindows -municode osd.c osd.res -ladvapi32 -lshell32 -ldwmapi -o osd.exe
```

---

## 📁 Repository Structure

- `osd.c` - Core application source code (ETW tracing, NVML, PawnIO, DWM picker, HUD rendering).
- `osd.manifest` - Windows application manifest requiring administrative execution level and DPI awareness.
- `osd.rc` - Resource definition embedding the manifest.
- `IntelMSR.bin` - Module used for MSR CPU temperature queries.
- `build.bat` - Compilation script.
- `osd.ini` - Persistent configuration file.
- `osd.exe` - Precompiled executable ready to run.

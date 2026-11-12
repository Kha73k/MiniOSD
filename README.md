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

## 🚀 Installation & Usage

### Option 1: Installer (Recommended)
Download and run **`MiniOSD-Setup.exe`** to install Mini OSD with Start Menu shortcuts and uninstaller support.

### Option 2: Standalone Portable
1. Download or clone this repository.
2. Run **`osd.exe`** directly from the folder (ensure `IntelMSR.bin` remains in the same directory).
3. Mini OSD requests Administrator privileges upon startup (required for ETW tracing and CPU temperature reading).

### Controls & Options
- The overlay appears at the top-right of your screen or active window.
- Right-click or left-click the **System Tray icon** to:
  - **Capture**: Pick a specific window with live DWM thumbnails or track the active foreground app.
  - **Toggle Metrics**: Enable or disable FPS, GPU usage, GPU temp, CPU usage, or CPU temp.
  - **Hide overlay**: Temporarily hide the HUD.
  - **Exit**: Close the overlay.

### Diagnostic Logging
Start `osd.exe` with the `/log` command-line switch to generate an `osd.log` file in the application directory:
```cmd
osd.exe /log
```

---

## 🛠️ Building from Source

### Prerequisites
- [MinGW-w64](https://www.mingw-w64.org/) / GCC or Clang (with `windres` support).
- [Inno Setup 6](https://jrsoftware.org/isdl.php) (optional, if compiling the installer).
- Windows 10/11 SDK headers and libraries (`advapi32`, `shell32`, `dwmapi`).

### Build Application Executable
Run the included build script:
```cmd
build.bat
```
Or execute the compilation commands directly:
```cmd
windres osd.rc -O coff -o osd.res
gcc -Os -s -Wall -mwindows -municode osd.c osd.res -ladvapi32 -lshell32 -ldwmapi -o osd.exe
```

### Build Setup Installer
To compile the `MiniOSD-Setup.exe` installer:
```cmd
installer\build.bat
```

---

## 📁 Repository Structure

- `MiniOSD-Setup.exe` - Windows setup installer.
- `osd.exe` - Standalone precompiled executable.
- `IntelMSR.bin` - Module used for MSR CPU temperature queries.
- `osd.c` - Core application source code (ETW tracing, NVML, PawnIO, DWM picker, HUD rendering).
- `osd.manifest` - Windows application manifest requiring administrative execution level and DPI awareness.
- `osd.rc` - Resource definition embedding the manifest.
- `build.bat` - Application compilation script.
- `osd.ini` - Persistent configuration file.
- `installer/` - Inno Setup packaging script (`miniosd.iss`) and build script (`build.bat`).

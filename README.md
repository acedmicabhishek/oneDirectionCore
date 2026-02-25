# OneDirectionCore

OneDirectionCore is a high-performance C-core library and .NET-based UI for advanced DSP and audio radar applications. This project captures system audio at the driver level and applies spectral analysis to detect 360-degree sound events.

## Features
- 360-degree audio radar with vector summation for precise azimuthal placement
- 7.1 surround sound multi-channel support
- Real-time classification of discrete audio events
- Transparent, OS-level click-through WPF overlay

---

## Technical Architecture
- Core DLL (`od_core.dll`): Written in C. Captures audio via WASAPI loopback (Windows) or PipeWire (Linux).
- DSP Layer (`dsp_windows.c`, `dsp.c`): Extracts features using FFT and identifies direction using per-channel energy vector mapping.
- UI/Overlay (`ODC-overlay-win.exe`, .NET Desktop): Pulls spatial data from the core library at 60Hz and renders the visual radar.

---

## Build Instructions (Windows)

The Windows application components require two separate compilation steps: building the native C dependencies with MSYS2/MinGW-w64, and publishing the .NET UI application.

### 1. Prerequisites
1. Install MSYS2 (https://www.msys2.org/).
2. Open the MSYS2 MINGW64 terminal.
3. Install the .NET 10.0 SDK for the UI (or equivalent supported version).

### 2. Build Native Dependencies (MSYS2 MINGW64)
Inside the MSYS2 MINGW64 terminal, update MSYS2 and install the required 64-bit MinGW toolchains. We build in 64-bit to align with the .NET win-x64 target.

```bash
# Update package databases
pacman -Syu

# Install GCC, Meson, Ninja, and GLFW
pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-glfw

# Navigate to project directory (change to your path)
cd /c/Users/YourName/Desktop/oneDirectionCore

# Configure build with Meson
meson setup build_msys

# Compile the native binaries (od_core.dll and ODC-overlay-win.exe)
meson compile -C build_msys
```

### 3. Build the .NET Application
To produce the final executable, copy the MSYS2 build outputs to the .NET project and compile. You can perform this step in a standard PowerShell or Command Prompt window.

```powershell
# Navigate to project root
cd C:\Users\YourName\Desktop\oneDirectionCore

# Copy native artifacts
Copy-Item "build_msys\od_core.dll" "ui\dotnet\OneDirectionCore\"
Copy-Item "build_msys\ODC-overlay-win.exe" "ui\dotnet\OneDirectionCore\"

# Publish single-file executable
cd ui\dotnet\OneDirectionCore
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
```

To run the application, launch `OneDirectionCore.exe` located within the publish output directory.

---

## Build Instructions (Linux)

Linux building relies strictly on system package managers.

### 1. Install Dependencies (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install libpipewire-0.3-dev libglfw3-dev libgtk-4-dev meson ninja-build build-essential
```

### 2. Compile Core
```bash
# In the project root
meson setup build
ninja -C build
```

---

## Configuration Requirements

For precise directional tracking on Windows, the system must output 7.1 surround sound coordinates to the driver.
1. Right-click the Windows speaker icon > Sound Settings.
2. Select your default output device > Format > Output: 7.1 Surround.
3. Inside the OneDirectionCore UI, select 8 channels.
The application will log `[Capture Windows] Final Format: 8 channels` on a successful startup.

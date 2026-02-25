# ODC


<p align="center">
  <a href="Index.html">
    <img src="https://img.shields.io/badge/DOWNLOAD-VISIT_WEB_PORTAL-00d2b4?style=for-the-badge&logo=googlechrome&logoColor=white" alt="Download Portal" />
  </a>
</p>

On Screen Display for Audio Directional clues
Made for people with disabilities or hard of hearing to help them enjoy Compititve games like PUBG or APEX LEGENDS


## Features
- 360-degree audio radar with vector summation for precise azimuthal placement
- 7.1 surround sound multi-channel support
- Real-time classification of discrete audio events
- Transparent, OS-level click-through WPF overlay
- Radar Map and full screen OSD available
- Support for Linux and Windows
![Platform Support](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)
![.NET Version](https://img.shields.io/badge/.NET-10.0-purple)
[![Download ODC](https://img.shields.io/badge/Download-ODC.msi-brightgreen?style=for-the-badge&logo=windows)](Download/ODC.msi)

---

## Build Instructions (Windows)

The Windows application components require two separate compilation steps: building the native C dependencies with MSYS2/MinGW-w64, and publishing the .NET UI application.

### 1. Prerequisites
1. Install MSYS2 (https://www.msys2.org/).
2. Open the MSYS2 MINGW64 terminal.
3. Install the .NET 10.0 SDK for the UI (or equivalent supported version) Higher is not compatible.

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

Linux building relies strictly on system package managers and its extremely simple.

### 1. Install Dependencies (Arch Linux)
```bash
sudo pacman -Syu
sudo pacman -S libpipewire-0.3-dev libglfw3-dev libgtk-4-dev meson ninja-build base-devel
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

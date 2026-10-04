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
[![Download ODC](https://img.shields.io/badge/Download-ODC--Setup.exe-brightgreen?style=for-the-badge&logo=windows)](Download/ODC-Setup.exe?raw=true)

---

## Download and use (Windows)

1. Download [ODC-Setup.exe](Download/ODC-Setup.exe?raw=true) (Windows 10/11 x64, version 1.1.0) and run it. It installs for the current user, so it does not ask for administrator rights. Windows may show a SmartScreen warning because the installer is not code-signed; choose "More info" > "Run anyway".
2. Start **OneDirectionCore** from the Start Menu and press **START ENGINE**. The radar appears in the corner of the screen.
3. In the game, set the display mode to **Fullscreen (Windowed)** or **Windowed**. In exclusive fullscreen no overlay can be drawn over the game.

The window opens in **Simple** mode, which only has start and stop. **Advanced** shows every option. Settings are saved in `%APPDATA%\OneDirectionCore\settings.cfg`.

## Surround Mode (sounds behind you)

A game only outputs 7.1 when the default playback device is a 7.1 device, and most headsets are stereo. From a stereo signal the radar can only tell left from right, so everything is drawn across the front.

Surround Mode, on by default, gets around this with a virtual 7.1 audio device:

- Install [FxSound](https://www.fxsound.com/) once. Its driver provides the virtual 7.1 device. Other virtual playback devices that offer a 7.1 format should be picked up the same way, but only FxSound has been tested.
- When the engine starts, ODC switches that device to 7.1 and makes it the default, so the game renders all eight channels into it. The radar reads those channels.
- ODC plays a stereo mix of the same audio on your real headphones or speakers, and the volume keys keep working. **Listen On** in Advanced picks the device if the automatic choice is wrong.
- When the engine stops, the previous audio settings are put back.

The FxSound app forces its device to stereo, so ODC closes that app while the engine runs and starts it again afterwards.

The line under the status says which mode is active. "Stereo only" means no virtual 7.1 device was found. A sound card that is already set to 5.1 or 7.1 is used directly.

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

# Optional: Dear ImGui is not vendored. Clone it only if you want the
# standalone ImGui overlay (ODC-overlay-win.exe); od_core.dll does not need it.
git clone https://github.com/ocornut/imgui 3rdparty/imgui

# Configure build with Meson
meson setup build_msys

# Compile the native binaries (od_core.dll, plus ODC-overlay-win.exe if ImGui is present)
meson compile -C build_msys
```

### 3. Build the .NET Application
To produce the final executable, copy the MSYS2 build outputs to the .NET project and compile. You can perform this step in a standard PowerShell or Command Prompt window.

```powershell
# Navigate to project root
cd C:\Users\YourName\Desktop\oneDirectionCore

# Copy native artifacts
Copy-Item "build_msys\od_core.dll" "ui\dotnet\OneDirectionCore\"
# Only if you built the optional ImGui overlay:
Copy-Item "build_msys\ODC-overlay-win.exe" "ui\dotnet\OneDirectionCore\"

# Publish single-file executable
cd ui\dotnet\OneDirectionCore
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
```

To run the application, launch `OneDirectionCore.exe` located within the publish output directory.

### 4. Build the Installer (optional)
With [Inno Setup 6](https://jrsoftware.org/isinfo.php) installed, compile the script after publishing. It picks the executable up from the publish directory and writes `Download\ODC-Setup.exe`.

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" winInstaller\installer.iss
```

---

## Build Instructions (Linux)

Linux building relies strictly on system package managers and its extremely simple.

### 1. Install Dependencies
Arch Linux:
```bash
sudo pacman -Syu
sudo pacman -S pipewire glfw gtk4 meson ninja base-devel
```

Debian / Ubuntu:
```bash
sudo apt install libpipewire-0.3-dev libglfw3-dev libgtk-4-dev meson ninja-build build-essential
```

### 2. Compile Core
```bash
# In the project root
# The Linux UI launches the ImGui overlay, so Dear ImGui is required here
git clone https://github.com/ocornut/imgui 3rdparty/imgui

meson setup build
ninja -C build
```

---

## Configuration Requirements

Directional tracking behind the listener needs the game to output 7.1. On Windows, Surround Mode (see above) sets this up automatically when a virtual 7.1 device is installed. To do it by hand instead:
1. Right-click the Windows speaker icon > Sound Settings.
2. Select your default output device > Format > Output: 7.1 Surround.

ODC detects a default device that is already surround and uses it directly. The status line shows "7.1 surround device" on a successful startup.

# 🎯 OneDirectionCore: Advanced Audio Directional Radar

**OneDirectionCore** is a high-performance, low-latency audio processing suite designed to provide real-time directional sound visualization. By capturing system audio at the driver level and applying advanced Digital Signal Processing (DSP), it translates auditory information into a visual radar overlay, enabling users to "see" sound sources in a full 360-degree space.

---

## � Project Motivation

In complex auditory environments—ranging from tactical gaming to accessibility for the hearing impaired—identifying the **precise direction** of a sound is critical. Most current solutions rely on stereo panning (Left/Right), which lacks depth and fails to distinguish between sounds in front of or behind the user.

This project was born out of the need for:
1.  **True 360° Awareness**: Moving beyond 180° "stereo" pans to a full circular spatial map.
2.  **Native 7.1 Integration**: Leveraging high-fidelity multi-channel audio data directly from the OS (WASAPI/PipeWire).
3.  **Low Latency**: Ensuring the visual radar updates synchronously with the audio the user hears.
4.  **Classification**: Not just *where* the sound is, but *what* it is (e.g., distinguishing a footstep from a gunshot).

---

## ⚙️ Technical Working & Internals

### 1. Multi-Channel Audio Capture
The application hooks into the system's default audio output device using a loopback mechanism:
-   **Windows**: Uses **WASAPI** (Windows Audio Session API) in loopback mode. It queries the system's "Mix Format" and requests an 8-channel (7.1 Surround) stream if available.
-   **Linux**: Uses **PipeWire**, mapping a specialized capture node to monitor system output.

### 2. Digital Signal Processing (DSP)
The captured audio frames are passed through a custom DSP engine (`od_core`):
-   **Spectral Analysis**: Every frame is transformed via a Fast Fourier Transform (FFT) to analyze frequency components.
-   **Vector Summation**: Unlike stereo mixers that discard channel data, our engine maps each of the 7.1 channels to a physical unit-vector (FL=315°, FC=0°, FR=45°, SL=270°, SR=90°, BL=225°, BR=135°). The energy in each channel weights its corresponding vector. Summing these vectors yields the **exact 360° azimuth** of the sound.
-   **Signature Matching**: Discrete frequency bands are monitored for specific patterns (signatures) to identify sound types.

### 3. Transparent Overlay
The visual component is a Windows Presentation Foundation (WPF) application that creates a transparent, click-through window. It pulls the processed spatial data from the native C library at 60Hz to provide a smooth, lag-free radar.

---

## � Detailed Build Guide (Windows) - "The Minute Details"

Follow these steps exactly to build the application from scratch on Windows.

### Phase 1: Environment Setup
1.  **Install MSYS2**:
    *   Download and install from [msys2.org](https://www.msys2.org/).
    *   Open the **MSYS2 MinGW 64-bit** terminal (blue icon).
    *   Run `pacman -Syu` to update. Restart the terminal if prompted, then run it again.
2.  **Install Native Build Tools**:
    In the MSYS2 terminal, run:
    ```bash
    pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-glfw
    ```
3.  **Install .NET SDK**:
    *   Download and install the latest **.NET 10.0 SDK** (or 8.0/9.0) from the [Microsoft .NET Website](https://dotnet.microsoft.com/download).

### Phase 2: Compile the Native Core (`od_core.dll`)
1.  Open your **MSYS2 MinGW 64-bit** terminal.
2.  Navigate to your project folder (e.g., `cd /c/Users/YourName/Desktop/oneDirectionCore`).
3.  Run the following commands:
    ```bash
    # Set up the build directory
    meson setup build_msys

    # Compile the project
    meson compile -C build_msys
    ```
4.  Verification: Ensure `build_msys/od_core.dll` and `build_msys/ODC-overlay-win.exe` exist.

### Phase 3: Build the UI & Integrate
1.  Open **PowerShell** or a **Command Prompt**.
2.  Navigate to the project root.
3.  Copy the compiled native files into the .NET project directory:
    ```powershell
    Copy-Item "build_msys\od_core.dll" "ui\dotnet\OneDirectionCore\"
    Copy-Item "build_msys\ODC-overlay-win.exe" "ui\dotnet\OneDirectionCore\"
    ```
4.  Build the final application:
    ```powershell
    cd ui\dotnet\OneDirectionCore
    dotnet build -c Release
    ```
5.  (Optional) Create a single-file executable:
    ```powershell
    dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
    ```

---

## � Detailed Build Guide (Linux)

### 1. Install Dependencies
You must have the development headers for PipeWire and GLFW. On Ubuntu/Debian:
```bash
sudo apt update
sudo apt install libpipewire-0.3-dev libglfw3-dev libgtk-4-dev meson ninja-build build-essential
```

### 2. Compile
```bash
# From the project root
meson setup build
meson compile -C build
```

---

## 🖥️ Usage & Configuration

### For 7.1 Surround Sound (Critical)
To get the most out of the 360° radar, your system MUST be outputting surround sound.
1.  **Windows**: Right-click the Speaker icon → **Sound Settings** → Select your device → **Format** → Choose **7.1 Surround**.
2.  **Application**: Select "8 Channels" in the UI dropdown.
3.  **Verification**: The console will log `[Capture Windows] Final Format: 8 channels`.

### Controls
-   **Sensitivity**: Lower values detect quieter sounds; higher values reduce background noise.
-   **Separation**: Prevents multiple sounds from "jittering" by merging sources within a certain degree range.
-   **Radar Size**: Adjusts the scale of the overlay.

---

## ❓ Troubleshooting

-   **"od_core.dll not found"**: Ensure you copied the DLL from `build_msys` into the same folder as `OneDirectionCore.exe`.
-   **Radar is stuck at Stereo (180°)**: Verify that the `Final Format` log shows 8 channels. If it shows 2, Windows is downmixing your audio before it reaches the driver.
-   **Overlay doesn't appear**: Verify that `ODC-overlay-win.exe` is in the application directory.

---

## ⚖️ License
Distributed under the MIT License. Developed with ❤️ for the tactical audio community.

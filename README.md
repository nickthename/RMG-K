# RMG-K (Rosalie's Mupen GUI - Kaillera)

Rosalie's Mupen GUI is a free and open-source mupen64plus front-end written in C++.
This is a fork of RMG with Kaillera netplay support for N64 online multiplayer.
It offers a simple-to-use user interface.

## Features added:
### N02 Client
<p align="center">
<img width="494" height="329" alt="image" src="https://github.com/user-attachments/assets/b7ae15a1-dae1-4bf0-ba57-64dac1f4d35d" />
</p>
<p align="center">
<img width="760" height="541" alt="image" src="https://github.com/user-attachments/assets/3afa1327-c5ff-4367-ba61-756657be8346" /></p>

### Frame Delay Override (Previously Ping Spoofing)

This allows the users to set their own frame delay

0 = Server sets frame delay on game start

1-9 = sets 1, 2, 3 frames of delay and so on.

### Connection Handling
- Keeps Kaillera connection alive after emulation ends (drop) for game restarts
- Players can restart games after dropping without reconnecting to server

### NRage Input Similarities
RMG-Input now uses independent per-axis scaling similar to the [USBtoN64v2](https://github.com/Ownasaurus/USBtoN64v2) adapter and N-Rage input plugin:
- Configurable range slider (0-100%) with default 66% to match N-Rage
- Linear scale: 100% = 127 (protocol max)
- Per-axis deadzone handling instead of circular deadzone
  <img width="1059" height="736" align="center" alt="Screenshot 2026-01-16 181813" src="https://github.com/user-attachments/assets/eacacb9b-f828-4486-a0f0-a8b539c8951f" />



## Showcase

<p align="center">
<img width="729" height="624" alt="image" src="https://github.com/user-attachments/assets/d5d6a703-18d0-4085-81ba-08b9b8a83336" />
</p>
<p align="center">
<img width="642" height="620" alt="image" src="https://github.com/user-attachments/assets/155f4b07-8272-4251-81b2-13cecf937f10" />

## Building

#### Linux
* Portable Debian/Ubuntu

  ```bash
  sudo apt-get -y install cmake libusb-1.0-0-dev libhidapi-dev libsamplerate0-dev libspeex-dev libminizip-dev libsdl3-dev libfreetype6-dev libgl1-mesa-dev libglu1-mesa-dev pkg-config zlib1g-dev binutils-dev libspeexdsp-dev qt6-base-dev qt6-websockets-dev libqt6svg6-dev libvulkan-dev build-essential nasm git zip ninja-build
  ./Source/Script/Build.sh Release
  ```
  
* Portable Fedora
  ```bash
  sudo dnf install libusb1-devel hidapi-devel libsamplerate-devel minizip-compat-devel SDL3-devel freetype-devel mesa-libGL-devel mesa-libGLU-devel pkgconfig zlib-ng-devel binutils-devel speexdsp-devel qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwebsockets-devel vulkan-devel gcc-c++ nasm git ninja-build
  ./Source/Script/Build.sh Release
  ```

* Portable Arch Linux
  ```bash
  sudo pacman -S --needed make cmake gcc libusb hidapi freetype2 libpng qt6 sdl3 libsamplerate nasm minizip pkgconf vulkan-headers git
  ./Source/Script/Build.sh Release
  ```

* Portable OpenSUSE Tumbleweed
  ```bash
  sudo zypper install SDL3-devel cmake freetype2-devel gcc gcc-c++ libusb-1_0-devel libhidapi-devel libhidapi-hidraw0 libpng16-devel libsamplerate-devel make nasm ninja pkgconf-pkg-config speex-devel vulkan-devel zlib-devel qt6-tools-devel qt6-opengl-devel qt6-widgets-devel qt6-svg-devel minizip-devel git
  ./Source/Script/Build.sh Release
  ```

When it's done building, executables can be found in `Bin/Release`

* Installation/Packaging
```bash
export src_dir="$(pwd)"
export build_dir="$(pwd)/build"
mkdir -p "$build_dir"
cmake -S "$src_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="Release" -DPORTABLE_INSTALL="OFF" -DCMAKE_INSTALL_PREFIX="/usr" -G "Ninja"
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix="/usr"
```

#### Windows
* Download & Install [MSYS2](https://www.msys2.org/) (UCRT64)
```bash
pacman -S --needed make mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-hidapi mingw-w64-ucrt-x86_64-freetype mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-qt6 mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-speexdsp mingw-w64-ucrt-x86_64-libsamplerate mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-minizip mingw-w64-ucrt-x86_64-vulkan-headers git
./Source/Script/Build.sh Release
```

When it's done building, executables can be found in `Bin/Release`


## License

Rosalie's Mupen GUI is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html).

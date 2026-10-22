# Quake RT for Mac (unofficial test build)

This fork runs **Quake: Ray Traced** natively on Apple Silicon Macs - no CrossOver or Wine. It uses an experimental ray tracing build of MoltenVK, with Apple MetalFX upscaling. The changes are proposed upstream in [sultim-t/vkquake-rt#61](https://github.com/sultim-t/vkquake-rt/pull/61) and [sultim-t/RayTracedGL1#47](https://github.com/sultim-t/RayTracedGL1/pull/47); the code is on the `macos` branch of this repo and of [loui1989/RayTracedGL1](https://github.com/loui1989/RayTracedGL1/tree/macos).

## What you need

- A Mac with **Apple Silicon** (M1 or newer). Tested on an M3 Max with macOS 27.2 - other Apple Silicon Macs on a recent macOS may work. Intel Macs are not supported.
- **Your own copy of Quake** (Steam or GOG): the folder that contains `id1/pak0.pak`. Steam's Quake is Windows-only, so copy the folder from a PC (usually `C:\Program Files (x86)\Steam\steamapps\common\Quake` - launch the game there once first), or use the one inside a CrossOver bottle (found automatically).
- **The Quake RT release** (free, by Sultim Tsyrendashiev): download the zip from [sultim-t/vkquake-rt releases](https://github.com/sultim-t/vkquake-rt/releases) and unzip it, e.g. into Downloads.

## Install

1. Download the latest `Quake-RT-macOS-*.dmg` from this repo's [Releases](https://github.com/loui1989/vkquake-rt/releases), open it and drag **Quake RT** onto **Applications**.
2. Open Quake RT. The app isn't notarized by Apple, so macOS blocks the first launch: open **System Settings > Privacy & Security**, scroll down, click **Open Anyway** next to Quake RT and confirm.
   (Or in Terminal: `xattr -dr com.apple.quarantine "/Applications/Quake RT.app"`)
3. On first launch the app asks for your Quake folder and the unzipped Quake RT folder, then copies what it needs into `~/Games/Quake RT`. Nothing outside that folder is changed.

## Playing

- Starts in fullscreen; **Option+Return** toggles a window. The FPS counter (bottom right) can be hidden in the console (`~` key) with `scr_showfps 0`.
- **Upscaling:** Options > Video > **Apple MetalFX** - Quality / Balanced / Performance (lower = more FPS).
- Add-ons from the 2021 re-release (e.g. *Dimension of the Past*): copy its folder (such as `dopa`) and `QuakeEX.kpf` from the re-release's `rerelease` folder into `~/Games/Quake RT`, then pick it under **Mods** in the main menu.
- **Uninstall:** delete Quake RT from Applications and the folder `~/Games/Quake RT`.

## Reporting problems

Please open an [issue on this repo](https://github.com/loui1989/vkquake-rt/issues) with your Mac model, macOS version, what happened, and the log file `~/Games/Quake RT/quakert.log`. Ray tracing on MoltenVK is experimental, so occasional glitches or crashes are expected.

## Credits

Quake RT and RayTracedGL1 by Sultim Tsyrendashiev, based on vkQuake (Axel Gneiting) and Quake (id Software). MoltenVK ray tracing from [KhronosGroup/MoltenVK#2771](https://github.com/KhronosGroup/MoltenVK/pull/2771). Quake is a trademark of id Software; no game data is included. Unofficial fan project, not affiliated with id Software, Bethesda, Apple or the Quake RT authors.

---

# Quake: Ray Traced

Quake: Ray Traced adds a path tracing renderer to id Software's [Quake](https://en.wikipedia.org/wiki/Quake_(video_game)) using the [RayTracedGL1](https://github.com/sultim-t/RayTracedGL1) library.

Quake: Ray Traced is based on the [vkQuake](https://github.com/Novum/vkQuake) — a port of QuakeSpasm to Vulkan API.

## Build

### Windows

Clone the vkQuake repo from `https://github.com/sultim-t/vkquake-rt.git`

Prerequisites:

* [Git for Windows](https://github.com/git-for-windows/git/releases)
* GPU with a ray tracing support

Steps:

* Install [Visual Studio Community](https://www.visualstudio.com/products/free-developer-offers-vs) with Visual C++ component
* Open the Visual Studio solution, `Windows\VisualStudio\vkquake.sln`
* Build the [RayTracedGL1](https://github.com/sultim-t/RayTracedGL1/tree/quake) library, provide its header files / `.lib` to the `vkQuake` solution
* Build the `vkQuake` solution
* Copy `RayTracedGL1.dll` next to `vkQuake.exe` before running the executable

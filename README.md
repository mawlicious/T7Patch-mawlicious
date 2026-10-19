# T7Patch-mawlicious

A Windows fork of [Scroptss's updated T7Patch 3.09](https://github.com/Scroptss/T7Patch),
originally by serious/shiversoftdev. Increases the joining player's lobby-info
wait from **10 to 30 seconds**. Successful replies continue immediately.

## Install and run

1. Download `T7Patch-mawlicious-windows-x64.zip` from this repository's Releases.
2. Extract the complete `T7Patch-mawlicious` folder to a writable location.
3. Close other T7Patch launchers. Disable any existing manual T7Patch installation
   before launching BO3 (do not stack patches).
4. Edit `t7patch-mawlicious.conf` in this folder for your player name, friends-only
   setting and network password. The default is friends-only, no password.
   Everyone playing together must use the same network password if one is set.
5. Start BO3 through Steam and wait at the main menu. Run `T7Patch-mawlicious.exe`
   and press Enter when prompted. Repeat this step each time you restart BO3.
6. Check the top-right in-game label: **T7Patch-mawlicious 3.09 - join info 30s**.
   `NOT ACTIVE` means the setting was not applied/verified. The launcher may then
   be closed. Settings remain in the extracted folder and are reloaded in game.

Use the current Windows Steam BO3 builds supported by Scroptss's patch
(February/September 2026). Older and unknown builds are unsupported.
Files and labels carry `-mawlicious`; do not mix this DLL with a normal launcher.

## Scope and validation

Changes only `join_infoProbeWindow`; it does not fix handshake failures or
Synchronizing game settings/stat-transfer stalls. A 30-second wait helps only
when a valid lobby reply arrives after the original 10-second limit.

Built and tested on Windows CI. Tests exercise the actual timeout implementation
with a mock engine, including missing/wrong dvars, rejected writes, successful
readback, labels and restoration. **Windows gameplay has not been tested here**;
treat the first release as experimental and confirm the in-game label.

See [UPSTREAM.md](UPSTREAM.md) for provenance, offset evidence and limitations.
To build: use Visual Studio 2022 C++ tools/Windows SDK, or the included Actions
workflow. No review-bot loop was run for this release.

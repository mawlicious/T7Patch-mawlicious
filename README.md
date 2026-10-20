# T7Patch-mawlicious

A Windows fork of [Scroptss's updated T7Patch 3.09](https://github.com/Scroptss/T7Patch),
originally by serious/shiversoftdev. Increases the joining player's lobby-info
wait from **10 to 30 seconds**. Successful replies continue immediately.

## Install and run

1. Download `T7Patch-mawlicious-windows-x64.zip` from this repository's Releases.
2. Extract the complete `T7Patch-mawlicious` folder to a writable location.
3. Close other T7Patch launchers. Disable any existing manual T7Patch installation
   before launching BO3 (do not stack patches).
4. Open **T7Patch-mawlicious.exe**. Choose your player name, friends-only option
   and network password in the window. Settings save automatically; there is
   also a Save settings button. Default: friends-only, no password. Everyone
   playing together must use the same network password if one is set.
5. Leave the window open and start BO3 through Steam as usual. The launcher
   detects BO3, waits for its interfaces/player data to initialize, and attaches
   automatically. It watches for game restarts too. There is no console or
   press-Enter step. The launcher also works if BO3 is already running.
6. Confirm the launcher says the 30-second timeout was verified and the top-right
   in-game label says **T7Patch-mawlicious 3.09 - join info 30s**. `NOT ACTIVE`
   means the timeout setting was not applied/verified. Settings changes reload
   in game. Closing the launcher leaves the existing patch active until BO3 exits.

The launcher recreates the familiar settings-and-auto-attach workflow using a
native Windows UI. It is not the original .NET launcher: its source is not
included in Scroptss's published patch source. The game patch is built from
Scroptss's source. No separate T7Patch download or Python/.NET runtime is needed.

Use the current Windows Steam BO3 builds supported by Scroptss's patch
(February/September 2026). Older and unknown builds are unsupported.
Files and labels carry `-mawlicious`; do not mix this DLL with a normal launcher.

## Scope and validation

Changes only `join_infoProbeWindow`; it does not fix handshake failures or
Synchronizing game settings/stat-transfer stalls. A 30-second wait helps only
when a valid lobby reply arrives after the original 10-second limit.

Built and tested on Windows CI. Tests exercise the actual timeout implementation
with a mock engine, including missing/wrong dvars, rejected writes, successful
readback, labels and restoration. Windows UI tests check automatic settings
saving, validation, password masking, option preservation and restart persistence. **Windows gameplay has not been tested here**;
treat the first release as experimental and confirm the in-game label.

See [UPSTREAM.md](UPSTREAM.md) for provenance, offset evidence and limitations.
To build: use Visual Studio 2022 C++ tools/Windows SDK, or the included Actions
workflow. Changes are reviewed through pull requests before publishing release updates.

## Startup crash fix (gui3)

Initializes protection function pointers, packet keys and callbacks before the
running game can enter the patch's hooks. Resolves the protection exception
handler's own `ZwContinue` pointer before installing it. This addresses the
null comparison call found in a gui2 crash dump and the uninitialized
continuation path implicated by its protection-marker crash logs.

Windows CI tests the compiled DLL's actual dependency preparation and marker
exception continuation, in addition to the existing launcher and timeout tests.
Live BO3 gameplay still needs confirmation on Windows. If a crash remains,
send `crashes-mawlicious.log` from the game folder and any new `.dmp` file.
The launcher status is displayed in its window, not saved as a separate log.

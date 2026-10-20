# Upstream and change scope

Based on Scroptss/T7Patch-src commit
16b9b50662c68149526039cdf2ebdad348b9ca70 (T7Patch 3.09), linked from
https://github.com/Scroptss/T7Patch . Original patch by serious/shiversoftdev.
The upstream source has no top-level license file. This fork preserves source
attribution; no new license is asserted over upstream material. Bundled MinHook
and Detours libraries are retained from upstream.

The custom change sets BO3's existing `join_infoProbeWindow` integer dvar from
10000 to 30000 through the engine's setter, after upstream initialization.
It validates the dvar hash (D23BBF56), integer type and existing value (10000 or
30000), then reads the value back. No flag changes or creation of missing dvars.
The in-game label reports 30s only while readback confirms the value. Unloading
restores the prior value unless someone else has changed it.

The dvar name and September Dvar_FindVar RVA 22601B0 are independently listed in
https://github.com/Ezz-lol/boiii-free/tree/main/data/lookup_tables/dvar_list.txt
and src/client/game/symbols/dvar/core.hpp. That map's Dvar_SetFromStringByName
RVA 226A9E0 agrees with Scroptss's February RVA 226B0A0 minus 6C0. The same
February/September mapping yields FindVar February RVA 2260870. Dvar layout:
hash +0, type +1C (integer=6), current +28.

The name's hash matches the Mac executable's registration that the Mac
UpdateInfoProbe timeout check reads. The supplied local Windows executable is
an older, unsupported build (timestamp 63F95E3C, image size 1FAB7000), not either
2026 build, so it cannot validate current runtime offsets. This release retains
Scroptss's supported-build check and does not support that old executable.

Other changes are the native branded settings/auto-attach launcher, separate config/crash filenames,
window/in-game labels, build workflow and tests. Existing security, campaign and
Lua behavior is inherited from upstream. The launcher loads the patch DLL and
calls the same initialization path as upstream's manual installer. It does not
modify game files or require Wine/Python/.NET. Source builds use Windows MSVC.

Scroptss publishes the DLL source but not its released .NET launcher's UI source.
This fork's launcher recreates the name/friends-only/password workflow with
native Win32 controls, saves the same configuration format beside the DLL, and
watches for BO3/restarts. Before attachment it validates the supported executable
fingerprint, initialized Steam interface/player-data/lobby/dvar pointers and a
visible game window. It never repeatedly initializes a failed/attached process.
The configuration file is replaced atomically, supports Unicode folder paths,
and preserves other upstream options such as autoinstallmods.

No handshake, reservation, stat-transfer, or loading timeout is changed.
No claim is made to fix the Synchronizing game settings stall.

## gui3 startup crash investigation

A supplied gui2 minidump records an execute access violation at RIP 0. Its top
stack return address is `T7Patch-mawlicious.dll + 0x16BEA`. Disassembly of the
published gui2 DLL shows the preceding instruction calls the pointer at DLL RVA
`0x77450`; the containing hook is `hkCL_GetConfigString`, and that pointer is
`Protection::I_stricmp`, initialized only later in `Protection::install`.
`ApplyHooks` had already enabled the hook on the game's running threads.

Separate text crash records contain the lobby-protection sentinel
`0xFFEEDDCC44332212`. The class's `Protection::ZwContinue` was initialized to
null and never assigned; it is separate from the initialized global in
`dllmain.cpp`. The protection exception handler used the null class pointer.

`Protection::prepare` now resolves continuation and initializes callback
function pointers, packet-key strings and callback tables before publishing
any VMT or MinHook detours or installing the exception handler. The two other
supplied dumps fault at executable RVA `0x225247F` and do not list the patch
DLL; this change does not claim to diagnose or fix those crashes.

CI tests the production DLL via its linker map without adding test exports or
executing game code. A fake player-data page supplies preparation's one game
read; the protection marker handler then restores its registers and reaches a
recording continuation. PDB and map artifacts are retained separately for
future crash diagnosis; crash dumps are not published in this repository.

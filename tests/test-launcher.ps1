$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Ui {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr Text(IntPtr window, uint message, IntPtr w, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr Read(IntPtr window, uint message, IntPtr w, StringBuilder text);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
}
'@
function Read-Control($window, $id) {
    $text = [Text.StringBuilder]::new(2048)
    [void][Ui]::Read([Ui]::GetDlgItem($window, $id), 0xD, [IntPtr]2048, $text)
    return $text.ToString()
}
function Wait-For($check, $description) {
    for ($i=0; $i -lt 100; $i++) {
        if (& $check) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out: $description"
}
function Launch($path) {
    $p = Start-Process $path -PassThru -WorkingDirectory $env:TEMP
    Wait-For { $p.Refresh(); if ($p.HasExited) { throw 'Launcher exited unexpectedly' }; $p.MainWindowHandle -ne [IntPtr]::Zero } 'launcher window'
    return $p
}
function Close-Launcher($p) {
    [void][Ui]::PostMessageW($p.MainWindowHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (!$p.WaitForExit(10000)) { $p.Kill(); throw 'Launcher failed to close cleanly' }
    if ($p.ExitCode -ne 0) { throw "Launcher exit code $($p.ExitCode)" }
}
$folder = Join-Path $env:RUNNER_TEMP 'T7Patch-mawlicious-ui-test'
New-Item -ItemType Directory -Force $folder | Out-Null
Copy-Item T7Patch-mawlicious.exe, x64/Release/T7Patch-mawlicious.dll $folder
$config = Join-Path $folder 't7patch-mawlicious.conf'
"playername=Unknown Soldier`nisfriendsonly=1`nnetworkpassword=`nautoinstallmods=1`n" | Set-Content $config -Encoding utf8NoBOM
$exe = Join-Path $folder 'T7Patch-mawlicious.exe'
$p = $null
try {
    $p = Launch $exe
    $window = $p.MainWindowHandle
    Wait-For { (Read-Control $window 107) -like 'Waiting for BO3*' } 'waiting for game status'
    [void][Ui]::Text([Ui]::GetDlgItem($window,101), 0xC, [IntPtr]::Zero, 'MawTest')
    [void][Ui]::Text([Ui]::GetDlgItem($window,103), 0xC, [IntPtr]::Zero, 'alpha=beta')
    [void][Ui]::Send([Ui]::GetDlgItem($window,102), 0xF5, [IntPtr]::Zero, [IntPtr]::Zero)
    Wait-For { (Get-Content $config -Raw) -match 'playername=MawTest' } 'automatic settings save'
    $saved = Get-Content $config -Raw
    foreach ($line in @('networkpassword=alpha=beta','isfriendsonly=0','autoinstallmods=1')) {
        if (!$saved.Contains($line)) { throw "Missing saved setting: $line" }
    }
    # Reject invalid input without replacing the last valid configuration.
    [void][Ui]::Text([Ui]::GetDlgItem($window,101), 0xC, [IntPtr]::Zero, '')
    Wait-For { (Read-Control $window 106) -like 'Choose a player name*' } 'invalid name validation'
    if ((Get-Content $config -Raw) -ne $saved) { throw 'Invalid input overwrote valid settings' }
    [void][Ui]::Text([Ui]::GetDlgItem($window,101), 0xC, [IntPtr]::Zero, 'MawTest')
    Wait-For { (Read-Control $window 106) -like 'Settings saved*' } 'valid settings saved'
    Close-Launcher $p
    $p = Launch $exe
    $window = $p.MainWindowHandle
    if ((Read-Control $window 101) -ne 'MawTest' -or (Read-Control $window 103) -ne 'alpha=beta') { throw 'Settings did not survive restart' }
    if ([Ui]::Send([Ui]::GetDlgItem($window,102), 0xF0, [IntPtr]::Zero, [IntPtr]::Zero) -ne [IntPtr]::Zero) { throw 'Friends-only setting did not survive restart' }
    if ([Ui]::Send([Ui]::GetDlgItem($window,103), 0xD2, [IntPtr]::Zero, [IntPtr]::Zero) -eq [IntPtr]::Zero) { throw 'Password was not masked' }
    Close-Launcher $p
    $p = $null
    Write-Output 'Window, auto-save, validation, password masking, config location, option preservation and restart persistence passed.'
} finally {
    if ($p -and !$p.HasExited) { $p.Kill(); $p.WaitForExit() }
}

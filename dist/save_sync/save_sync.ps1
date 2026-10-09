# Denpa Ningen RPG FREE! save sharing between Azahar (PC) and RetroArch (iPad) via iCloud Drive.
# Usage: save_sync.ps1 import|export
# Exit codes: 0 = done or nothing to do, 2 = conflict (nothing overwritten), 1 = error
param([Parameter(Mandatory = $true)][ValidateSet('import', 'export')][string]$Mode)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

# iCloud Drive folder name: "電波人間セーブ" (kept ASCII-only for Windows PowerShell 5.1)
$folderName = -join ([char[]](0x96FB, 0x6CE2, 0x4EBA, 0x9593, 0x30BB, 0x30FC, 0x30D6))
$cloudDir = Join-Path $env:USERPROFILE (Join-Path 'iCloudDrive' $folderName)
if ($env:SAVE_SYNC_CLOUD_DIR) { $cloudDir = $env:SAVE_SYNC_CLOUD_DIR } # for testing
$zipPath = Join-Path $cloudDir 'save.zip'

$userDir = Join-Path $PSScriptRoot 'user'
$stateFile = Join-Path $userDir 'save_sync_state.json'
$backupDir = Join-Path $userDir 'save_backup'
$id = Get-ChildItem (Join-Path $userDir 'sdmc\Nintendo 3DS') -Directory | Select-Object -First 1
$id2 = Get-ChildItem $id.FullName -Directory | Select-Object -First 1
$sdRoot = $id2.FullName
# ZIP folder name -> local folder
$targets = [ordered]@{
    'data'     = Join-Path $sdRoot 'title\00040000\00125d00\data'
    '0000125D' = Join-Path $sdRoot 'extdata\00000000\0000125D'
}

# Write-Host keeps log lines out of function return values (they still reach stdout)
function Write-Log($msg) { Write-Host ("[save_sync {0}] {1}" -f $Mode, $msg) }

function Get-State {
    if (Test-Path $stateFile) { return Get-Content $stateFile -Raw | ConvertFrom-Json }
    return [pscustomobject]@{ zipTimeUtc = $null; localHash = $null }
}

function Set-State($zipTimeUtc, $localHash) {
    [pscustomobject]@{ zipTimeUtc = $zipTimeUtc; localHash = $localHash } |
        ConvertTo-Json | Set-Content $stateFile -Encoding ascii
}

function Get-LocalHash {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $sb = New-Object System.Text.StringBuilder
    foreach ($name in $targets.Keys) {
        $dir = $targets[$name]
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Recurse -File | Sort-Object FullName | ForEach-Object {
            $rel = $name + $_.FullName.Substring($dir.Length)
            $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
            [void]$sb.Append("$rel|$h`n")
        }
    }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($sb.ToString())
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
}

function Get-ZipTime {
    if (Test-Path $zipPath) { return (Get-Item $zipPath).LastWriteTimeUtc.ToString('o') }
    return $null
}

# Entries use '/' separators (Windows PowerShell's CreateFromDirectory writes '\', which iOS mishandles)
function New-LocalZip($dest) {
    if (Test-Path $dest) { Remove-Item $dest -Force }
    $zip = [System.IO.Compression.ZipFile]::Open($dest, 'Create')
    try {
        foreach ($name in $targets.Keys) {
            $dir = $targets[$name]
            if (-not (Test-Path $dir)) { continue }
            [void]$zip.CreateEntry("$name/")
            Get-ChildItem $dir -Recurse | Sort-Object FullName | ForEach-Object {
                $rel = $name + $_.FullName.Substring($dir.Length).Replace('\', '/')
                if ($_.PSIsContainer) {
                    [void]$zip.CreateEntry("$rel/")
                } else {
                    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel)
                }
            }
        }
    } finally { $zip.Dispose() }
}

function Backup-Local {
    New-Item -ItemType Directory -Force $backupDir | Out-Null
    $dest = Join-Path $backupDir ((Get-Date -Format 'yyyyMMdd_HHmmss') + '.zip')
    New-LocalZip $dest
    Get-ChildItem $backupDir -Filter *.zip | Sort-Object Name -Descending | Select-Object -Skip 10 |
        Remove-Item -Force
    return $dest
}

function Save-Conflict {
    New-Item -ItemType Directory -Force $cloudDir | Out-Null
    $dest = Join-Path $cloudDir ('save_conflict_pc_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.zip')
    New-LocalZip $dest
    return $dest
}

function Invoke-Export($state, $localHash) {
    if ($state.localHash -eq $localHash) { Write-Log 'no local changes; nothing to send'; return 0 }
    $zipTime = Get-ZipTime
    if ($zipTime -and $state.zipTimeUtc -ne $zipTime) {
        $c = Save-Conflict
        Write-Log "CONFLICT: save.zip was updated by another device. Local save kept as $c"
        return 2
    }
    New-Item -ItemType Directory -Force $cloudDir | Out-Null
    $tmp = "$zipPath.tmp"
    New-LocalZip $tmp
    Move-Item $tmp $zipPath -Force
    Set-State (Get-ZipTime) $localHash
    Write-Log "sent $zipPath"
    return 0
}

function Invoke-Import($state, $localHash) {
    $zipTime = Get-ZipTime
    $localChanged = $state.localHash -and ($state.localHash -ne $localHash)
    if (-not $zipTime -or $state.zipTimeUtc -eq $zipTime) {
        if ($localChanged) {
            Write-Log 'unsent local changes found; sending them first'
            return Invoke-Export $state $localHash
        }
        Write-Log 'save.zip is not newer; nothing to import'
        return 0
    }
    $code = 0
    if ($localChanged) {
        $c = Save-Conflict
        Write-Log "CONFLICT: both devices changed the save. Local save kept as $c; importing save.zip"
        $code = 2
    }
    $b = Backup-Local
    Write-Log "backup $b"
    $tmp = Join-Path $env:TEMP ('save_sync_in_' + [guid]::NewGuid())
    try {
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $tmp)
        if (-not (Test-Path (Join-Path $tmp 'data'))) { throw "save.zip has no 'data' folder" }
        foreach ($name in $targets.Keys) {
            $src = Join-Path $tmp $name
            if (-not (Test-Path $src)) { continue }
            $dst = $targets[$name]
            if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
            New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
            Move-Item $src $dst
        }
    } finally { if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force } }
    Set-State $zipTime (Get-LocalHash)
    Write-Log "imported $zipPath"
    return $code
}

try {
    $state = Get-State
    $localHash = Get-LocalHash
    if ($Mode -eq 'import') { exit (Invoke-Import $state $localHash) }
    exit (Invoke-Export $state $localHash)
} catch {
    Write-Log ("ERROR: " + $_.Exception.Message)
    exit 1
}

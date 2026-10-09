# Denpa Ningen RPG FREE! save sharing between Azahar (PC) and RetroArch (iPad) via iCloud Drive.
# Only savedata.bin is exchanged (extdata does not change during play).
# Usage: save_sync.ps1 import|export
# Exit codes: 0 = done or nothing to do, 2 = conflict (nothing lost), 1 = error
param([Parameter(Mandatory = $true)][ValidateSet('import', 'export')][string]$Mode)

$ErrorActionPreference = 'Stop'

# iCloud Drive folder name: "電波人間セーブ" (kept ASCII-only for Windows PowerShell 5.1)
$folderName = -join ([char[]](0x96FB, 0x6CE2, 0x4EBA, 0x9593, 0x30BB, 0x30FC, 0x30D6))
$cloudDir = Join-Path $env:USERPROFILE (Join-Path 'iCloudDrive' $folderName)
if ($env:SAVE_SYNC_CLOUD_DIR) { $cloudDir = $env:SAVE_SYNC_CLOUD_DIR } # for testing
$cloudFile = Join-Path $cloudDir 'savedata.bin'

$userDir = Join-Path $PSScriptRoot 'user'
$stateFile = Join-Path $userDir 'save_sync_state.json'
$backupDir = Join-Path $userDir 'save_backup'
$id = Get-ChildItem (Join-Path $userDir 'sdmc\Nintendo 3DS') -Directory | Select-Object -First 1
$id2 = Get-ChildItem $id.FullName -Directory | Select-Object -First 1
$localFile = Join-Path $id2.FullName 'title\00040000\00125d00\data\00000001\savedata.bin'

# Write-Host keeps log lines out of function return values (they still reach stdout)
function Write-Log($msg) { Write-Host ("[save_sync {0}] {1}" -f $Mode, $msg) }

function Get-State {
    if (Test-Path $stateFile) { return Get-Content $stateFile -Raw | ConvertFrom-Json }
    return [pscustomobject]@{ cloudTimeUtc = $null; localHash = $null }
}

function Set-State($cloudTimeUtc, $localHash) {
    [pscustomobject]@{ cloudTimeUtc = $cloudTimeUtc; localHash = $localHash } |
        ConvertTo-Json | Set-Content $stateFile -Encoding ascii
}

function Get-LocalHash {
    if (-not (Test-Path $localFile)) { return $null }
    return (Get-FileHash $localFile -Algorithm SHA256).Hash
}

function Get-CloudTime {
    if (Test-Path $cloudFile) { return (Get-Item $cloudFile).LastWriteTimeUtc.ToString('o') }
    return $null
}

function Stamp { return (Get-Date -Format 'yyyyMMdd_HHmmss') }

function Backup-Local {
    New-Item -ItemType Directory -Force $backupDir | Out-Null
    $dest = Join-Path $backupDir ((Stamp) + '_savedata.bin')
    Copy-Item $localFile $dest
    Get-ChildItem $backupDir -Filter *_savedata.bin | Sort-Object Name -Descending |
        Select-Object -Skip 10 | Remove-Item -Force
    return $dest
}

function Save-Conflict {
    New-Item -ItemType Directory -Force $cloudDir | Out-Null
    $dest = Join-Path $cloudDir ('savedata_conflict_pc_' + (Stamp) + '.bin')
    Copy-Item $localFile $dest
    return $dest
}

# Copy through a temporary file so a half-written file is never left in place
function Copy-Safely($src, $dst) {
    $tmp = "$dst.tmp"
    Copy-Item $src $tmp -Force
    Move-Item $tmp $dst -Force
}

function Invoke-Export($state, $localHash) {
    if (-not $localHash) { Write-Log 'no local save; nothing to send'; return 0 }
    if ($state.localHash -eq $localHash) { Write-Log 'no local changes; nothing to send'; return 0 }
    $cloudTime = Get-CloudTime
    if ($cloudTime -and $state.cloudTimeUtc -ne $cloudTime) {
        $c = Save-Conflict
        Write-Log "CONFLICT: savedata.bin in iCloud was updated by another device. Local save kept as $c"
        return 2
    }
    New-Item -ItemType Directory -Force $cloudDir | Out-Null
    Copy-Safely $localFile $cloudFile
    Set-State (Get-CloudTime) $localHash
    Write-Log "sent $cloudFile"
    return 0
}

function Invoke-Import($state, $localHash) {
    $cloudTime = Get-CloudTime
    $localChanged = $state.localHash -and ($state.localHash -ne $localHash)
    if (-not $cloudTime -or $state.cloudTimeUtc -eq $cloudTime) {
        if ($localChanged -or (-not $cloudTime -and $localHash)) {
            Write-Log 'unsent local changes found; sending them first'
            return Invoke-Export $state $localHash
        }
        Write-Log 'savedata.bin in iCloud is not newer; nothing to import'
        return 0
    }
    $code = 0
    if ($localChanged) {
        $c = Save-Conflict
        Write-Log "CONFLICT: both devices changed the save. Local save kept as $c; importing iCloud save"
        $code = 2
    }
    if (Test-Path $localFile) { Write-Log ("backup " + (Backup-Local)) }
    New-Item -ItemType Directory -Force (Split-Path $localFile) | Out-Null
    Copy-Safely $cloudFile $localFile
    Set-State $cloudTime (Get-LocalHash)
    Write-Log "imported $cloudFile"
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

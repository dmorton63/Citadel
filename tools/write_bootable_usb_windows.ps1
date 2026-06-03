[CmdletBinding()]
param(
    [string]$IsoPath,
    [int]$DiskNumber = -1,
    [string]$DriveLetter,
    [switch]$Force,
    [switch]$SkipBuild,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Resolve-IsoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'IsoPath is required.'
    }

    if (Test-Path -LiteralPath $Path) {
        return (Resolve-Path -LiteralPath $Path).ProviderPath
    }

    if ($Path.StartsWith('\\wsl.localhost\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Path
    }

    throw "ISO not found: $Path"
}

function Get-DiskFromDriveLetter {
    param([string]$Letter)

    if ([string]::IsNullOrWhiteSpace($Letter)) {
        throw 'DriveLetter is required.'
    }

    $normalized = $Letter.Substring(0, 1).ToUpperInvariant()
    $partition = Get-Partition -DriveLetter $normalized -ErrorAction SilentlyContinue
    if (-not $partition) {
        throw "Drive letter ${normalized}: is not currently mounted. Replug the USB stick or restore the volume mount before retrying."
    }
    return [int]$partition.DiskNumber
}

function Get-VolumePathFromDriveLetter {
    param([string]$Letter)

    if ([string]::IsNullOrWhiteSpace($Letter)) {
        return $null
    }

    $normalized = $Letter.Substring(0, 1).ToUpperInvariant()
    $partition = Get-Partition -DriveLetter $normalized -ErrorAction SilentlyContinue
    if (-not $partition) {
        return $null
    }
    foreach ($accessPath in $partition.AccessPaths) {
        if ($accessPath -like '\\?\Volume{*}\') {
            return $accessPath
        }
    }

    $volume = Get-Volume -DriveLetter $normalized -ErrorAction SilentlyContinue
    if ($volume -and $volume.Path -like '\\?\Volume{*}\') {
        return [string]$volume.Path
    }

    return $null
}

function Ensure-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator rights are required to open a raw physical drive. Re-run this WSL session from an elevated Windows Terminal or PowerShell.'
    }
}

function Test-RawDiskWritable {
    param([int]$ResolvedDiskNumber)

    $physicalDrivePath = "\\.\PhysicalDrive$ResolvedDiskNumber"
    $probe = $null
    try {
        $probe = New-Object System.IO.FileStream($physicalDrivePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
    }
    catch {
        throw "Unable to open $physicalDrivePath for raw write access. $($_.Exception.Message)"
    }
    finally {
        if ($probe) {
            $probe.Dispose()
        }
    }
}

function Confirm-Target {
    param(
        [int]$ResolvedDiskNumber,
        [string]$ResolvedDriveLetter,
        [string]$ResolvedIsoPath,
        [UInt64]$DiskSize,
        [string]$FriendlyName
    )

    if ($Force) {
        return
    }

    Write-Host 'About to erase and overwrite the target USB disk:'
    Write-Host "  DiskNumber : $ResolvedDiskNumber"
    Write-Host "  DriveLetter: $ResolvedDriveLetter"
    Write-Host "  FriendlyName: $FriendlyName"
    Write-Host "  DiskSize    : $DiskSize bytes"
    Write-Host "  ISO         : $ResolvedIsoPath"
    $response = Read-Host 'Type the disk number to continue'
    if ($response -ne [string]$ResolvedDiskNumber) {
        throw 'Confirmation mismatch. Aborting.'
    }
}

function Dismount-DriveLetter {
    param([string]$Letter)

    if ([string]::IsNullOrWhiteSpace($Letter)) {
        return
    }

    $normalized = $Letter.Substring(0, 1).ToUpperInvariant()
    $partition = Get-Partition -DriveLetter $normalized -ErrorAction SilentlyContinue
    if (-not $partition) {
        Write-Host "Drive letter ${normalized}: is not mounted; skipping dismount."
        return
    }
    Write-Host "Dismounting volume ${normalized}:"
    & mountvol.exe "${normalized}:" /P
}

function Restore-DriveLetter {
    param(
        [string]$Letter,
        [string]$VolumePath
    )

    if ([string]::IsNullOrWhiteSpace($Letter) -or [string]::IsNullOrWhiteSpace($VolumePath)) {
        return
    }

    $normalized = $Letter.Substring(0, 1).ToUpperInvariant()
    Write-Host "Restoring drive letter ${normalized}:"
    & mountvol.exe "${normalized}:" $VolumePath
}

function Write-IsoToDisk {
    param(
        [string]$ResolvedIsoPath,
        [int]$ResolvedDiskNumber
    )

    $physicalDrivePath = "\\.\PhysicalDrive$ResolvedDiskNumber"
    $buffer = New-Object byte[] (4MB)
    $isoStream = $null
    $diskStream = $null

    try {
        $isoStream = [System.IO.File]::Open($ResolvedIsoPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
        $diskStream = New-Object System.IO.FileStream($physicalDrivePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)

        $totalBytes = $isoStream.Length
        [Int64]$writtenBytes = 0

        while (($readCount = $isoStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $diskStream.Write($buffer, 0, $readCount)
            $writtenBytes += $readCount
            $percent = 0
            if ($totalBytes -gt 0) {
                $percent = [int](($writtenBytes * 100) / $totalBytes)
            }
            Write-Progress -Activity 'Writing Citadel ISO to USB disk' -Status "$writtenBytes / $totalBytes bytes" -PercentComplete $percent
        }

        $diskStream.Flush()
        Write-Progress -Activity 'Writing Citadel ISO to USB disk' -Completed
    }
    finally {
        if ($diskStream) {
            $diskStream.Dispose()
        }
        if ($isoStream) {
            $isoStream.Dispose()
        }
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

if ([string]::IsNullOrWhiteSpace($IsoPath)) {
    $IsoPath = Join-Path $projectDir 'build\citadel-limine.iso'
}

if (-not $SkipBuild -and -not $DryRun) {
    throw 'Build orchestration stays in WSL. Build the ISO there first and rerun this script with -SkipBuild.'
}

$resolvedIsoPath = Resolve-IsoPath -Path $IsoPath

if ($DiskNumber -lt 0) {
    if ([string]::IsNullOrWhiteSpace($DriveLetter)) {
        throw 'Provide either -DiskNumber or -DriveLetter.'
    }
    $DiskNumber = Get-DiskFromDriveLetter -Letter $DriveLetter
}

$disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
if ($disk.BusType -ne 'USB' -and $disk.FriendlyName -notmatch 'SanDisk') {
    throw "Refusing to write to disk $DiskNumber because it is not clearly a USB target."
}

$resolvedDriveLetter = $DriveLetter
$resolvedVolumePath = $null
if ([string]::IsNullOrWhiteSpace($resolvedDriveLetter)) {
    $partition = Get-Partition -DiskNumber $DiskNumber -ErrorAction SilentlyContinue | Where-Object { $_.DriveLetter } | Select-Object -First 1
    if ($partition) {
        $resolvedDriveLetter = [string]$partition.DriveLetter
    }
}

if (-not [string]::IsNullOrWhiteSpace($resolvedDriveLetter)) {
    $resolvedVolumePath = Get-VolumePathFromDriveLetter -Letter $resolvedDriveLetter
}

Ensure-Administrator
Test-RawDiskWritable -ResolvedDiskNumber $DiskNumber

Confirm-Target -ResolvedDiskNumber $DiskNumber -ResolvedDriveLetter $resolvedDriveLetter -ResolvedIsoPath $resolvedIsoPath -DiskSize ([UInt64]$disk.Size) -FriendlyName ([string]$disk.FriendlyName)

if ($DryRun) {
    Write-Host 'Dry run only. No changes made.'
    exit 0
}

$dismounted = $false
try {
    Dismount-DriveLetter -Letter $resolvedDriveLetter
    $dismounted = -not [string]::IsNullOrWhiteSpace($resolvedDriveLetter)
    Write-IsoToDisk -ResolvedIsoPath $resolvedIsoPath -ResolvedDiskNumber $DiskNumber
    Write-Host "Bootable USB write complete for PhysicalDrive$DiskNumber"
}
catch {
    if ($dismounted) {
        try {
            Restore-DriveLetter -Letter $resolvedDriveLetter -VolumePath $resolvedVolumePath
        }
        catch {
            Write-Warning "Failed to restore drive letter ${resolvedDriveLetter}: $($_.Exception.Message)"
        }
    }
    throw
}
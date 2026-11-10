<#
.SYNOPSIS
    Prebuild step for DesktopSplitterSetup: stages everything the installer
    embeds into installer\setup\payload\.

.DESCRIPTION
    Stages two groups of files:

    DRIVER PACKAGE (from driver\x64\Release\DesktopSplitterVdd)
      DesktopSplitterVdd.inf / .dll / .cat
      Hard build failure when missing, or when the catalog is not Authenticode
      signed at all - an unsigned package cannot be installed and gives the
      installer no certificate to offer for trust. A catalog signed by an
      untrusted root is only a warning: that is the development case the
      installer exists to handle.

    PRODUCT FILES (installed into %ProgramFiles%\DesktopSplitter)
      DesktopSplitter.exe  - the native TWL control app
                             (app-native\build\x64\<Configuration>)
      dscomp.exe           - the compositor
      devcreate.exe        - device node helper
      edidoverride.exe     - EDID override + guarded-transaction helper. Must go
                             in the install root: the app probes for it next to
                             its own exe first.
      compositor-README.md - the compositor's README

    Writes payload.json describing exactly what was staged, so the build output
    is auditable.

.PARAMETER SkipAppPublish
    Reuse a previously staged DesktopSplitter.exe in payload\ instead of
    copying the freshly built native exe.
#>
[CmdletBinding()]
param(
    [string] $PackageDir,
    [string] $OutDir,
    [string] $Configuration = 'Release',
    [switch] $SkipAppPublish
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)

if (-not $PackageDir) { $PackageDir = Join-Path $repoRoot 'driver\x64\Release\DesktopSplitterVdd' }
if (-not $OutDir) { $OutDir = Join-Path $scriptRoot 'payload' }

function Fail([string] $Message) {
    Write-Host "stage-payload : error : $Message"
    exit 1
}

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# =========================== driver package ================================
if (-not (Test-Path $PackageDir)) {
    Fail ("driver package directory not found: $PackageDir`n" +
          "         Build the driver first:  msbuild driver\DesktopSplitterVdd.vcxproj /p:Configuration=Release /p:Platform=x64`n" +
          "         (requires the WDK; see docs\BUILDING.md)")
}

$wanted = @(
    @{ Source = 'DesktopSplitterVdd.inf'; Target = 'DesktopSplitterVdd.inf' },
    @{ Source = 'DesktopSplitterVdd.dll'; Target = 'DesktopSplitterVdd.dll' },
    @{ Source = 'desktopsplittervdd.cat'; Target = 'DesktopSplitterVdd.cat' }
)

$resolved = @()
foreach ($item in $wanted) {
    $path = Join-Path $PackageDir $item.Source
    if (-not (Test-Path $path)) {
        Fail ("$($item.Source) missing from $PackageDir`n" +
              "         The driver package is incomplete - rebuild driver\DesktopSplitterVdd.vcxproj (Release|x64).")
    }
    $resolved += [pscustomobject]@{ Path = (Resolve-Path $path).Path; Target = $item.Target }
}

$catPath = ($resolved | Where-Object { $_.Target -eq 'DesktopSplitterVdd.cat' }).Path
$sig = Get-AuthenticodeSignature $catPath
if ($sig.Status -eq 'NotSigned' -or -not $sig.SignerCertificate) {
    Fail ("$catPath is NOT Authenticode signed.`n" +
          "         Sign it first:  installer\sign-dev.ps1")
}

$subject = $sig.SignerCertificate.Subject
$thumb = $sig.SignerCertificate.Thumbprint
if ($sig.Status -eq 'Valid') {
    Write-Host "stage-payload : catalog signature is trusted on this machine ($subject)"
} else {
    Write-Host ("stage-payload : warning : catalog is signed but not trusted by this machine " +
                "($($sig.Status)). Expected for a development certificate; the installer prompts the user.")
}
Write-Host "stage-payload : signer = $subject"
Write-Host "stage-payload : sha1   = $thumb"

foreach ($item in $resolved) {
    Copy-Item -LiteralPath $item.Path -Destination (Join-Path $OutDir $item.Target) -Force
}

# =========================== product files =================================

# --- control app: native TWL exe ---
$appExe = Join-Path $OutDir 'DesktopSplitter.exe'
if ($SkipAppPublish) {
    if (-not (Test-Path $appExe)) { Fail "-SkipAppPublish given but $appExe does not exist." }
    Write-Host "stage-payload : reusing existing staged app (SkipAppPublish)"
} else {
    $built = Join-Path $repoRoot "app-native\build\x64\$Configuration\DesktopSplitter.exe"
    if (-not (Test-Path $built)) {
        Fail ("native control app not found: $built`n" +
              "         Build it first:  msbuild app-native\DesktopSplitter.vcxproj /p:Configuration=$Configuration /p:Platform=x64`n" +
              "         (or re-run with -SkipAppPublish to reuse a previously staged exe).")
    }
    Write-Host "stage-payload : native app -> $built"
    Copy-Item -LiteralPath $built -Destination $appExe -Force
}

# --- compositor + devcreate + docs ---
$products = @(
    @{ Source = Join-Path $repoRoot "compositor\build\x64\$Configuration\dscomp.exe";           Target = 'dscomp.exe';           Required = $true },
    @{ Source = Join-Path $repoRoot "installer\devcreate\bin\x64\$Configuration\devcreate.exe"; Target = 'devcreate.exe';        Required = $true },
    @{ Source = Join-Path $repoRoot "installer\edid-override\build\x64\$Configuration\edidoverride.exe"; Target = 'edidoverride.exe'; Required = $true },
    @{ Source = Join-Path $repoRoot 'compositor\README.md';                                     Target = 'compositor-README.md'; Required = $false }
)

foreach ($item in $products) {
    if (-not (Test-Path $item.Source)) {
        if ($item.Required) {
            Fail ("$($item.Target) not found at $($item.Source)`n" +
                  "         Build it first (build.ps1 builds the compositor and devcreate before the installer).")
        }
        Write-Host "stage-payload : warning : optional file missing, using placeholder: $($item.Source)"
        # A placeholder keeps the .rc compilable - every resource must exist.
        Set-Content -Path (Join-Path $OutDir $item.Target) -Value "(not available at build time)" -Encoding UTF8
        continue
    }
    Copy-Item -LiteralPath $item.Source -Destination (Join-Path $OutDir $item.Target) -Force
}

# =========================== manifest ======================================
$staged = @()
foreach ($name in @('DesktopSplitterVdd.inf', 'DesktopSplitterVdd.dll', 'DesktopSplitterVdd.cat',
                    'DesktopSplitter.exe', 'dscomp.exe', 'devcreate.exe', 'edidoverride.exe',
                    'compositor-README.md')) {
    $p = Join-Path $OutDir $name
    if (-not (Test-Path $p)) { Fail "expected staged file missing: $p" }
    $len = (Get-Item $p).Length
    $staged += [pscustomobject]@{ Name = $name; Bytes = $len; Sha256 = (Get-FileHash $p -Algorithm SHA256).Hash }
    Write-Host ("stage-payload : staged {0,-24} {1,12:N0} bytes" -f $name, $len)
}

$totalMb = [math]::Round((($staged | Measure-Object -Property Bytes -Sum).Sum / 1MB), 1)
Write-Host "stage-payload : total payload $totalMb MB"

[pscustomobject]@{
    StagedUtc     = (Get-Date).ToUniversalTime().ToString('o')
    PackageDir    = $PackageDir
    Configuration = $Configuration
    Signer        = $subject
    Sha1          = $thumb
    SigStatus     = $sig.Status.ToString()
    TotalMB       = $totalMb
    Files         = $staged
} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir 'payload.json') -Encoding UTF8

Write-Host "stage-payload : payload ready in $OutDir"
exit 0

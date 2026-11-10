[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $ArchivePath,
    [string] $PackagePath,
    [string] $OutputRoot,
    [ValidateRange(20, 300)]
    [int] $TimeoutSeconds = 90
)

# 配布物の ZIP を OBS Studio 33.0.1 の通常の導入先へ置き、OBS を 2 回起動して検査する。
# - fresh: 検査用のモジュールが集中線のソースを追加し、画素を検査して保存する
# - restart: 再起動後に、保存したソースと設定が残っていることを確かめ、画素を検査する
# 使い捨ての GitHub のランナーでだけ動かす。

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'Normal-install verification requires a fresh GitHub-hosted runner.'
}
$ConfigDir = Join-Path $env:APPDATA 'obs-studio'
$SystemPlugins = Join-Path $env:ProgramData 'obs-studio/plugins'
$ProductInstall = Join-Path $SystemPlugins 'simple-burst-lines'
$ProbeInstall = Join-Path $SystemPlugins 'burst-lines-install-probe'
foreach ($Existing in @($ConfigDir, $ProductInstall, $ProbeInstall)) {
    if (Test-Path -LiteralPath $Existing) { throw "Fresh runner required; existing state: $Existing" }
}
if (@(Get-Process -Name obs64 -ErrorAction SilentlyContinue).Count) { throw 'An OBS process already exists.' }
foreach ($Variable in @('OBS_PLUGINS_PATH', 'OBS_LEGACY_PLUGINS_PATH', 'OBS_LEGACY_PLUGINS_DATA_PATH')) {
    if ([Environment]::GetEnvironmentVariable($Variable)) { throw "External plugin search path is set: $Variable" }
}

$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$OutputRoot) { $OutputRoot = Join-Path $ProjectRoot '.deps/obs-load-windows' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw 'OutputRoot already exists. Use a new directory to preserve evidence.' }
$BuildSpec = Get-Content -LiteralPath (Join-Path $ProjectRoot 'buildspec.json') -Raw | ConvertFrom-Json
if (!$PackagePath) { $PackagePath = Join-Path $ProjectRoot "release/$($BuildSpec.name)-$($BuildSpec.version)-windows-x64.zip" }
$ProbeDll = Join-Path $ProjectRoot "build_x64/install-probe/$Configuration/burst-lines-install-probe.dll"
foreach ($File in @($PackagePath, $ProbeDll)) {
    if (!(Test-Path -LiteralPath $File -PathType Leaf)) { throw "Missing built module: $File" }
}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$PackageDir = Join-Path $OutputRoot 'package'
Expand-Archive -LiteralPath $PackagePath -DestinationPath $PackageDir
$ProductDll = Join-Path $PackageDir 'simple-burst-lines/simple-burst-lines.dll'
$ProductData = Join-Path $PackageDir 'simple-burst-lines/data'
if (!(Test-Path -LiteralPath $ProductDll -PathType Leaf) -or !(Test-Path -LiteralPath $ProductData -PathType Container)) { throw 'Unexpected product package layout.' }
if (Test-Path -LiteralPath (Join-Path $PackageDir 'simple-burst-lines/bin/64bit')) { throw 'Legacy binary layout found in the distribution.' }
$ResultDir = Join-Path $OutputRoot 'results'
New-Item -ItemType Directory -Path $ResultDir | Out-Null
if (!$ArchivePath) {
    $ArchivePath = Join-Path $OutputRoot 'OBS-Studio-33.0.1-Windows-x64.zip'
    Invoke-WebRequest -Uri 'https://github.com/obsproject/obs-studio/releases/download/33.0.1/OBS-Studio-33.0.1-Windows-x64.zip' -OutFile $ArchivePath
}
$ExpectedHash = '0d9436451c2d772b33428e3fdae9776f071895c335de17283176306fb72651d3'
$ActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualHash -ne $ExpectedHash) { throw 'Official OBS runtime archive checksum mismatch.' }
$Runtime = Join-Path $OutputRoot 'runtime'
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $Runtime
$BinaryDir = Join-Path $Runtime 'bin/64bit'
$ObsExe = Join-Path $BinaryDir 'obs64.exe'
if (!(Test-Path -LiteralPath $ObsExe -PathType Leaf)) { throw 'Unexpected OBS archive layout.' }
New-Item -ItemType Directory -Path $SystemPlugins -Force | Out-Null
# 使い捨てのランナーに、配布物の通常の ProgramData の配置で導入する
Copy-Item -LiteralPath (Join-Path $PackageDir 'simple-burst-lines') -Destination $ProductInstall -Recurse
New-Item -ItemType Directory -Path $ProbeInstall -Force | Out-Null
Copy-Item -LiteralPath $ProbeDll -Destination $ProbeInstall
$InstalledDll = Join-Path $ProductInstall 'simple-burst-lines.dll'
if ((Get-FileHash -LiteralPath $InstalledDll).Hash -ne (Get-FileHash -LiteralPath $ProductDll).Hash) {
    throw 'Installed DLL differs from the distribution.'
}
$ProfileDir = Join-Path $ConfigDir 'basic/profiles/Baseline'
$SceneDir = Join-Path $ConfigDir 'basic/scenes'
New-Item -ItemType Directory -Path $ProfileDir, $SceneDir -Force | Out-Null
$UserIni = "[General]`nLanguage=en-US`nFirstRun=true`n[Basic]`nProfile=Baseline`nProfileDir=Baseline`nSceneCollection=Baseline`nSceneCollectionFile=Baseline.json`nConfigOnNewProfile=false`n[BasicWindow]`nPreviewEnabled=false`n"
Set-Content -LiteralPath (Join-Path $ConfigDir 'user.ini') -Value $UserIni -Encoding utf8
Set-Content -LiteralPath (Join-Path $ConfigDir 'global.ini') -Value "[General]`nEnableAutoUpdates=false`n" -Encoding utf8
$ProfileIni = "[General]`nName=Baseline`n[Audio]`nSampleRate=48000`nChannelSetup=Stereo`nMonitoringDeviceId=default`n[Video]`nBaseCX=640`nBaseCY=360`nOutputCX=640`nOutputCY=360`nFPSType=0`nFPSCommon=30`n"
Set-Content -LiteralPath (Join-Path $ProfileDir 'basic.ini') -Value $ProfileIni -Encoding utf8
$InitialScene = @{
    name = 'Baseline'
    sources = @(@{ name = 'Scene'; id = 'scene'; settings = @{ items = @() } })
    scene_order = @(@{ name = 'Scene' })
    current_scene = 'Scene'
    current_program_scene = 'Scene'
} | ConvertTo-Json -Depth 8
Set-Content -LiteralPath (Join-Path $SceneDir 'Baseline.json') -Value $InitialScene -Encoding utf8

$PreviousResultDir = $env:BURST_LINES_INSTALL_RESULT_DIR
$PreviousPhase = $env:BURST_LINES_INSTALL_PHASE
$Process = $null
try {
    foreach ($Stage in @('fresh', 'restart')) {
        $Phase = if ($Stage -eq 'fresh') { 'setup' } else { 'restore' }
        $PhaseDir = Join-Path $ResultDir $Stage
        New-Item -ItemType Directory -Path $PhaseDir | Out-Null
        $env:BURST_LINES_INSTALL_RESULT_DIR = $PhaseDir
        $env:BURST_LINES_INSTALL_PHASE = $Phase
        $Process = Start-Process -FilePath $ObsExe -WorkingDirectory $BinaryDir -ArgumentList @('--disable-updater', '--multi') -WindowStyle Hidden -PassThru
        if (!$Process.WaitForExit($TimeoutSeconds * 1000)) { throw "OBS probe timed out: $Phase" }
        if ($Process.ExitCode -ne 0) { throw "OBS exited with code $($Process.ExitCode): $Phase" }
        $ResultPath = Join-Path $PhaseDir 'result.json'
        if (!(Test-Path -LiteralPath $ResultPath)) { throw "Probe result missing: $Phase" }
        $Result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
        if ($Result.passed -ne $true -or $Result.obs -ne '33.0.1' -or $Result.phase -ne $Phase) { throw "Probe failed: $Phase $($Result.reason)" }
        if (!$Result.product_binary_path -or [IO.Path]::GetFullPath($Result.product_binary_path, [IO.Path]::GetFullPath($BinaryDir)) -ne [IO.Path]::GetFullPath($InstalledDll)) { throw "Unexpected loaded product DLL: $($Result.product_binary_path)" }
        $Logs = @(Get-ChildItem -LiteralPath (Join-Path $ConfigDir 'logs') -Filter '*.txt' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
        if (!$Logs.Count) { throw 'OBS log missing.' }
        $LogText = ($Logs | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
        if ([regex]::Matches($LogText, '\[simple-burst-lines\] plugin loaded successfully').Count -ne 1) { throw 'Expected exactly one product module load.' }
        foreach ($Required in @('Portable mode: false', "BURST_LINES_INSTALL PASS phase=$Phase")) {
            if (!$LogText.Contains($Required)) { throw "Required log marker missing: $Required" }
        }
        foreach ($Resource in @('effect_path', 'locale_path')) {
            $Relative = if ($Resource -eq 'effect_path') { 'data/effects/burst-lines.effect' } else { 'data/locale/en-US.ini' }
            $ExpectedResource = [IO.Path]::GetFullPath((Join-Path $ProductInstall $Relative))
            $ObservedResource = $Result.evidence.$Resource
            if (!$ObservedResource -or [IO.Path]::GetFullPath($ObservedResource, $BinaryDir) -ne $ExpectedResource) { throw "Unexpected product resource: $Resource" }
        }
        if ($Result.evidence.pixel_result -ne 0 -or $Result.evidence.source_name -ne 'Simple Burst Lines') { throw 'Pixel or locale verification failed.' }
        $SavedScene = Get-Content -LiteralPath (Join-Path $SceneDir 'Baseline.json') -Raw | ConvertFrom-Json
        $SavedSource = @($SavedScene.sources | Where-Object { $_.name -eq 'Burst lines install check' })
        if ($SavedSource.Count -ne 1 -or $SavedSource[0].id -ne 'simple_burst_lines_source' -or
            $SavedSource[0].settings.line_count -ne 64 -or $SavedSource[0].settings.color -ne 4278190335) { throw 'Saved source settings mismatch.' }
        Copy-Item -LiteralPath $ConfigDir -Destination (Join-Path $PhaseDir 'saved-config') -Recurse
        Write-Output "OBS 33.0.1 normal installation verification passed: $Stage"
    }
} finally {
    if ($Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
    $env:BURST_LINES_INSTALL_RESULT_DIR = $PreviousResultDir
    $env:BURST_LINES_INSTALL_PHASE = $PreviousPhase
    $LogDir = Join-Path $ConfigDir 'logs'
    if (Test-Path -LiteralPath $LogDir) { Copy-Item -LiteralPath $LogDir -Destination (Join-Path $ResultDir 'obs-logs') -Recurse }
    @{
        runtime_version = '33.0.1'
        runtime_archive_sha256 = $ActualHash
        package_sha256 = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash
        product_sha256 = (Get-FileHash -LiteralPath $ProductDll -Algorithm SHA256).Hash
        product_installed_path = $InstalledDll
        config_directory = $ConfigDir
        probe_sha256 = (Get-FileHash -LiteralPath $ProbeDll -Algorithm SHA256).Hash
        process_exit_code = $(if ($Process -and $Process.HasExited) { $Process.ExitCode } else { $null })
        runner_image = $env:ImageVersion
        architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ResultDir 'environment.json') -Encoding utf8
}

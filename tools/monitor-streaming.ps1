param(
    [Parameter(Mandatory=$true)][string]$ArtifactsDirectory,
    [Parameter(Mandatory=$true)][string]$PlaybackUrl,
    [string]$FFmpeg = 'ffmpeg',
    [int]$TestProcessId = 0
)

# Run beside the explicit SRS endurance test. Reads its status and process
# resources; captures media without re-encoding. Never stops a process/server.
$ErrorActionPreference = 'Stop'
if (!$TestProcessId) {
    $candidates = @(Get-Process tst_streaming -ErrorAction SilentlyContinue)
    if ($candidates.Count -ne 1) { throw 'Specify the endurance test process ID.' }
    $TestProcessId = $candidates[0].Id
}
$artifactRoot = (Resolve-Path -LiteralPath $ArtifactsDirectory).Path
$testStarted = (Get-Process -Id $TestProcessId).StartTime
$startCaptured = $false
$endCaptured = $false

function Capture-Sample([string]$name) {
    $destination = Join-Path $artifactRoot $name
    if (Test-Path -LiteralPath $destination) { return }
    # Keep arbitrary server responses/URLs out of diagnostic output.
    & $FFmpeg -nostdin -n -v error -threads 1 -rw_timeout 5000000 -i $PlaybackUrl -t 20 -c copy $destination 2>$null
    if ($LASTEXITCODE) { Write-Warning 'HTTP-FLV sample capture failed; verify playback independently.' }
}

while ($true) {
    $testProcess = Get-Process -Id $TestProcessId -ErrorAction SilentlyContinue
    if (!$testProcess -or $testProcess.StartTime -ne $testStarted) { break }
    try { $progress = Get-Content (Join-Path $artifactRoot 'progress.json') -Raw | ConvertFrom-Json }
    catch { Start-Sleep -Seconds 1; continue }
    [pscustomobject]@{
        elapsedMs = $progress.elapsedMs
        privateBytes = $testProcess.PrivateMemorySize64
        workingSet = $testProcess.WorkingSet64
        handles = $testProcess.HandleCount
        cpuSeconds = $testProcess.CPU
    } | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $artifactRoot 'resources.jsonl') -Encoding utf8
    if (!$startCaptured -and $progress.elapsedMs -gt 1000) {
        $startCaptured = $true
        Capture-Sample 'http-flv-start.flv'
    }
    if (!$endCaptured -and $progress.elapsedMs -ge ($progress.targetSeconds-30)*1000) {
        $endCaptured = $true
        Capture-Sample 'http-flv-end.flv'
    }
    Start-Sleep -Seconds 10
}

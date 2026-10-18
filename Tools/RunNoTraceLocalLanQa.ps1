[CmdletBinding()]
param(
  [string]$Executable = 'build/lan-release/NoTraceLanQa.exe',
  [ValidateSet('mission', 'tour', 'table-tour', 'fps', 'mechanics', 'training', 'pipe', 'showcase', 'betrayal', 'fps-enemy')][string]$Scenario = 'mechanics',
  [string]$OutputRoot,
  [ValidateRange(1, 65535)][int]$Port = 55141,
  [ValidateRange(640, 7680)][int]$Width = 960,
  [ValidateRange(360, 4320)][int]$Height = 540,
  [ValidateRange(0, 1000)][int]$FrameLimit = 0,
  [switch]$Profile,
  [string]$RecordFfmpeg,
  [ValidateRange(30, 3600)][int]$Timeout = 900
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
if (-not [IO.Path]::IsPathRooted($Executable)) { $Executable = Join-Path $workspace $Executable }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($RecordFfmpeg) {
  $RecordFfmpeg = (Resolve-Path -LiteralPath $RecordFfmpeg).Path
  if ($FrameLimit -ne 30) { throw 'La grabación requiere -FrameLimit 30.' }
}
$runId = [Guid]::NewGuid().ToString('N')
if (-not $OutputRoot) { $OutputRoot = Join-Path $workspace ('build/local-lan-' + $Scenario + '-' + $runId) }
if (-not [IO.Path]::IsPathRooted($OutputRoot)) { $OutputRoot = Join-Path $workspace $OutputRoot }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $OutputRoot.StartsWith($workspace + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'La salida debe permanecer dentro del proyecto.' }
if (Test-Path -LiteralPath $OutputRoot) { throw 'La prueba requiere una salida nueva.' }
[IO.Directory]::CreateDirectory($OutputRoot) | Out-Null
$coord = Join-Path $OutputRoot 'coord'
[IO.Directory]::CreateDirectory($coord) | Out-Null
$save = Join-Path $workspace 'Content/Save.json'
$saveHash = (Get-FileHash -LiteralPath $save -Algorithm SHA256).Hash
$exeHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
$processes = [Collections.Generic.List[object]]::new()
$summary = [ordered]@{
  status = 'RUNNING'; runId = $runId; scenario = $Scenario; host = '127.0.0.1'; port = $Port
  startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null; failure = $null
  executable = $Executable; executableSha256 = $exeHash; saveSha256Before = $saveHash; saveSha256After = $null
  frameLimit = $FrameLimit; nativeSize = @($Width, $Height); instrumentation = [bool]$Profile; roles = @()
  scope = 'Dos procesos y ventanas visibles en el mismo equipo mediante TCP local; entradas automáticas del arnés, sin evidencia de dos equipos físicos.'
}

function Save-Summary {
  [IO.File]::WriteAllText((Join-Path $OutputRoot 'summary.json'), ($summary | ConvertTo-Json -Depth 32), [Text.UTF8Encoding]::new($false))
}

try {
  Save-Summary
  foreach ($role in @('host', 'client')) {
    $roleOutput = Join-Path $OutputRoot $role
    $arguments = @('--role', $role, '--scenario', $Scenario, '--host', '127.0.0.1', '--port', "$Port", '--run-id', $runId,
      '--coord', ('"' + $coord + '"'), '--output', ('"' + $roleOutput + '"'), '--timeout', "$Timeout", '--frame-limit', "$FrameLimit",
      '--width', "$Width", '--height', "$Height", '--warmup', '60', '--window-x', $(if ($role -eq 'host') { '0' } else { "$Width" }), '--window-y', '0', '--profile', $(if ($Profile) { 'true' } else { 'false' }))
    if ($RecordFfmpeg) { $arguments += @('--record-ffmpeg', ('"' + $RecordFfmpeg + '"')) }
    # Las ventanas visibles permiten comprobar ambos agentes durante el recorrido automático.
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $workspace -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $OutputRoot ($role + '.stdout.log')) -RedirectStandardError (Join-Path $OutputRoot ($role + '.stderr.log'))
    $processes.Add([pscustomobject]@{ role = $role; process = $process; output = $roleOutput })
    Write-Host "$role PID=$($process.Id) $Scenario -> $roleOutput"
  }
  $deadline = [DateTime]::UtcNow.AddSeconds($Timeout + 90)
  while (@($processes | Where-Object { -not $_.process.HasExited }).Count -gt 0) {
    foreach ($entry in $processes) {
      if ($entry.process.HasExited -and $entry.process.ExitCode -ne 0) { throw "Terminó $($entry.role) con código $($entry.process.ExitCode); consulta su reporte y logs." }
    }
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Los procesos no terminaron dentro del plazo del arnés.' }
    Start-Sleep -Milliseconds 1000
  }
  foreach ($entry in $processes) {
    $entry.process.WaitForExit()
    $reportFile = Join-Path $entry.output 'qa-report.json'
    $performanceFile = Join-Path $entry.output 'qa-performance.json'
    $report = if (Test-Path -LiteralPath $reportFile) { Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json } else { $null }
    $performance = if (Test-Path -LiteralPath $performanceFile) { Get-Content -LiteralPath $performanceFile -Raw | ConvertFrom-Json } else { $null }
    $summary.roles += [ordered]@{ role = $entry.role; pid = $entry.process.Id; exitCode = $entry.process.ExitCode; report = $report; performance = $performance }
  }
  if (@($summary.roles | Where-Object { $_.exitCode -ne 0 -or $null -eq $_.report -or $_.report.status -ne 'PASS' }).Count -gt 0) { throw 'La sesión LAN no completó todos sus casos; consulta los reportes por rol.' }
  if ((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash -ne $exeHash) { throw 'El ejecutable cambió durante la prueba.' }
  $summary.status = 'PASS'
} catch {
  $summary.status = 'FAIL'; $summary.failure = $_.Exception.Message
  $cancel = @{ runId = $runId; reason = $summary.failure } | ConvertTo-Json
  [IO.File]::WriteAllText((Join-Path $coord 'cancel.json'), $cancel, [Text.UTF8Encoding]::new($false))
  throw
} finally {
  $summary.saveSha256After = (Get-FileHash -LiteralPath $save -Algorithm SHA256).Hash
  if ($summary.saveSha256After -ne $saveHash) { $summary.status = 'FAIL'; $summary.failure = 'Cambió el guardado original durante QA.' }
  $summary.finishedUtc = [DateTime]::UtcNow.ToString('o'); Save-Summary
  Write-Host "Reporte LAN: $OutputRoot"
}
if ($summary.status -ne 'PASS') { throw $summary.failure }

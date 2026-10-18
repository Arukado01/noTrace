[CmdletBinding()]
param(
  [string]$Workspace = (Split-Path -Parent $PSScriptRoot),
  [string]$OutputRoot = 'build/qa-urgent-performance-20261018/alternated-before-after-07',
  [string]$BeforeContent = 'build/qa-urgent-performance-20261018/baseline-debug-tactical-moving-01/runtime/Content',
  [string]$AfterContent = 'Content',
  [string]$BeforeDebug = 'build/qa-urgent-performance-20261018/NoTracePerformanceScenesBaselineDebug.exe',
  [string]$BeforeRelease = 'build/qa-urgent-performance-20261018/NoTracePerformanceScenesBaselineRelease.exe',
  [string]$AfterDebug = 'build/qa-urgent-performance-20261018/NoTracePerformanceCompositionDebug.exe',
  [string]$AfterRelease = 'build/qa-urgent-performance-20261018/NoTracePerformanceCompositionRelease.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Workspace = (Resolve-Path -LiteralPath $Workspace).Path

function Absolute-Path([string]$Path) {
  if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
  return [IO.Path]::GetFullPath((Join-Path $Workspace $Path))
}

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function File-Sha([string]$Path) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "Falta archivo: $Path"
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Text-Sha([string]$Value) {
  $algorithm = [Security.Cryptography.SHA256]::Create()
  try {
    return ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant()
  } finally { $algorithm.Dispose() }
}

function Content-Snapshot([string]$Directory) {
  $records = [Collections.Generic.List[object]]::new()
  foreach ($file in (Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($Directory.Length + 1).Replace('\', '/')
    $first = $relative.Split('/')[0]
    # Estas exclusiones reproducen Sandbox del arnés; Save nunca se copia a la medición.
    if ($relative -eq 'Save.json' -or $first -in @('Sources', 'Screenshots')) { continue }
    $records.Add([ordered]@{ file = $relative; bytes = $file.Length; sha256 = (File-Sha $file.FullName) })
  }
  Require ($records.Count -gt 0) "Content vacío: $Directory"
  $payload = ($records | ForEach-Object { $_.file + "`0" + $_.sha256 + "`n" }) -join ''
  return [ordered]@{ directory = $Directory; sha256 = (Text-Sha $payload); files = @($records.ToArray()) }
}

function Write-Report($Report, [string]$Path) {
  [IO.File]::WriteAllText($Path, ($Report | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
}

function Same-Value($Left, $Right) {
  return (($Left | ConvertTo-Json -Depth 10 -Compress) -ceq ($Right | ConvertTo-Json -Depth 10 -Compress))
}

function Check-Parameters($Result, $Expected, [string]$Label) {
  foreach ($key in $Expected.Keys) {
    Require ($null -ne $Result.PSObject.Properties[$key]) "Falta parámetro $key en $Label"
    Require (Same-Value $Result.$key $Expected[$key]) "Parámetro distinto $key en $Label"
  }
}

$OutputRoot = Absolute-Path $OutputRoot
Require ($OutputRoot.StartsWith($Workspace.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) 'La salida debe estar dentro del workspace'
Require (-not (Test-Path -LiteralPath $OutputRoot)) "La salida ya existe: $OutputRoot"
$BeforeContent = (Resolve-Path -LiteralPath (Absolute-Path $BeforeContent)).Path.TrimEnd('\')
$AfterContent = (Resolve-Path -LiteralPath (Absolute-Path $AfterContent)).Path.TrimEnd('\')
$executables = [ordered]@{
  BeforeDebug = (Absolute-Path $BeforeDebug); BeforeRelease = (Absolute-Path $BeforeRelease)
  AfterDebug = (Absolute-Path $AfterDebug); AfterRelease = (Absolute-Path $AfterRelease)
}
$exeHashes = [ordered]@{}
foreach ($key in $executables.Keys) { $exeHashes[$key] = File-Sha $executables[$key] }
$savePath = Join-Path $Workspace 'Content/Save.json'
$saveHash = File-Sha $savePath
$beforeConfig = File-Sha (Join-Path $BeforeContent 'Config.json')
$afterConfig = File-Sha (Join-Path $AfterContent 'Config.json')
Require ($beforeConfig -eq $afterConfig) 'Config BEFORE y AFTER no son idénticos'
$beforeSnapshot = Content-Snapshot $BeforeContent
$afterSnapshot = Content-Snapshot $AfterContent
Require ($beforeSnapshot.sha256 -eq $afterSnapshot.sha256) 'Content BEFORE y AFTER no son idénticos'

$null = New-Item -ItemType Directory -Path $OutputRoot
$reportPath = Join-Path $OutputRoot 'comparison.json'
$rows = [Collections.Generic.List[object]]::new()
$pairs = [Collections.Generic.List[object]]::new()
$parameterFields = @('configuration', 'frames', 'warmup', 'moving', 'firstPerson', 'stress', 'instrumentation', 'fixedSimulationDelta', 'gpuFence', 'seed', 'scenario', 'initialPosition', 'nativeSize', 'logicalSize', 'guards', 'objects', 'obstacles', 'surfaces', 'glVendor', 'glRenderer', 'glVersion')
$report = [ordered]@{
  status = 'RUNNING'; startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null
  scope = '16 casos consecutivos en 8 parejas BEFORE/AFTER; CPU inclusiva y presentación real, sin atribuir variaciones previas del equipo al código'
  workspace = $Workspace; configHash = $beforeConfig; saveHashBefore = $saveHash; saveHashAfter = $null
  executables = $executables; executableSha256 = $exeHashes
  content = [ordered]@{ before = $beforeSnapshot; after = $afterSnapshot }
  parameterFields = $parameterFields; rows = @(); pairs = @(); failure = $null
}
Write-Report $report $reportPath

try {
  foreach ($configuration in @('Debug', 'Release')) {
    $frames = if ($configuration -eq 'Debug') { 120 } else { 180 }
    foreach ($view in @('tactical', 'fps')) {
      foreach ($stress in @($false, $true)) {
        $load = if ($stress) { 'stress' } else { 'normal' }
        $pairId = $configuration.ToLowerInvariant() + '-' + $view + '-' + $load
        $results = @{}
        foreach ($version in @('Before', 'After')) {
          $caseId = ('{0:D2}-{1}-{2}' -f ($rows.Count + 1), $pairId, $version.ToLowerInvariant())
          $caseDirectory = Join-Path $OutputRoot $caseId
          Require (-not (Test-Path -LiteralPath $caseDirectory)) "La salida de caso ya existe: $caseDirectory"
          $key = $version + $configuration
          $exe = $executables[$key]
          $content = if ($version -eq 'Before') { $BeforeContent } else { $AfterContent }
          Require ((File-Sha $savePath) -eq $saveHash) 'Cambió Save original antes del caso'
          Require ((File-Sha $exe) -eq $exeHashes[$key]) "Cambió el ejecutable: $key"
          Require ((File-Sha (Join-Path $content 'Config.json')) -eq $beforeConfig) 'Cambió Config antes del caso'
          $arguments = @('--output', ('"' + $caseDirectory + '"'), '--frames', "$frames", '--warmup', '24', '--width', '1280', '--height', '720', '--scenario', 'spawn', '--moving')
          if ($view -eq 'fps') { $arguments += '--fps' }
          if ($stress) { $arguments += '--stress' }
          $started = [DateTime]::UtcNow.ToString('o')
          $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $content) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $OutputRoot ($caseId + '.stdout.log')) -RedirectStandardError (Join-Path $OutputRoot ($caseId + '.stderr.log')) -Wait -PassThru
          Require ((File-Sha $savePath) -eq $saveHash) 'Cambió Save original al ejecutar el caso'
          Require ($process.ExitCode -eq 0) "Proceso fallido $caseId, exit $($process.ExitCode)"
          $resultPath = Join-Path $caseDirectory 'result.json'
          Require (Test-Path -LiteralPath $resultPath -PathType Leaf) "Falta result.json: $caseId"
          $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
          Require ($result.status -ceq 'PASS') "Resultado distinto de PASS: $caseId"
          Require ((File-Sha (Join-Path $caseDirectory 'runtime/Content/Config.json')) -eq $beforeConfig) "Config aislado distinto: $caseId"
          $expected = [ordered]@{
            configuration = $configuration; frames = $frames; warmup = 24; moving = $true
            firstPerson = ($view -eq 'fps'); stress = $stress; instrumentation = $false
            fixedSimulationDelta = $true; gpuFence = $false; seed = 119003; scenario = 'spawn'
            initialPosition = @([double]50, [double]650, [double]0); nativeSize = @(1280, 720)
            logicalSize = @([double]1600, [double]900)
            guards = $(if ($stress) { 8 } else { 4 }); objects = $(if ($stress) { 88 } else { 44 })
            obstacles = 27; surfaces = 21
          }
          Check-Parameters $result $expected $caseId
          Require ($result.frame.meanMs -gt 0 -and $result.frame.averageFps -gt 0) "Métrica temporal inválida: $caseId"
          $results[$version] = $result
          $rows.Add([pscustomobject][ordered]@{
            order = $rows.Count + 1; case = $caseId; configuration = $configuration; view = $view; stress = $stress
            version = $version; startedUtc = $started; finishedUtc = [DateTime]::UtcNow.ToString('o')
            executableSha256 = $exeHashes[$key]; contentSha256 = $beforeSnapshot.sha256; configHash = $beforeConfig; saveHash = $saveHash
            frames = $frames; warmup = 24; moving = $true; status = $result.status
            fps = $result.frame.averageFps; meanMs = $result.frame.meanMs; p95Ms = $result.frame.p95Ms; maxMs = $result.frame.maxMs
            updateMs = $result.update.meanMs; renderMs = $result.renderSubmission.meanMs; presentMs = $result.present.meanMs
            result = $resultPath
          })
          $report.rows = @($rows.ToArray())
          Write-Report $report $reportPath
        }
        $expectedPair = [ordered]@{}
        foreach ($field in $parameterFields) {
          Require ($null -ne $results.Before.PSObject.Properties[$field]) "Falta parámetro de pareja $field en BEFORE"
          $expectedPair[$field] = $results.Before.$field
        }
        Check-Parameters $results.After $expectedPair $pairId
        $pairs.Add([ordered]@{
          pair = $pairId; parameterMatch = $true; fields = $parameterFields.Count
          beforeMeanMs = $results.Before.frame.meanMs; afterMeanMs = $results.After.frame.meanMs
          beforeFps = $results.Before.frame.averageFps; afterFps = $results.After.frame.averageFps
          meanRatioBeforeOverAfter = $results.Before.frame.meanMs / $results.After.frame.meanMs
        })
        $report.pairs = @($pairs.ToArray())
        Write-Report $report $reportPath
      }
    }
  }
  Require ($rows.Count -eq 16 -and $pairs.Count -eq 8) 'El lote no contiene los 16 casos y 8 parejas previstos'
  Require ((Content-Snapshot $BeforeContent).sha256 -eq $beforeSnapshot.sha256) 'Content BEFORE cambió durante el lote'
  Require ((Content-Snapshot $AfterContent).sha256 -eq $afterSnapshot.sha256) 'Content AFTER cambió durante el lote'
  foreach ($key in $executables.Keys) { Require ((File-Sha $executables[$key]) -eq $exeHashes[$key]) "Cambió el ejecutable durante el lote: $key" }
  $report.status = 'PASS'
} catch {
  $report.status = 'FAIL'
  $report.failure = $_.Exception.Message
  throw
} finally {
  $report.finishedUtc = [DateTime]::UtcNow.ToString('o')
  try { $report.saveHashAfter = File-Sha $savePath } catch { $report.saveHashAfter = '<missing-or-unreadable>' }
  if ($report.saveHashAfter -ne $saveHash) { $report.status = 'FAIL'; $report.failure = 'Cambió Save original durante el lote' }
  Write-Report $report $reportPath
  $rows.ToArray() | Export-Csv -LiteralPath (Join-Path $OutputRoot 'comparison.csv') -NoTypeInformation -Encoding UTF8
  $rows.ToArray() | Select-Object order, configuration, view, stress, version, fps, meanMs, p95Ms, maxMs | Format-Table -AutoSize | Out-String | Set-Content -LiteralPath (Join-Path $OutputRoot 'comparison-table.txt') -Encoding UTF8
  Require ($report.status -eq 'PASS') "Lote inválido: $($report.failure)"
}

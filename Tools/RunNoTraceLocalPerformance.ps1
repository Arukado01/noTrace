[CmdletBinding()]
param(
  [string]$Workspace = (Split-Path -Parent $PSScriptRoot),
  [string]$Executable = 'build-release/NoTracePerformanceTests.exe',
  [string]$OutputRoot,
  [ValidateRange(1, 3600)][int]$Frames = 600,
  [ValidateRange(0, 3600)][int]$Warmup = 120,
  [ValidateRange(640, 16384)][int]$Width = 1280,
  [ValidateRange(360, 16384)][int]$Height = 720,
  [ValidateSet('spawn', 'warehouse', 'multilevel', 'camera10', 'top23')]
  [string[]]$Scenarios = @('spawn', 'warehouse', 'multilevel', 'camera10', 'top23'),
  [ValidateSet('tactical', 'fps')][string[]]$Views = @('tactical', 'fps'),
  [switch]$IncludeProfile,
  [switch]$ShowWindow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Workspace = (Resolve-Path -LiteralPath $Workspace).Path.TrimEnd('\')

function Require([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw $Message }
}

function Absolute-Path([string]$Path) {
  if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
  return [IO.Path]::GetFullPath((Join-Path $Workspace $Path))
}

function File-Sha([string]$Path) {
  Require (Test-Path -LiteralPath $Path -PathType Leaf) "Falta archivo: $Path"
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Optional-Sha([string]$Path) {
  if (Test-Path -LiteralPath $Path -PathType Leaf) { return File-Sha $Path }
  return $null
}

function Content-Manifest([string]$Directory) {
  $records = [Collections.Generic.List[object]]::new()
  foreach ($file in (Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($Directory.Length + 1).Replace('\', '/')
    $first = $relative.Split('/')[0]
    # Se excluyen los mismos archivos que Sandbox del ejecutable de medición.
    if ($relative -eq 'Save.json' -or $first -in @('Sources', 'Screenshots')) { continue }
    $records.Add([ordered]@{ file = $relative; bytes = $file.Length; sha256 = (File-Sha $file.FullName) })
  }
  Require ($records.Count -gt 0) "Content vacío: $Directory"
  $payload = ($records | ForEach-Object { $_.file + "`0" + $_.sha256 + "`n" }) -join ''
  $algorithm = [Security.Cryptography.SHA256]::Create()
  try {
    $hash = ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($payload)))).Replace('-', '').ToLowerInvariant()
  } finally { $algorithm.Dispose() }
  return [ordered]@{ directory = $Directory; sha256 = $hash; files = @($records.ToArray()) }
}

function Save-Json($Value, [string]$Path) {
  [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
}

function Same-Value($Left, $Right) {
  return (($Left | ConvertTo-Json -Depth 12 -Compress) -ceq ($Right | ConvertTo-Json -Depth 12 -Compress))
}

function Check-Parameters($Result, $Expected, [string]$Label) {
  foreach ($key in $Expected.Keys) {
    Require ($null -ne $Result.PSObject.Properties[$key]) "Falta parámetro $key en $Label"
    Require (Same-Value $Result.$key $Expected[$key]) "Parámetro distinto $key en $Label"
  }
}

function Positive-Metric($Value, [string]$Label) {
  $number = [double]$Value
  Require (-not [double]::IsNaN($number) -and -not [double]::IsInfinity($number) -and $number -gt 0) "Métrica inválida: $Label"
}

Require ($Scenarios.Count -gt 0 -and $Views.Count -gt 0) 'La matriz no puede estar vacía'
$Scenarios = @($Scenarios | ForEach-Object { $_.ToLowerInvariant() })
$Views = @($Views | ForEach-Object { $_.ToLowerInvariant() })
Require (@($Scenarios | Select-Object -Unique).Count -eq $Scenarios.Count) 'Hay escenarios repetidos'
Require (@($Views | Select-Object -Unique).Count -eq $Views.Count) 'Hay perspectivas repetidas'
$Executable = (Resolve-Path -LiteralPath (Absolute-Path $Executable)).Path
$contentDirectory = (Resolve-Path -LiteralPath (Join-Path $Workspace 'Content')).Path.TrimEnd('\')
$configPath = Join-Path $contentDirectory 'Config.json'
$savePath = Join-Path $contentDirectory 'Save.json'
$executableHash = File-Sha $Executable
$configHash = File-Sha $configPath
$saveHash = Optional-Sha $savePath
$contentManifest = Content-Manifest $contentDirectory
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $suffix = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
  $OutputRoot = 'build/perf-local/' + $suffix
}
$OutputRoot = Absolute-Path $OutputRoot
Require ($OutputRoot.StartsWith($Workspace + '\', [StringComparison]::OrdinalIgnoreCase)) 'La salida debe estar dentro del workspace'
Require (-not (Test-Path -LiteralPath $OutputRoot)) "La salida ya existe: $OutputRoot"
$null = New-Item -ItemType Directory -Path $OutputRoot -Force
Save-Json $contentManifest (Join-Path $OutputRoot 'content-manifest.json')

$rows = [Collections.Generic.List[object]]::new()
$stageRows = [Collections.Generic.List[object]]::new()
$passes = @('timing')
if ($IncludeProfile) { $passes += 'profile' }
$pairFields = @('configuration', 'frames', 'warmup', 'seed', 'scenario', 'initialPosition', 'nativeSize', 'logicalSize', 'guards', 'objects', 'obstacles', 'surfaces', 'glVendor', 'glRenderer', 'glVersion', 'firstPerson', 'moving', 'stress', 'fixedSimulationDelta', 'gpuFence')
$report = [ordered]@{
  status = 'RUNNING'; startedUtc = [DateTime]::UtcNow.ToString('o'); finishedUtc = $null; activeCase = $null; failure = $null
  workspace = $Workspace; executable = $Executable; executableSha256 = $executableHash; contentSha256 = $contentManifest.sha256
  configSha256 = $configHash; saveSha256Before = $saveHash; saveSha256After = $null
  frames = $Frames; warmup = $Warmup; nativeSize = @($Width, $Height); scenarios = $Scenarios; views = $Views; passes = $passes
  requestedWindowStyle = $(if ($ShowWindow) { 'Normal' } else { 'Hidden' })
  plannedCases = $Scenarios.Count * $Views.Count * $passes.Count; completedCases = 0; cases = @()
  scope = 'Arnés individual con delta fijo; FPS principales sin instrumentación. Perfil CPU inclusivo separado. No cubre una sesión LAN con dos procesos ni todas las mecánicas.'
  stageScope = 'Los tiempos de etapas son inclusivos; no sumar padre e hijos. Sin --profile las etapas no registran llamadas.'
}
$reportPath = Join-Path $OutputRoot 'summary.json'
Save-Json $report $reportPath

try {
  foreach ($scenario in $Scenarios) {
    foreach ($view in $Views) {
      $timingResult = $null
      foreach ($pass in $passes) {
        $caseId = '{0:D2}-{1}-{2}-{3}' -f ($rows.Count + 1), $scenario, $view, $pass
        $caseDirectory = Join-Path $OutputRoot $caseId
        $stdout = Join-Path $OutputRoot ($caseId + '.stdout.log')
        $stderr = Join-Path $OutputRoot ($caseId + '.stderr.log')
        $report.activeCase = $caseId
        Save-Json $report $reportPath
        Require ((File-Sha $Executable) -ceq $executableHash) 'Cambió el ejecutable durante el lote'
        Require ((File-Sha $configPath) -ceq $configHash) 'Cambió Config durante el lote'
        Require (Same-Value (Optional-Sha $savePath) $saveHash) 'Cambió Save original antes del caso'
        Require (-not (Test-Path -LiteralPath $caseDirectory)) "La salida de caso ya existe: $caseDirectory"
        $arguments = @('--output', ('"' + $caseDirectory + '"'), '--frames', "$Frames", '--warmup', "$Warmup", '--width', "$Width", '--height', "$Height", '--scenario', $scenario, '--moving')
        if ($view -eq 'fps') { $arguments += '--fps' }
        if ($pass -eq 'profile') { $arguments += '--profile' }
        $startedUtc = [DateTime]::UtcNow.ToString('o')
        Write-Host "[$($rows.Count + 1)/$($report.plannedCases)] $caseId"
        $windowStyle = if ($ShowWindow) { 'Normal' } else { 'Hidden' }
        $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $Workspace -WindowStyle $windowStyle -RedirectStandardOutput $stdout -RedirectStandardError $stderr -Wait -PassThru
        Require (Same-Value (Optional-Sha $savePath) $saveHash) 'Cambió Save original al ejecutar el caso'
        Require ($process.ExitCode -eq 0) "Falló $caseId, exit $($process.ExitCode); consulta $stderr"
        $resultPath = Join-Path $caseDirectory 'result.json'
        Require (Test-Path -LiteralPath $resultPath -PathType Leaf) "Falta result.json: $caseId"
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        $expected = [ordered]@{
          status = 'PASS'; configuration = 'Release'; frames = $Frames; warmup = $Warmup; scenario = $scenario
          firstPerson = ($view -eq 'fps'); instrumentation = ($pass -eq 'profile'); moving = $true; stress = $false
          fixedSimulationDelta = $true; gpuFence = $false; nativeSize = @($Width, $Height)
        }
        Check-Parameters $result $expected $caseId
        Require ((File-Sha (Join-Path $caseDirectory 'runtime/Content/Config.json')) -ceq $configHash) "Config aislado distinto: $caseId"
        foreach ($metric in @('averageFps', 'meanMs', 'p95Ms', 'p99Ms', 'maxMs')) { Positive-Metric $result.frame.$metric "$caseId/frame/$metric" }
        if ($pass -eq 'timing') { $timingResult = $result }
        else {
          $expectedPair = [ordered]@{}
          foreach ($field in $pairFields) { $expectedPair[$field] = $timingResult.$field }
          Check-Parameters $result $expectedPair "$caseId/perfil frente a timing"
        }
        $row = [ordered]@{
          order = $rows.Count + 1; case = $caseId; scenario = $scenario; view = $view; pass = $pass; instrumentation = ($pass -eq 'profile')
          processId = $process.Id; startedUtc = $startedUtc; finishedUtc = [DateTime]::UtcNow.ToString('o')
          configuration = $result.configuration; frames = $result.frames; warmup = $result.warmup; seed = $result.seed
          guards = $result.guards; objects = $result.objects; obstacles = $result.obstacles; surfaces = $result.surfaces
          fps = $result.frame.averageFps; frameMeanMs = $result.frame.meanMs; p95Ms = $result.frame.p95Ms; p99Ms = $result.frame.p99Ms; maxMs = $result.frame.maxMs
          updateMeanMs = $result.update.meanMs; renderMeanMs = $result.renderSubmission.meanMs; presentMeanMs = $result.present.meanMs
          cpuCoreEquivalent = $result.cpuCoreEquivalent; privateBytes = $result.memoryFinal.privateBytes
          executableSha256 = $executableHash; contentSha256 = $contentManifest.sha256; result = $resultPath
        }
        $stageCalls = [UInt64]0
        foreach ($stage in ($result.stages.PSObject.Properties | Sort-Object Name)) {
          $stageCalls += [UInt64]$stage.Value.calls
          $row['stage_' + $stage.Name + '_perFrameMs'] = $stage.Value.perFrameMs
          $row['stage_' + $stage.Name + '_calls'] = $stage.Value.calls
          $stageRows.Add([pscustomobject][ordered]@{
            case = $caseId; scenario = $scenario; view = $view; pass = $pass; instrumentation = ($pass -eq 'profile')
            stage = $stage.Name; totalCpuMs = $stage.Value.milliseconds; calls = $stage.Value.calls; perFrameMs = $stage.Value.perFrameMs
          })
        }
        if ($pass -eq 'profile') { Require ($stageCalls -gt 0) "La pasada instrumentada no registró etapas: $caseId" }
        $rows.Add([pscustomobject]$row)
        $report.completedCases = $rows.Count; $report.cases = @($rows.ToArray())
        Save-Json $report $reportPath
      }
    }
  }
  Require ($rows.Count -eq $report.plannedCases) 'El lote no contiene todos los casos previstos'
  Require ((File-Sha $Executable) -ceq $executableHash) 'Cambió el ejecutable al finalizar el lote'
  Require ((Content-Manifest $contentDirectory).sha256 -ceq $contentManifest.sha256) 'Cambió Content durante el lote'
  $report.saveSha256After = Optional-Sha $savePath
  Require (Same-Value $report.saveSha256After $saveHash) 'Cambió Save original durante el lote'
  $report.status = 'PASS'; $report.activeCase = $null
} catch {
  $report.status = 'FAIL'; $report.failure = $_.Exception.Message
  throw
} finally {
  $report.finishedUtc = [DateTime]::UtcNow.ToString('o'); $report.saveSha256After = Optional-Sha $savePath
  Save-Json $report $reportPath
  if ($rows.Count -gt 0) {
    $rows.ToArray() | Export-Csv -LiteralPath (Join-Path $OutputRoot 'summary.csv') -NoTypeInformation -Encoding UTF8
    $rows.ToArray() | Select-Object case, fps, frameMeanMs, p95Ms, p99Ms, maxMs, updateMeanMs, renderMeanMs, presentMeanMs | Format-Table -AutoSize | Out-String | Set-Content -LiteralPath (Join-Path $OutputRoot 'summary-table.txt') -Encoding UTF8
  }
  if ($stageRows.Count -gt 0) { $stageRows.ToArray() | Export-Csv -LiteralPath (Join-Path $OutputRoot 'stages.csv') -NoTypeInformation -Encoding UTF8 }
  Write-Host "Reporte: $reportPath"
}

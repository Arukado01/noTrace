param([int]$Parallel = 2)

. (Join-Path $PSScriptRoot 'Production.ps1')
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$production = Join-Path $repository 'production'
$pointer = Join-Path $production 'current.txt'
$play = Join-Path $repository 'play.exe'
$qa = Join-Path $production ('.work/qa-' + (Get-ProductionId))
[IO.Directory]::CreateDirectory($qa) | Out-Null
$results = [Collections.Generic.List[string]]::new()
$launcher = $null
$child = $null
$secondLauncher = $null
$secondChild = $null
$oldReport = [Environment]::GetEnvironmentVariable('NO_TRACE_LAUNCHER_REPORT', 'Process')
$save = Join-Path $repository 'Content/Save.json'
$originalSave = if (Test-Path -LiteralPath $save) { (Get-ProductionFileHash -LiteralPath $save -Algorithm SHA256).Hash } else { '<ausente>' }

function Assert-Production {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
    $results.Add("PASS $Message")
    Write-Host "PASS $Message"
}

function Invoke-ProductionLauncherValidation {
    param([string]$Launcher, [string[]]$Arguments)
    $quoted = foreach ($argument in $Arguments) {
        $escaped = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
        $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
        '"' + $escaped + '"'
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Launcher
    $start.Arguments = $quoted -join ' '
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Launcher))
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $validation = [Diagnostics.Process]::new()
    $validation.StartInfo = $start
    try {
        if (-not $validation.Start()) { throw 'No se pudo iniciar la validación del lanzador.' }
        # Los ejecutables GUI requieren espera explícita y su propio ExitCode en PowerShell 5.
        $validation.WaitForExit()
        return $validation.ExitCode
    } finally { $validation.Dispose() }
}

try {
    Assert-Production ((Test-Path -LiteralPath $pointer) -and (Test-Path -LiteralPath $play)) 'Existe una publicación previa y un play.exe real.'
    $signature = [IO.File]::ReadAllBytes($play)
    Assert-Production ($signature.Length -gt 2048 -and $signature[0] -eq 77 -and $signature[1] -eq 90) 'play.exe contiene una imagen ejecutable MZ.'
    $validationCode = Invoke-ProductionLauncherValidation $play @('--validate-production')
    Assert-Production ($validationCode -eq 0) 'El lanzador valida íntegramente la publicación vigente.'
    $oldId = (Get-Content -LiteralPath $pointer -Raw).Trim()
    $oldRelease = Join-Path $production "releases/$oldId"
    $oldGame = Join-Path $oldRelease 'AwesomeEngine.exe'
    $oldGameHash = (Get-ProductionFileHash -LiteralPath $oldGame -Algorithm SHA256).Hash
    $oldManifestHash = (Get-ProductionFileHash -LiteralPath (Join-Path $oldRelease 'release.manifest') -Algorithm SHA256).Hash
    $launcherHash = (Get-ProductionFileHash -LiteralPath $play -Algorithm SHA256).Hash
    $report = Join-Path $qa 'launcher-report.txt'
    [Environment]::SetEnvironmentVariable('NO_TRACE_LAUNCHER_REPORT', $report, 'Process')
    $launcher = Start-Process -FilePath $play -ArgumentList @('--scene', 'NoTraceGame') -WorkingDirectory $repository -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while (-not (Test-Path -LiteralPath $report) -and [DateTime]::UtcNow -lt $deadline -and -not $launcher.HasExited) { Start-Sleep -Milliseconds 100 }
    Assert-Production (Test-Path -LiteralPath $report) 'play.exe crea el proceso de producción desde la raíz.'
    $fields = @{}
    foreach ($line in Get-Content -LiteralPath $report -Encoding UTF8) { $pair = $line.Split('=', 2); if ($pair.Length -eq 2) { $fields[$pair[0]] = $pair[1] } }
    Assert-Production ($fields['release'] -eq $oldRelease -and $fields['arguments'] -eq ' "--scene" "NoTraceGame"') 'El lanzador conserva argumentos y el directorio de recursos de producción.'
    $sharedSave = Join-Path $production 'user-data/Save.json'
    Assert-Production ($fields['save'] -eq $sharedSave) 'La partida usa el guardado común de producción fuera de las publicaciones inmutables.'
    $child = Get-Process -Id ([int]$fields['pid'])
    Start-Sleep -Seconds 5
    Assert-Production (-not $child.HasExited -and $child.Path -eq $oldGame -and -not $launcher.HasExited) 'La producción abre NO TRACE y permanece ejecutándose con sus recursos.'
    $newRelease = Invoke-NoTraceProduction -RepositoryRoot $repository -Parallel $Parallel
    Assert-Production (-not $child.HasExited -and -not $launcher.HasExited) 'El juego anterior permanece abierto durante otra compilación y publicación Release.'
    Assert-Production ($newRelease -ne $oldRelease -and (Get-Content -LiteralPath $pointer -Raw).Trim() -ne $oldId) 'La nueva producción se publica en otra carpeta.'
    Assert-Production ((Get-ProductionFileHash -LiteralPath $oldGame -Algorithm SHA256).Hash -eq $oldGameHash -and (Get-ProductionFileHash -LiteralPath (Join-Path $oldRelease 'release.manifest') -Algorithm SHA256).Hash -eq $oldManifestHash) 'El ejecutable y el manifiesto de la partida en uso permanecen intactos.'
    Assert-Production ((Get-ProductionFileHash -LiteralPath $play -Algorithm SHA256).Hash -eq $launcherHash) 'El lanzador abierto no se sustituye durante la partida.'
    $secondReport = Join-Path $qa 'launcher-new-release.txt'
    [Environment]::SetEnvironmentVariable('NO_TRACE_LAUNCHER_REPORT', $secondReport, 'Process')
    $secondLauncher = Start-Process -FilePath $play -ArgumentList @('--scene', 'NoTraceGame') -WorkingDirectory $repository -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while (-not (Test-Path -LiteralPath $secondReport) -and [DateTime]::UtcNow -lt $deadline -and -not $secondLauncher.HasExited) { Start-Sleep -Milliseconds 100 }
    Assert-Production (Test-Path -LiteralPath $secondReport) 'Una segunda ejecución de play.exe abre la nueva publicación.'
    $secondFields = @{}
    foreach ($line in Get-Content -LiteralPath $secondReport -Encoding UTF8) { $pair = $line.Split('=', 2); if ($pair.Length -eq 2) { $secondFields[$pair[0]] = $pair[1] } }
    $secondChild = Get-Process -Id ([int]$secondFields['pid'])
    Start-Sleep -Seconds 3
    Assert-Production ($secondFields['release'] -eq $newRelease -and $secondFields['save'] -eq $sharedSave -and -not $secondChild.HasExited) 'Ambas versiones abiertas comparten la ruta persistente y usan sus propios recursos.'
    $beforeFailure = [IO.File]::ReadAllText($pointer)
    $beforeManifest = (Get-ProductionFileHash -LiteralPath (Join-Path $newRelease 'release.manifest') -Algorithm SHA256).Hash
    $productionLogs = @(Get-ChildItem -LiteralPath (Join-Path $production 'logs') -Filter 'production-*.log').Count
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'Build.ps1') --target NoTraceControlledMissingTarget
    Assert-Production ($LASTEXITCODE -ne 0) 'Un build de desarrollo con objetivo inexistente termina con error real de Ninja.'
    Assert-Production ([IO.File]::ReadAllText($pointer) -eq $beforeFailure -and @(Get-ChildItem -LiteralPath (Join-Path $production 'logs') -Filter 'production-*.log').Count -eq $productionLogs) 'El fallo de desarrollo no inicia una compilación ni una publicación de producción.'
    $failed = $false
    try { Invoke-NoTraceProduction -RepositoryRoot $repository -Parallel $Parallel -InjectCompileFailure | Out-Null }
    catch { $failed = $true; $results.Add("FALLO ESPERADO $($_.Exception.Message)") }
    Assert-Production $failed 'Una compilación Release aislada con #error falla realmente en MSVC.'
    $failureLog = Get-ChildItem -LiteralPath (Join-Path $production 'logs') -Filter 'production-*.log' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $failureText = Get-Content -LiteralPath $failureLog.FullName -Raw -Encoding UTF8
    Assert-Production ($failureText -match 'C1189.*Fallo de compilacion' -and $failureText -match 'COMANDO cmake --build .*failure-') 'El log demuestra un build Release ejecutado y el diagnóstico C1189 del #error controlado.'
    Assert-Production ([IO.File]::ReadAllText($pointer) -eq $beforeFailure -and (Get-ProductionFileHash -LiteralPath (Join-Path $newRelease 'release.manifest') -Algorithm SHA256).Hash -eq $beforeManifest) 'El fallo conserva byte a byte la referencia y el manifiesto de la última publicación válida.'
    Assert-Production (-not $child.HasExited) 'El fallo de producción tampoco cierra la partida anterior.'
    $fakeRoot = Join-Path $qa 'sin-publicacion'
    [IO.Directory]::CreateDirectory($fakeRoot) | Out-Null
    $fakeLauncher = Join-Path $fakeRoot 'play.exe'
    [IO.File]::Copy($play, $fakeLauncher)
    $validationCode = Invoke-ProductionLauncherValidation $fakeLauncher @('--validate-production')
    Assert-Production ($validationCode -ne 0) 'Sin publicación válida el lanzador devuelve error y no busca un build de desarrollo.'
    $incomplete = Join-Path $qa 'publicacion-incompleta'
    Sync-ProductionTree $newRelease $incomplete $qa
    Remove-Item -LiteralPath (Join-Path $incomplete 'Content/Audio/NoTrace/Weapons/SuppressorAttach-01.wav')
    $validationCode = Invoke-ProductionLauncherValidation $play @('--validate-release', $incomplete)
    Assert-Production ($validationCode -ne 0) 'El lanzador rechaza una publicación completa cuyo efecto de audio obligatorio fue eliminado.'
    $missingResourceRejected = $false
    try { Write-ProductionManifest $incomplete (Join-Path $PSScriptRoot 'RequiredContent.txt') } catch { $missingResourceRejected = $true }
    Assert-Production $missingResourceRejected 'El publicador también rechaza un recurso ausente antes de regenerar el manifiesto.'
    [IO.File]::Copy((Join-Path $newRelease 'Content/Audio/NoTrace/Weapons/SuppressorAttach-01.wav'), (Join-Path $incomplete 'Content/Audio/NoTrace/Weapons/SuppressorAttach-01.wav'))
    Remove-Item -LiteralPath (Join-Path $incomplete 'Content/Textures/NoTrace/Environment/MilitaryFurniture.png')
    $missingEnvironmentRejected = $false
    try { Write-ProductionManifest $incomplete (Join-Path $PSScriptRoot 'RequiredContent.txt') } catch { $missingEnvironmentRejected = $true }
    Assert-Production $missingEnvironmentRejected 'El publicador exige la textura de mobiliario militar utilizada por el renderer.'
    $validationCode = Invoke-ProductionLauncherValidation $play @('--validate-release', $incomplete)
    Assert-Production ($validationCode -ne 0) 'El lanzador rechaza también una publicación sin la textura obligatoria del entorno.'
    $lock = Get-ProductionLock $repository
    try {
        $blocked = $false
        try { $secondLock = Get-ProductionLock $repository; $secondLock.Dispose() } catch { $blocked = $true }
        Assert-Production $blocked 'El bloqueo exclusivo impide dos compilaciones de producción simultáneas.'
    } finally { $lock.Dispose() }
    $finalSave = if (Test-Path -LiteralPath $save) { (Get-ProductionFileHash -LiteralPath $save -Algorithm SHA256).Hash } else { '<ausente>' }
    Assert-Production ($finalSave -eq $originalSave) 'Las pruebas de producción no modifican la partida de desarrollo del usuario.'
    $results.Add("PUBLICACIÓN FINAL $newRelease")
    $results.Add("EVIDENCIA $qa")
} catch {
    $results.Add("FAIL $($_.Exception.Message)")
    throw
} finally {
    [Environment]::SetEnvironmentVariable('NO_TRACE_LAUNCHER_REPORT', $oldReport, 'Process')
    foreach ($ownedChild in @($secondChild, $child)) {
        if ($ownedChild) {
            $ownedChild.Refresh()
            if (-not $ownedChild.HasExited) {
                $closed = $ownedChild.CloseMainWindow()
                if (-not $closed -or -not $ownedChild.WaitForExit(15000)) { Stop-Process -Id $ownedChild.Id -ErrorAction SilentlyContinue }
            }
        }
    }
    if ($secondLauncher) { $secondLauncher.WaitForExit(15000) | Out-Null }
    if ($launcher) { $launcher.WaitForExit(15000) | Out-Null }
    [IO.File]::WriteAllText((Join-Path $qa 'results.txt'), (($results -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    Write-Host "Evidencia de producción: $qa"
}

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$BuildArguments = @())

. (Join-Path $PSScriptRoot 'Production.ps1')
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$lock = $null
try {
    $lock = Get-ProductionLock $repository
    $id = Get-ProductionId
    $logs = Join-Path $repository 'production/logs'
    [IO.Directory]::CreateDirectory($logs) | Out-Null
    $script:ProductionLog = Join-Path $logs "development-$id.log"
    Write-ProductionLog "DESARROLLO INICIO; build=$repository\build"
    Invoke-ProductionCommand 'cmake' (@('--build', (Join-Path $repository 'build')) + $BuildArguments)
    Write-ProductionLog 'DESARROLLO CORRECTO; comienza la compilación de producción independiente.'
    $release = Invoke-NoTraceProduction -RepositoryRoot $repository -Parallel 2 -LockHeld
    Write-ProductionLog "BUILD COMPLETO; producción=$release"
    exit 0
} catch {
    Write-ProductionLog "BUILD FALLIDO; no se publica una versión incompleta. $($_.Exception.Message)"
    exit 1
} finally {
    if ($lock) { $lock.Dispose() }
}

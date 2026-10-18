param([string]$WorkDirectory = [IO.Path]::GetTempPath())

. (Join-Path $PSScriptRoot 'Production.ps1')
$fixture = Join-Path $WorkDirectory ('notrace-production-primitives-' + (Get-ProductionId))
$source = Join-Path $fixture 'source'
$workspace = Join-Path $fixture 'work'
$destination = Join-Path $workspace 'snapshot'
[IO.Directory]::CreateDirectory($source) | Out-Null
[IO.Directory]::CreateDirectory($workspace) | Out-Null
$sourceFile = Join-Path $source 'Changed.cpp'
$destinationFile = Join-Path $destination 'Changed.cpp'
[IO.File]::WriteAllText($sourceFile, 'contenido-anterior', [Text.UTF8Encoding]::new($false))
[IO.File]::SetLastWriteTimeUtc($sourceFile, [DateTime]::UtcNow.AddHours(-2))
Sync-ProductionTree $source $destination $workspace
$oldTime = [IO.File]::GetLastWriteTimeUtc($destinationFile)
Start-Sleep -Milliseconds 60
$before = [DateTime]::UtcNow
[IO.File]::WriteAllText($sourceFile, 'contenido-cambiado', [Text.UTF8Encoding]::new($false))
[IO.File]::SetLastWriteTimeUtc($sourceFile, [DateTime]::UtcNow.AddHours(-2))
Sync-ProductionTree $source $destination $workspace
$newTime = [IO.File]::GetLastWriteTimeUtc($destinationFile)
if ($newTime -le $oldTime -or $newTime -lt $before -or [IO.File]::ReadAllText($destinationFile) -ne 'contenido-cambiado') { throw 'El snapshot no invalida el objeto compilado con contenido anterior.' }
Sync-ProductionTree $source $destination $workspace
$sameTime = [IO.File]::GetLastWriteTimeUtc($destinationFile)
if ($sameTime -ne $newTime) { throw 'Un sync sin cambios invalida innecesariamente el objeto compilado.' }
$active = Join-Path $workspace 'current.txt'
$pending = Join-Path $workspace 'pending.txt'
[IO.File]::WriteAllText($pending, 'release-anterior', [Text.UTF8Encoding]::new($false))
Set-ProductionAtomicFile $pending $active
[IO.File]::WriteAllText($pending, 'release-nuevo', [Text.UTF8Encoding]::new($false))
Set-ProductionAtomicFile $pending $active
if ([IO.File]::ReadAllText($active) -ne 'release-nuevo' -or [IO.File]::Exists($pending)) { throw 'El reemplazo atómico no conservó el contenido nuevo.' }
$report = [pscustomobject]@{ PowerShell = $PSVersionTable.PSVersion.ToString(); SourceTime = [IO.File]::GetLastWriteTimeUtc($sourceFile).ToString('o'); OldSnapshot = $oldTime.ToString('o'); NewSnapshot = $newTime.ToString('o'); UnchangedSnapshot = $sameTime.ToString('o'); Atomic = [IO.File]::ReadAllText($active); Fixture = $fixture }
$json = $report | ConvertTo-Json -Compress
[IO.File]::WriteAllText((Join-Path $fixture 'results.json'), $json, [Text.UTF8Encoding]::new($false))
Write-Output $json

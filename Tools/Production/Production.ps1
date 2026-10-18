Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# El entorno de VS Code puede heredar rutas de módulos de otra versión de PowerShell.
Import-Module -Name ([IO.Path]::Combine($PSHOME, 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1')) -ErrorAction Stop

function Get-ProductionFileHash {
    param([string]$LiteralPath, [ValidateSet('SHA256')][string]$Algorithm = 'SHA256')
    $stream = $null
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($LiteralPath)
        $digest = $hasher.ComputeHash($stream)
        return [pscustomobject]@{ Algorithm = $Algorithm; Hash = [BitConverter]::ToString($digest).Replace('-', ''); Path = $LiteralPath }
    } finally {
        if ($stream) { $stream.Dispose() }
        $hasher.Dispose()
    }
}

function Write-ProductionLog {
    param([string]$Message)
    $line = '[{0}] {1}' -f [DateTime]::UtcNow.ToString('o'), $Message
    Write-Host $line
    if ($script:ProductionLog) { Add-Content -LiteralPath $script:ProductionLog -Value $line -Encoding UTF8 }
}

function Invoke-ProductionCommand {
    param([string]$Executable, [string[]]$Arguments, [int[]]$SuccessExitCodes = @(0))
    $command = (Get-Command $Executable -CommandType Application -ErrorAction Stop).Source
    Write-ProductionLog ('COMANDO {0} {1}' -f $Executable, ($Arguments -join ' '))
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $command @Arguments 2>&1 | ForEach-Object { $line = "$_"; Write-Host $line; Add-Content -LiteralPath $script:ProductionLog -Value $line -Encoding UTF8 }
        $result = $LASTEXITCODE
    } finally { $ErrorActionPreference = $oldPreference }
    if ($result -notin $SuccessExitCodes) { throw "El comando terminó con código $result`: $Executable" }
}

function Get-ProductionLock {
    param([string]$RepositoryRoot)
    $directory = Join-Path $RepositoryRoot 'production'
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    try { return [IO.File]::Open((Join-Path $directory 'build.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw 'Ya hay una compilación de desarrollo/producción en curso; la publicación actual se conserva.' }
}

function Get-ProductionId {
    return ('{0}-{1}' -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'), [Guid]::NewGuid().ToString('N').Substring(0, 8))
}

function Sync-ProductionTree {
    param([string]$Source, [string]$Destination, [string]$Workspace)
    $base = [IO.Path]::GetFullPath($Workspace).TrimEnd('\') + '\'
    $target = [IO.Path]::GetFullPath($Destination)
    if (-not $target.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'La carpeta de trabajo de producción salió de su ubicación permitida.' }
    [IO.Directory]::CreateDirectory($target) | Out-Null
    $sourceBase = [IO.Path]::GetFullPath($Source).TrimEnd('\') + '\'
    $expected = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Source -File -Recurse) {
        $relative = $file.FullName.Substring($sourceBase.Length)
        if ($relative -eq 'Save.json' -or $relative.StartsWith('Screenshots\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        $expected[$relative] = $true
        $destinationFile = Join-Path $target $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationFile)) | Out-Null
        $existing = Get-Item -LiteralPath $destinationFile -ErrorAction SilentlyContinue
        if (-not $existing -or $existing.Length -ne $file.Length -or (Get-ProductionFileHash -LiteralPath $existing.FullName -Algorithm SHA256).Hash -ne (Get-ProductionFileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
            [IO.File]::Copy($file.FullName, $destinationFile, $true)
            [IO.File]::SetLastWriteTimeUtc($destinationFile, [DateTime]::UtcNow)
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $target -File -Recurse) {
        if (-not $expected.ContainsKey($file.FullName.Substring($target.Length + 1))) { Remove-Item -LiteralPath $file.FullName -Force }
    }
}

function Get-ProductionImports {
    param([string]$Binary)
    $output = & dumpbin /NOLOGO /DEPENDENTS $Binary 2>&1
    if ($LASTEXITCODE -ne 0) { throw "No se pudo verificar las dependencias de $Binary" }
    return @($output | ForEach-Object { if ("$_" -match '^\s+([A-Za-z0-9_.+\-]+\.dll)\s*$') { $Matches[1] } } | Sort-Object -Unique)
}

function Sync-ProductionVendor {
    param([string]$Source, [string]$Destination, [string]$Workspace)
    $base = [IO.Path]::GetFullPath($Workspace).TrimEnd('\') + '\'
    $target = [IO.Path]::GetFullPath($Destination)
    $origin = [IO.Path]::GetFullPath($Source)
    if (-not $target.StartsWith($base, [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($target) -notmatch '^[a-zA-Z0-9_+\-]+-src$') { throw 'El mirror de dependencias salió de su carpeta generada de producción.' }
    if ($target.StartsWith($origin.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $target -eq $origin) { throw 'El mirror de dependencias no puede contener sus propias fuentes.' }
    # Robocopy admite rutas largas y solo limpia el snapshot generado que acaba de verificarse.
    Invoke-ProductionCommand 'robocopy' @($origin, $target, '/MIR', '/COPY:DT', '/DCOPY:T', '/R:1', '/W:1', '/XD', '.git', '/XF', '.git', '/NFL', '/NDL', '/NJH', '/NJS', '/NP') -SuccessExitCodes @(0..7)
}

function Copy-ProductionDependencies {
    param([string]$Stage, [string[]]$BuildDirectories)
    $systemNames = @('kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll', 'winmm.dll', 'ws2_32.dll', 'crypt32.dll', 'bcrypt.dll', 'imm32.dll', 'dwmapi.dll', 'opengl32.dll', 'glu32.dll', 'version.dll', 'shlwapi.dll', 'comdlg32.dll', 'comctl32.dll', 'setupapi.dll', 'cfgmgr32.dll', 'secur32.dll', 'ntdll.dll', 'msvcrt.dll', 'winspool.drv', 'normaliz.dll', 'powrprof.dll', 'iphlpapi.dll', 'dnsapi.dll', 'winhttp.dll', 'wininet.dll', 'propsys.dll', 'd3d11.dll', 'dxgi.dll', 'uxtheme.dll', 'hid.dll')
    $checked = @{}
    while ($true) {
        $unexamined = @(Get-ChildItem -LiteralPath $Stage -File | Where-Object { ($_.Extension -in @('.exe', '.dll')) -and -not $checked.ContainsKey($_.Name) })
        if ($unexamined.Count -eq 0) { break }
        foreach ($binary in $unexamined) {
            $checked[$binary.Name] = $true
            foreach ($dependency in Get-ProductionImports $binary.FullName) {
                if ($dependency.ToLowerInvariant() -in $systemNames -or $dependency -match '^(api|ext)-ms-win-') { continue }
                if (Test-Path -LiteralPath (Join-Path $Stage $dependency) -PathType Leaf) { continue }
                $candidates = @(foreach ($directory in $BuildDirectories) { Get-ChildItem -LiteralPath $directory -File -Recurse -Filter $dependency })
                if ($candidates.Count -eq 0) { throw "Dependencia no distribuible o ausente: $dependency, requerida por $($binary.Name)." }
                $hashes = @($candidates | ForEach-Object { (Get-ProductionFileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } | Select-Object -Unique)
                if ($hashes.Count -ne 1) { throw "Hay versiones ambiguas de la dependencia $dependency." }
                [IO.File]::Copy($candidates[0].FullName, (Join-Path $Stage $dependency))
            }
        }
    }
}

function Write-ProductionManifest {
    param([string]$Stage, [string]$Inventory)
    $required = @('AwesomeEngine.exe') + @(Get-Content -LiteralPath $Inventory -Encoding UTF8 | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    foreach ($relative in $required) {
        $path = Join-Path $Stage $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) { throw "Recurso obligatorio ausente o vacío: $relative" }
    }
    $configuration = Get-Content -LiteralPath (Join-Path $Stage 'Content/Config.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $configuration.windowTitle -or -not $configuration.windowResolution) { throw 'La configuración de producción es incompleta.' }
    $rootLength = $Stage.TrimEnd('\').Length + 1
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('NO_TRACE_RELEASE_V1')
    foreach ($file in Get-ChildItem -LiteralPath $Stage -File -Recurse | Sort-Object FullName) {
        $relative = $file.FullName.Substring($rootLength).Replace('\', '/')
        if ($relative -in 'release.manifest', 'Content/Save.json' -or $relative.StartsWith('Content/Screenshots/')) { continue }
        $hash = (Get-ProductionFileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash`t$relative")
    }
    [IO.File]::WriteAllText((Join-Path $Stage 'release.manifest'), (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
}

function Set-ProductionAtomicFile {
    param([string]$Source, [string]$Destination)
    if (Test-Path -LiteralPath $Destination) { [IO.File]::Replace($Source, $Destination, [System.Management.Automation.Language.NullString]::Value) }
    else { [IO.File]::Move($Source, $Destination) }
}

function Invoke-NoTraceProduction {
    param([string]$RepositoryRoot, [int]$Parallel = 2, [switch]$InjectCompileFailure, [switch]$LockHeld)
    $lock = $null
    if (-not $LockHeld) { $lock = Get-ProductionLock $RepositoryRoot }
    $oldLog = $script:ProductionLog
    $id = Get-ProductionId
    $production = Join-Path $RepositoryRoot 'production'
    $work = Join-Path $production '.work'
    [IO.Directory]::CreateDirectory((Join-Path $production 'logs')) | Out-Null
    $script:ProductionLog = Join-Path $production "logs/production-$id.log"
    try {
        Write-ProductionLog "PRODUCCIÓN INICIO $id; la publicación actual no se modifica hasta validar todos los archivos."
        $snapshot = Join-Path $work 'source'
        foreach ($directory in 'Engine', 'Games', 'Content') { Sync-ProductionTree (Join-Path $RepositoryRoot $directory) (Join-Path $snapshot $directory) $work }
        [IO.File]::Copy((Join-Path $RepositoryRoot 'CMakeLists.txt'), (Join-Path $snapshot 'CMakeLists.txt'), $true)
        $build = Join-Path $work 'release-build'
        if ($InjectCompileFailure) { $build = Join-Path $work "failure-$id" }
        $dependencyBase = Join-Path $build '_deps'
        $configure = @('-S', $snapshot, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DBUILD_SHARED_LIBS=OFF', '-DTGUI_SHARED_LIBS=OFF', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DSFML_USE_STATIC_STD_LIBS=ON', '-DTGUI_USE_STATIC_STD_LIBS=ON', '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON', '-DCMAKE_PROJECT_AwesomeEngine_INCLUDE:FILEPATH=', ('-DFETCHCONTENT_BASE_DIR:PATH=' + $dependencyBase))
        foreach ($dependency in Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'build/_deps') -Directory -Filter '*-src') {
            if (Test-Path -LiteralPath (Join-Path $dependency.FullName 'CMakeLists.txt')) {
                $name = $dependency.Name.Substring(0, $dependency.Name.Length - 4).ToUpperInvariant()
                $dependencySnapshot = Join-Path $dependencyBase $dependency.Name
                Sync-ProductionVendor $dependency.FullName $dependencySnapshot $work
                $configure += "-DFETCHCONTENT_SOURCE_DIR_$name=$dependencySnapshot"
            }
        }
        Invoke-ProductionCommand 'cmake' $configure
        if ($InjectCompileFailure) {
            $header = (Join-Path $RepositoryRoot 'Tools/Production/FailureProbe.h').Replace('\', '/')
            Invoke-ProductionCommand 'cmake' @('-S', $snapshot, '-B', $build, "-DCMAKE_CXX_FLAGS=/FI`"$header`"")
        }
        Invoke-ProductionCommand 'cmake' @('--build', $build, '--target', 'AwesomeEngine', '--parallel', "$Parallel")
        if ($InjectCompileFailure) { throw 'La compilación con #error terminó inesperadamente; no se publicará.' }
        $launcherSource = Join-Path $work 'launcher-source'
        Sync-ProductionTree (Join-Path $RepositoryRoot 'Tools/Production/Launcher') $launcherSource $work
        $launcherBuild = Join-Path $work 'launcher-build'
        Invoke-ProductionCommand 'cmake' @('-S', $launcherSource, '-B', $launcherBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
        Invoke-ProductionCommand 'cmake' @('--build', $launcherBuild, '--parallel', "$Parallel")
        $stage = Join-Path $work "stage-$id"
        [IO.Directory]::CreateDirectory($stage) | Out-Null
        $game = Join-Path $build 'AwesomeEngine.exe'
        if (-not (Test-Path -LiteralPath $game -PathType Leaf)) { throw "No existe el ejecutable real de CMake: $game" }
        [IO.File]::Copy($game, (Join-Path $stage 'AwesomeEngine.exe'))
        Sync-ProductionTree (Join-Path $snapshot 'Content') (Join-Path $stage 'Content') $work
        Copy-ProductionDependencies $stage @($build, $launcherBuild)
        $launcher = Join-Path $launcherBuild 'play.exe'
        foreach ($dependency in Get-ProductionImports $launcher) {
            if ($dependency -match '(?i)(vcruntime|msvcp|ucrtbase|sfml|tgui)') { throw "El lanzador conserva una dependencia externa: $dependency" }
        }
        Write-ProductionManifest $stage (Join-Path $RepositoryRoot 'Tools/Production/RequiredContent.txt')
        Invoke-ProductionCommand $launcher @('--validate-release', $stage)
        $release = Join-Path $production "releases/$id"
        [IO.Directory]::CreateDirectory((Join-Path $production 'releases')) | Out-Null
        [IO.Directory]::Move($stage, $release)
        $rootLauncher = Join-Path $RepositoryRoot 'play.exe'
        $pending = Join-Path $production 'pending-play.exe'
        $needsLauncher = -not (Test-Path -LiteralPath $rootLauncher)
        if (-not $needsLauncher) { $needsLauncher = (Get-ProductionFileHash -LiteralPath $launcher).Hash -ne (Get-ProductionFileHash -LiteralPath $rootLauncher).Hash }
        if ($needsLauncher) {
            $candidate = Join-Path $RepositoryRoot ".play-$id.exe"
            [IO.File]::Copy($launcher, $candidate)
            try {
                $running = @(Get-Process | ForEach-Object { try { if ($_.Path -eq $rootLauncher) { $_ } } catch {} })
                if ($running.Count -gt 0) { throw [IO.IOException]::new('El lanzador sigue abierto durante una partida.') }
                Set-ProductionAtomicFile $candidate $rootLauncher
                Write-ProductionLog "LANZADOR publicado atómicamente: $rootLauncher"
            } catch {
                [IO.File]::Copy($launcher, $pending, $true)
                if (Test-Path -LiteralPath $candidate) { Remove-Item -LiteralPath $candidate }
                Write-ProductionLog "LANZADOR bloqueado; se conserva el anterior y se reintentará en el próximo build: $pending"
                $format = Join-Path $production 'launcher-format.txt'
                if (-not (Test-Path -LiteralPath $format) -or (Get-Content -LiteralPath $format -Raw).Trim() -ne '1') { throw 'El lanzador anterior no tiene compatibilidad comprobada; se conserva la publicación anterior.' }
            }
        }
        $pointer = Join-Path $production ".current-$id.tmp"
        [IO.File]::WriteAllText($pointer, "$id`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $production 'launcher-format.txt'), "1`n", [Text.UTF8Encoding]::new($false))
        if ((Test-Path -LiteralPath $pending) -and (Get-ProductionFileHash -LiteralPath $pending).Hash -eq (Get-ProductionFileHash -LiteralPath $rootLauncher).Hash) { Remove-Item -LiteralPath $pending }
        Set-ProductionAtomicFile $pointer (Join-Path $production 'current.txt')
        Write-ProductionLog "PUBLICACIÓN VÁLIDA $release; entrada $rootLauncher"
        return $release
    } catch {
        Write-ProductionLog "PRODUCCIÓN FALLIDA; la última publicación válida se conserva. $($_.Exception.Message)"
        throw
    } finally {
        $script:ProductionLog = $oldLog
        if ($lock) { $lock.Dispose() }
    }
}

$script:ProductionLog = $null

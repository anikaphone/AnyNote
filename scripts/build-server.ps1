param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Linux
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

$env:GOTOOLCHAIN = 'local'
$env:CGO_ENABLED = '0'
$env:GOCACHE = Join-Path $repo 'build/temp/go-cache'
$env:GOARCH = 'amd64'

if ($Linux) {
    $env:GOOS = 'linux'
    $targetDir = Join-Path $repo 'build/bin/server/linux-amd64'
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    Push-Location (Join-Path $repo 'src/AnyNoteServer')
    try {
        foreach ($command in @('anynote-server', 'anynote-admin', 'anynote-cert')) {
            $binPath = Join-Path $targetDir $command
            & go build -trimpath -o $binPath "./cmd/$command"
            if ($LASTEXITCODE -ne 0) { throw "Build Linux $command failed: $LASTEXITCODE" }
        }
    } finally {
        Pop-Location
    }
    Write-Output "Linux server binaries built successfully into $targetDir"
} else {
    $env:GOOS = 'windows'
    $binDir = Join-Path $repo 'build/bin'
    $serverConfigDir = Join-Path $repo "build/bin/server/$Configuration"
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null
    New-Item -ItemType Directory -Force -Path $serverConfigDir | Out-Null

    Push-Location (Join-Path $repo 'src/AnyNoteServer')
    try {
        foreach ($command in @('anynote-server', 'anynote-admin', 'anynote-cert')) {
            $binPath = Join-Path $serverConfigDir "$command.exe"
            $buildArgs = @('build', '-trimpath', '-o', $binPath)
            if ($Configuration -eq 'Debug') {
                $buildArgs += @('-gcflags', 'all=-N -l')
            }
            $buildArgs += "./cmd/$command"
            & go @buildArgs
            if ($LASTEXITCODE -ne 0) { throw "Build $command failed: $LASTEXITCODE" }
            Copy-Item -LiteralPath $binPath -Destination (Join-Path $binDir "$command.exe") -Force
        }
    } finally {
        Pop-Location
    }
    Write-Output "Go server components built successfully into $binDir"
}

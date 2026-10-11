param([switch]$Scale, [switch]$LinuxBuild)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$saved = @{}
foreach ($name in @('CGO_ENABLED','GOOS','GOARCH','GOTOOLCHAIN','ANYNOTE_SCALE_TEST')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
Push-Location (Join-Path $repo 'src/AnyNoteServer')
try {
    $env:GOTOOLCHAIN='local'
    $version = go env GOVERSION
    if ($LASTEXITCODE -ne 0 -or $version -ne 'go1.27.2') { throw "Expected go1.27.2, found $version" }
    $env:CGO_ENABLED='0'
    $env:GOOS='windows'
    $env:GOARCH='amd64'
    $env:ANYNOTE_SCALE_TEST=if($Scale){'1'}else{'0'}
    go mod download
    if ($LASTEXITCODE -ne 0) { throw 'go mod download failed' }
    go mod verify
    if ($LASTEXITCODE -ne 0) { throw 'go mod verify failed' }
    $format = gofmt -l cmd internal
    if ($LASTEXITCODE -ne 0 -or $format) { throw "gofmt required: $format" }
    go vet ./...
    if ($LASTEXITCODE -ne 0) { throw 'go vet failed' }
    $env:GOCACHE = Join-Path $repo 'build/temp/go-cache'
    go test -count=1 -v ./...
    if ($LASTEXITCODE -ne 0) { throw 'go test failed' }
    New-Item -ItemType Directory -Force (Join-Path $repo 'build/temp/p0') | Out-Null
    go build -trimpath -o ../../build/temp/p0/p0probe.exe ./cmd/p0probe
    if ($LASTEXITCODE -ne 0) { throw 'Windows probe build failed' }
    New-Item -ItemType Directory -Force (Join-Path $repo 'build/bin') | Out-Null
    foreach ($configuration in @('Debug','Release')) {
        $output = Join-Path $repo "build/bin/server/$configuration"
        New-Item -ItemType Directory -Force $output | Out-Null
        foreach ($command in @('anynote-server','anynote-admin','anynote-cert')) {
            $binPath = Join-Path $output "$command.exe"
            $buildArgs = @('build','-trimpath','-o',$binPath)
            if ($configuration -eq 'Debug') { $buildArgs += @('-gcflags','all=-N -l') }
            $buildArgs += "./cmd/$command"
            & go @buildArgs
            if ($LASTEXITCODE -ne 0) { throw "$configuration $command build failed" }
            if ($configuration -eq 'Release') {
                Copy-Item -LiteralPath $binPath -Destination (Join-Path $repo "build/bin/$command.exe") -Force
            }
        }
    }
    if ($LinuxBuild) {
        $env:GOOS='linux'
        go build -trimpath -o ../../build/temp/p0/p0probe-linux ./cmd/p0probe
        if ($LASTEXITCODE -ne 0) { throw 'Linux probe build failed' }
        go test -c -o ../../build/temp/p0/protocol-linux.test ./internal/protocol
        if ($LASTEXITCODE -ne 0) { throw 'Linux tests build failed' }
        $output = Join-Path $repo 'build/bin/server/linux-amd64'
        New-Item -ItemType Directory -Force $output | Out-Null
        foreach ($command in @('anynote-server','anynote-admin','anynote-cert')) {
            go build -trimpath -o (Join-Path $output $command) "./cmd/$command"
            if ($LASTEXITCODE -ne 0) { throw "Linux $command build failed" }
        }
        foreach ($package in @('api','store','lease')) {
            go test -c -o (Join-Path $output "$package.test") "./internal/$package"
            if ($LASTEXITCODE -ne 0) { throw "Linux $package test build failed" }
        }
    }
} finally {
    Pop-Location
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}

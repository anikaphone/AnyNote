param([string]$OutputDirectory = '', [string]$VcpkgInstalledDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$VcpkgInstalledDirectory) {
    if (!$env:VCPKG_ROOT) { throw 'Set VCPKG_ROOT or pass -VcpkgInstalledDirectory for the dependencies used to build the client' }
    $VcpkgInstalledDirectory = Join-Path $env:VCPKG_ROOT 'installed'
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('build/package/AnyNote-sync-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Package directory must not already exist' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output | Out-Null
foreach ($directory in @('client','server-windows','server-linux','licenses/client','licenses/go','docs','protocol')) {
    New-Item -ItemType Directory -Path (Join-Path $output $directory) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $repo 'build/Release/AnyNote.exe') -Destination (Join-Path $output 'client')
foreach ($command in @('anynote-server','anynote-admin','anynote-cert')) {
    Copy-Item -LiteralPath (Join-Path $repo "build/server/Release/$command.exe") -Destination (Join-Path $output 'server-windows')
    Copy-Item -LiteralPath (Join-Path $repo "build/server/linux-amd64/$command") -Destination (Join-Path $output 'server-linux')
}
foreach ($directory in @('server-windows','server-linux')) {
    foreach ($file in @('README.md','server.example.json')) {
        Copy-Item -LiteralPath (Join-Path $repo "src/AnyNoteServer/$file") -Destination (Join-Path $output $directory)
    }
}
Get-ChildItem -LiteralPath (Join-Path $repo 'docs/同步功能') -Filter '*.md' -File | Copy-Item -Destination (Join-Path $output 'docs')
Get-ChildItem -LiteralPath (Join-Path $repo 'protocol') -File | Copy-Item -Destination (Join-Path $output 'protocol')
foreach ($package in @('sqlite3mc','nlohmann-json','curl','openssl','zlib')) {
    Copy-Item -LiteralPath (Join-Path $VcpkgInstalledDirectory "x64-windows-static/share/$package/copyright") -Destination (Join-Path $output "licenses/client/$package.txt")
}
$modules = & go -C (Join-Path $repo 'src/AnyNoteServer') list -deps -f '{{if .Module}}{{.Module.Path}}|{{.Module.Version}}|{{.Module.Dir}}{{end}}' ./cmd/anynote-server ./cmd/anynote-admin ./cmd/anynote-cert
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Go dependency inventory' }
$modules = @($modules | Where-Object { $_ } | Sort-Object -Unique)
foreach ($module in $modules) {
    $fields = $module.Split('|')
    if (!$fields[1]) { continue }
    $destination = Join-Path $output ('licenses/go/' + $fields[0].Replace('/','_') + '@' + $fields[1])
    New-Item -ItemType Directory -Path $destination | Out-Null
    $licenses = @(Get-ChildItem -LiteralPath $fields[2] -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT|NOTICE)' })
    if (!$licenses) { throw "Missing license: $($fields[0])" }
    $licenses | Copy-Item -Destination $destination
}
$goRoot = & go env GOROOT
Copy-Item -LiteralPath (Join-Path $goRoot 'LICENSE') -Destination (Join-Path $output 'licenses/go/Go-LICENSE.txt')
$head = & git -C $repo rev-parse HEAD
$dirty = [bool](& git -C $repo status --porcelain)
$manifest = [ordered]@{
    created_at = [DateTime]::UtcNow.ToString('o')
    source_head = "$head"
    source_dirty = $dirty
    go_version = (& go version)
    vcpkg_mode = 'classic (locally managed dependencies)'
    go_modules = @($modules | ForEach-Object { $parts = $_.Split('|'); "$($parts[0])@$($parts[1])" })
    files = @(Get-ChildItem -LiteralPath $output -Recurse -File | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($output, $_.FullName); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'manifest.json') -Encoding utf8
Write-Output "Package: $output"

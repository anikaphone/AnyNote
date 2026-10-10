param(
    [string]$BuildDirectory = 'build',
    [switch]$Scale,
    [switch]$ExistingRegression
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$build = [IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory))
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('AnyNote-sync-p0-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
Write-Output "Evidence and isolated test data: $scratch"
$probe = Join-Path $build 'p0/p0probe.exe'
if (!(Test-Path -LiteralPath $probe)) { throw 'Build src/AnyNoteServer/cmd/p0probe first.' }
$tlsDir = Join-Path $scratch 'tls'
$process = Start-Process -FilePath $probe -ArgumentList @('-dir', ('"{0}"' -f $tlsDir)) -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $scratch 'probe-out.log') -RedirectStandardError (Join-Path $scratch 'probe-error.log')
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath (Join-Path $tlsDir 'ready.json'))) {
        if ($process.HasExited) { throw "TLS probe exited: $($process.ExitCode)" }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'TLS fixture startup timeout' }
        Start-Sleep -Milliseconds 200
    }
    foreach ($config in @('Debug', 'Release')) {
        $bin = Join-Path $build $config
        & (Join-Path $bin 'test_sync_protocol.exe') (Join-Path $repo 'tests/fixtures/sync-v1') 2>&1 |
            Tee-Object -FilePath (Join-Path $scratch "$config-protocol.log")
        if ($LASTEXITCODE -ne 0) { throw "$config protocol failed" }
        & (Join-Path $bin 'test_sync_storage.exe') (Join-Path $scratch "$config-storage") 2>&1 |
            Tee-Object -FilePath (Join-Path $scratch "$config-storage.log")
        if ($LASTEXITCODE -ne 0) { throw "$config storage failed" }
        & (Join-Path $bin 'test_sync_tls.exe') $tlsDir 2>&1 |
            Tee-Object -FilePath (Join-Path $scratch "$config-tls.log")
        if ($LASTEXITCODE -ne 0) { throw "$config TLS failed" }
        foreach ($test in @('test_sync_migration','test_sync_config','test_sync_snapshot')) {
            & (Join-Path $bin "$test.exe") (Join-Path $scratch "$config-$test") 2>&1 |
                Tee-Object -FilePath (Join-Path $scratch "$config-$test.log")
            if ($LASTEXITCODE -ne 0) { throw "$config/$test failed" }
        }
        & (Join-Path $bin 'test_sync_client.exe') $tlsDir 2>&1 |
            Tee-Object -FilePath (Join-Path $scratch "$config-client.log")
        if ($LASTEXITCODE -ne 0) { throw "$config client/server integration failed" }
        & (Join-Path $bin 'test_sync_e2e.exe') $tlsDir 2>&1 |
            Tee-Object -FilePath (Join-Path $scratch "$config-e2e.log")
        if ($LASTEXITCODE -ne 0) { throw "$config two-client E2E failed" }
        if ($Scale) {
            & (Join-Path $bin 'test_sync_protocol.exe') --scale 2>&1 |
                Tee-Object -FilePath (Join-Path $scratch "$config-scale.log")
            if ($LASTEXITCODE -ne 0) { throw "$config scale failed" }
            if ($config -eq 'Release') {
                & (Join-Path $bin 'test_sync_client.exe') $tlsDir --scale 2>&1 |
                    Tee-Object -FilePath (Join-Path $scratch "$config-network-scale.log")
                if ($LASTEXITCODE -ne 0) { throw 'Network scale/cancellation failed' }
            }
        }
        if ($ExistingRegression) {
            $regression = Join-Path $scratch "$config-regression"
            New-Item -ItemType Directory -Path $regression | Out-Null
            Push-Location $regression
            try {
                foreach ($test in @('test_storage','test_codeblock_hover','test_table_editing','test_node_search','test_outline')) {
                    & (Join-Path $bin "$test.exe") 2>&1 | Tee-Object -FilePath (Join-Path $scratch "$config-$test.log")
                    if ($LASTEXITCODE -ne 0) { throw "$config/$test failed" }
                }
            } finally { Pop-Location }
        }
    }
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
Write-Output "Sync protocol, storage, client/server and TLS checks passed. Evidence: $scratch"

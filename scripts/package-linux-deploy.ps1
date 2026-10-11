param(
    [string]$VpsHost = '',
    [int]$Port = 0
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "      AnyNote Linux VPS 一键打包向导      " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 1. 交互式获取 VPS 地址与端口
while ([string]::IsNullOrWhiteSpace($VpsHost)) {
    $VpsHost = Read-Host "请输入目标 VPS 的 IP 地址或域名 (例如: 123.45.67.89 或 sync.yourdomain.com)"
}
$VpsHost = $VpsHost.Trim()

if ($Port -le 0 -or $Port -gt 65535) {
    $portInput = Read-Host "请输入服务监听端口 [回车默认: 8443]"
    if ([string]::IsNullOrWhiteSpace($portInput)) {
        $Port = 8443
    } else {
        $Port = [int]$portInput
    }
}

Write-Host "`n[+] 目标地址: $VpsHost" -ForegroundColor Green
Write-Host "[+] 服务端口: $Port" -ForegroundColor Green

# 2. 检查并编译 Linux 二进制
Write-Host "`n[+] 1. 正在检查并交叉编译 Linux 服务端程序..." -ForegroundColor Yellow
& "$PSScriptRoot\build-server.ps1" -Linux
if ($LASTEXITCODE -ne 0) { throw "Linux 交叉编译失败" }

# 检查本地 Windows 版 anynote-cert.exe (用于生成证书)
$certGenTool = Join-Path $repo "build\bin\anynote-cert.exe"
if (!(Test-Path -LiteralPath $certGenTool)) {
    Write-Host "[+] 正在编译本地证书生成工具..." -ForegroundColor Yellow
    & "$PSScriptRoot\build-server.ps1"
}

# 3. 准备临时打包目录
$stagingDir = Join-Path $repo "build\temp\linux-deploy-staging"
if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Force (Join-Path $stagingDir "certs") | Out-Null

# 4. 生成 TLS 自签证书 (注入 VPS 的 IP/域名)
Write-Host "[+] 2. 正在为 $VpsHost 专属生成 TLS 证书与私钥..." -ForegroundColor Yellow
$certHosts = "$VpsHost,127.0.0.1,localhost"
& $certGenTool -dir (Join-Path $stagingDir "certs") -hosts $certHosts
if ($LASTEXITCODE -ne 0) { throw "证书生成失败" }

# 5. 生成 server.json 配置文件
Write-Host "[+] 3. 正在生成服务端配置文件 server.json..." -ForegroundColor Yellow
$serverConfig = @{
    listen = "0.0.0.0:$Port"
    data_dir = "data"
    cert_file = "certs/server.crt"
    key_file = "certs/server.key"
    registration_enabled = $true
    account_vaults = 100
    account_bytes = 1073741824
} | ConvertTo-Json
Set-Content -Path (Join-Path $stagingDir "server.json") -Value $serverConfig -Encoding utf8

# 6. 复制 Linux 二进制程序
Write-Host "[+] 4. 正在装配 Linux 二进制与部署脚本..." -ForegroundColor Yellow
$linuxBinDir = Join-Path $repo "build\bin\server\linux-amd64"
foreach ($bin in @("anynote-server", "anynote-admin", "anynote-cert")) {
    Copy-Item -LiteralPath (Join-Path $linuxBinDir $bin) -Destination (Join-Path $stagingDir $bin)
}

# 复制部署脚本并确保 LF 换行符
$deployScriptSrc = Join-Path $repo "scripts\deploy.sh"
$deployScriptDst = Join-Path $stagingDir "deploy.sh"
$deployContent = [System.IO.File]::ReadAllText($deployScriptSrc)
[System.IO.File]::WriteAllText($deployScriptDst, $deployContent.Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))

# 7. 打包为 tar.gz
$packageOut = Join-Path $repo "build\bin\anynote-linux-deploy.tar.gz"
if (Test-Path -LiteralPath $packageOut) {
    Remove-Item -Force $packageOut
}
Write-Host "[+] 5. 正在压缩生成安装包 anynote-linux-deploy.tar.gz..." -ForegroundColor Yellow
tar -czf $packageOut -C $stagingDir .
if ($LASTEXITCODE -ne 0) { throw "打包失败" }

# 8. 留存一份客户端所需的 server.crt 到 build/bin
$clientCertDir = Join-Path $repo "build\bin\client-certs"
New-Item -ItemType Directory -Force $clientCertDir | Out-Null
$clientCertFile = Join-Path $clientCertDir "$VpsHost-server.crt"
Copy-Item -LiteralPath (Join-Path $stagingDir "certs\server.crt") -Destination $clientCertFile -Force
Copy-Item -LiteralPath (Join-Path $stagingDir "certs\server.crt") -Destination (Join-Path $repo "build\bin\server.crt") -Force

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "                打包已顺利完成！                        " -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
Write-Host "【1. 上传至 VPS 的部署包】:" -ForegroundColor Cyan
Write-Host "  -> $packageOut`n" -ForegroundColor White
Write-Host "【2. 本机客户端连接所需证书 (已自动留存)】:" -ForegroundColor Cyan
Write-Host "  -> $clientCertFile`n" -ForegroundColor White
Write-Host "【后续只需两步】:" -ForegroundColor Yellow
Write-Host "第 1 步: 用 SFTP 工具将 [anynote-linux-deploy.tar.gz] 上传到你的 VPS (例如上传到 /root/)"
Write-Host "第 2 步: 在 VPS 终端上执行以下单行命令即可全自动一键部署:"
Write-Host "   mkdir -p /tmp/anynote-deploy && tar -zxvf anynote-linux-deploy.tar.gz -C /tmp/anynote-deploy && cd /tmp/anynote-deploy && sudo bash deploy.sh" -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green

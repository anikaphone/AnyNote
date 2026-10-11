#!/usr/bin/env bash
set -e

# AnyNote Linux VPS 一键部署脚本

echo "=========================================="
echo "    AnyNote 服务端一键部署向导            "
echo "=========================================="

# 1. 检查 root 权限
if [ "$(id -u)" -ne 0 ]; then
    echo "[-] 错误: 请使用 root 用户或 sudo 执行此脚本: sudo bash deploy.sh"
    exit 1
fi

INSTALL_DIR="/opt/anynote"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[+] 1. 创建专用系统账号 (anynote)..."
if ! id -u anynote >/dev/null 2>&1; then
    useradd -r -s /bin/false -U anynote
    echo "[✓] 已创建 anynote 系统用户"
else
    echo "[✓] anynote 用户已存在，跳过"
fi

echo "[+] 2. 创建安装目录并设置安全权限..."
mkdir -p "${INSTALL_DIR}/data" "${INSTALL_DIR}/certs" "${INSTALL_DIR}/backups"

# 检查当前运行的服务并安全停止
if systemctl is-active --quiet anynote; then
    echo "[!] 发现正在运行的 anynote 服务，正在停止以便更新..."
    systemctl stop anynote
fi

echo "[+] 3. 安装二进制程序与配置文件..."
cp "${SCRIPT_DIR}/anynote-server" "${INSTALL_DIR}/"
cp "${SCRIPT_DIR}/anynote-admin" "${INSTALL_DIR}/"
cp "${SCRIPT_DIR}/anynote-cert" "${INSTALL_DIR}/"
chmod +x "${INSTALL_DIR}/anynote-"*

# 证书与配置安装
if [ -f "${SCRIPT_DIR}/server.json" ]; then
    cp "${SCRIPT_DIR}/server.json" "${INSTALL_DIR}/server.json"
fi

if [ -f "${SCRIPT_DIR}/certs/server.crt" ]; then
    cp "${SCRIPT_DIR}/certs/server.crt" "${INSTALL_DIR}/certs/server.crt"
fi

if [ -f "${SCRIPT_DIR}/certs/server.key" ]; then
    cp "${SCRIPT_DIR}/certs/server.key" "${INSTALL_DIR}/certs/server.key"
    chmod 600 "${INSTALL_DIR}/certs/server.key"
fi

# 设置目录所有权与权限
chown -R anynote:anynote "${INSTALL_DIR}"
chmod 700 "${INSTALL_DIR}/data" "${INSTALL_DIR}/certs" "${INSTALL_DIR}/backups"

# 读取监听端口
PORT=8443
if [ -f "${INSTALL_DIR}/server.json" ]; then
    DETECTED_PORT=$(grep -o '"listen": *"[^"]*"' "${INSTALL_DIR}/server.json" | awk -F: '{print $NF}' | tr -d '"')
    if [ -n "${DETECTED_PORT}" ]; then
        PORT="${DETECTED_PORT}"
    fi
fi

echo "[+] 4. 注册并配置 systemd 系统守护进程..."
cat <<EOF > /etc/systemd/system/anynote.service
[Unit]
Description=AnyNote Sync Server
After=network.target

[Service]
Type=simple
User=anynote
Group=anynote
WorkingDirectory=${INSTALL_DIR}
ExecStart=${INSTALL_DIR}/anynote-server --config server.json
Restart=on-failure
RestartSec=5s
LimitNOFILE=65536

# 安全沙盒隔离
ProtectSystem=full
ProtectHome=true
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now anynote

echo "[+] 5. 正在配置系统防火墙端口 (${PORT})..."
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q "Status: active"; then
    ufw allow "${PORT}/tcp" >/dev/null 2>&1 || true
    echo "[✓] 已通过 ufw 开放端口 ${PORT}"
elif command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld; then
    firewall-cmd --add-port="${PORT}/tcp" --permanent >/dev/null 2>&1 || true
    firewall-cmd --reload >/dev/null 2>&1 || true
    echo "[✓] 已通过 firewalld 开放端口 ${PORT}"
else
    echo "[!] 未检测到启用的 ufw 或 firewalld。如果云服务器有安全组策略，请确保在云控制台放行 TCP ${PORT} 端口。"
fi

echo "[+] 6. 检查服务运行状态..."
sleep 2

if systemctl is-active --quiet anynote; then
    echo "=========================================="
    echo "       AnyNote 服务端已成功启动运行！     "
    echo "=========================================="
    echo "服务监听: https://0.0.0.0:${PORT}"
    echo "安装目录: ${INSTALL_DIR}"
    echo "查看状态: systemctl status anynote"
    echo "查看日志: journalctl -u anynote -f"
    echo "------------------------------------------"
    echo "【提示】"
    echo "1. 默认已开启注册功能 (registration_enabled: true)。"
    echo "2. 在客户端注册完账号后，建议执行以下命令关闭开放注册："
    echo "   sudo sed -i 's/\"registration_enabled\": true/\"registration_enabled\": false/' /opt/anynote/server.json && sudo systemctl restart anynote"
    echo "=========================================="
else
    echo "[-] 服务启动异常，正在显示最近错误日志："
    journalctl -u anynote -n 20 --no-pager
    exit 1
fi

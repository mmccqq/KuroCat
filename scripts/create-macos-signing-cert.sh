#!/usr/bin/env bash
# Creates Whisku's stable signing certificate once (self-signed, free, no Apple developer account).
# 一次性创建 Whisku 的「固定签名证书」（自签名，免费，不需要苹果开发者账号）。
#
# 为什么需要：macOS 按「代码签名」记住输入监控 / 辅助功能权限。之前 Mac 版用临时（ad-hoc）签名，
# 每编译一次签名就变，系统把每个新版本当成新 App —— 用户每次更新都要重新开一次「输入监控」，
# 系统设置里还会残留一个旧的 KuroCat 开关。用同一张证书签名后，更新后权限会一直保留。
#
# 用法（在 Mac 上执行一次即可）：
#   bash scripts/create-macos-signing-cert.sh [备份目录，默认 ~/Whisku-signing]
#
# 执行后：
#   - 证书导入「登录」钥匙串，本机 ./build.sh 会自动使用它签名
#   - 备份目录里有 whisku-signing.p12 和密码：务必备份！丢了就只能换新证书，
#     用户更新到换证书后的版本时还得再授权一次
#   - 如果用 GitHub Actions 打包，把脚本最后打印的两项填进仓库 Secrets
set -euo pipefail

NAME="Whisku Code Signing"
OUT_DIR="${1:-$HOME/Whisku-signing}"
P12="$OUT_DIR/whisku-signing.p12"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"
OPENSSL=/usr/bin/openssl   # macOS 自带的 LibreSSL，导出的 .p12 能被 security 直接导入

if [ "$(uname -s)" != "Darwin" ]; then
    echo "请在 Mac 上运行这个脚本。" >&2
    exit 1
fi

if security find-identity -p codesigning 2>/dev/null | grep -q "\"$NAME\""; then
    echo "钥匙串里已经有「${NAME}」证书，不需要重新创建。"
    echo "注意：不要删除或重新生成它，否则用户更新后又要重新授权一次。"
    exit 0
fi

mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/cert.cnf" <<EOF
[ req ]
distinguished_name = dn
x509_extensions = ext
prompt = no
[ dn ]
CN = $NAME
[ ext ]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
EOF

"$OPENSSL" req -x509 -newkey rsa:2048 -nodes -days 7300 \
    -config "$WORK/cert.cnf" -keyout "$WORK/key.pem" -out "$WORK/cert.pem"

P12_PASSWORD="$("$OPENSSL" rand -hex 16)"
LEGACY=""
if "$OPENSSL" version | grep -q "^OpenSSL 3"; then LEGACY="-legacy"; fi
"$OPENSSL" pkcs12 -export $LEGACY -inkey "$WORK/key.pem" -in "$WORK/cert.pem" \
    -name "$NAME" -out "$P12" -passout "pass:$P12_PASSWORD"

security import "$P12" -k "$KEYCHAIN" -P "$P12_PASSWORD" -T /usr/bin/codesign

printf '%s\n' "$P12_PASSWORD" > "$OUT_DIR/p12-password.txt"
base64 < "$P12" > "$P12.base64.txt"
chmod 600 "$OUT_DIR"/*

echo ""
echo "✅ 已创建并导入「${NAME}」证书。现在运行 ./build.sh 就会自动用它签名。"
echo "   第一次签名时如果弹出「codesign 想要使用钥匙串中的密钥」，请点「始终允许」。"
echo ""
echo "📦 备份文件（请妥善保存，不要上传到公开仓库）：$OUT_DIR"
echo ""
echo "🔧 如果用 GitHub Actions 打包 Mac 版，在仓库 Settings → Secrets and variables → Actions 新建："
echo "   MACOS_SIGN_P12_BASE64   = $P12.base64.txt 的全部内容"
echo "   MACOS_SIGN_P12_PASSWORD = $OUT_DIR/p12-password.txt 的内容"

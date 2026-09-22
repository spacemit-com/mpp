#!/usr/bin/env bash
# 下载测试素材到 test/assets/。
#
# 素材不再随仓库分发，统一托管在 Nexus：
#   https://nexus.bianbu.xyz/#browse/browse:video:mpp
# 直链格式：${BASE_URL}/<文件名>
#
# 用法：
#   bash test/download_assets.sh           # 下载缺失或校验失败的文件
#   bash test/download_assets.sh --force   # 忽略本地缓存，全部重新下载
#
# 可用环境变量覆盖：
#   MPP_ASSET_BASE_URL  素材仓库地址（默认下方 DEFAULT_BASE_URL）
#   DOWNLOADER          curl 或 wget（默认自动探测）

set -euo pipefail

DEFAULT_BASE_URL="https://nexus.bianbu.xyz/repository/video/mpp"
BASE_URL="${MPP_ASSET_BASE_URL:-$DEFAULT_BASE_URL}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="${SCRIPT_DIR}/assets"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

# 文件名 sha1（与 Nexus 上 mpp/ 目录一致）
FILES=(
    "1920x1080.jpg|df5a275868acd2f2b7c444cfbf954ac52c4ebf70"
    "input.264|2a3ff5f6635af5e8bf22fb7b936a7b0c670f3c8f"
    "input.265|7ee5df378ec55dfce76db9932765127ef6ab6104"
    "input.mjpeg|6505366c690f52d2664eb2865d900cc8ae9cddca"
    "test_video.mp4|0dd6717677877b9f31859bbd547d6f284aeb2a07"
    "test_video.ts|e0000c69292814251d259d58f901d82446a724cf"
    "vi_phy0_last_frame.yuv|dc839abee063f83e4e3706839b92d8ffeb6309af"
)

sha1_of() {
    if command -v sha1sum >/dev/null 2>&1; then
        sha1sum "$1" | awk '{print $1}'
    else
        shasum "$1" | awk '{print $1}'
    fi
}

# 下载到临时文件再原子替换，避免中断留下半截文件。
# nexus.bianbu.xyz 为国内直连源，若走了全局代理导致 TLS 握手失败，
# 自动绕过代理重试一次。
fetch() { # fetch <url> <dest.tmp>
    local url="$1" dest="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 3 --connect-timeout 10 -o "$dest" "$url" \
            || curl -fsSL --retry 3 --connect-timeout 10 --noproxy '*' -o "$dest" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --tries=3 -O "$dest" "$url"
    else
        echo "错误：需要 curl 或 wget" >&2
        exit 1
    fi
}

mkdir -p "$ASSET_DIR"

fail=0
for entry in "${FILES[@]}"; do
    name="${entry%%|*}"
    want_sha1="${entry##*|}"
    dest="${ASSET_DIR}/${name}"

    if [ "$FORCE" -eq 0 ] && [ -f "$dest" ] && [ "$(sha1_of "$dest")" = "$want_sha1" ]; then
        echo "已存在  ${name}"
        continue
    fi

    echo "下载    ${BASE_URL}/${name}"
    tmp="${dest}.tmp"
    if ! fetch "${BASE_URL}/${name}" "$tmp"; then
        echo "失败    ${name}（下载出错）" >&2
        rm -f "$tmp"
        fail=1
        continue
    fi

    got_sha1="$(sha1_of "$tmp")"
    if [ "$got_sha1" != "$want_sha1" ]; then
        echo "失败    ${name}（sha1 不匹配：期望 ${want_sha1}，实际 ${got_sha1}）" >&2
        rm -f "$tmp"
        fail=1
        continue
    fi
    mv -f "$tmp" "$dest"
    echo "完成    ${name}"
done

if [ "$fail" -ne 0 ]; then
    echo "部分素材下载失败，请检查网络后重试。" >&2
    exit 1
fi
echo "全部素材就绪：${ASSET_DIR}"

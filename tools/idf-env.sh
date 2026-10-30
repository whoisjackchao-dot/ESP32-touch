#!/usr/bin/env bash
# 用法：source tools/idf-env.sh
# 说明：本机无 sudo、系统 python 为 3.14；ESP-IDF v5.5.4 用用户态 python3.13 venv。
#       openocd 需要 libusb，已解包到 ~/.local/share/esp-libs（无系统安装）。
set -u

export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$IDF_TOOLS_PATH/python_env/idf5.5_py3.13_env}"

_libusb="$HOME/.local/share/esp-libs/usr/lib/x86_64-linux-gnu"
if [ -d "$_libusb" ]; then
    export LD_LIBRARY_PATH="$_libusb${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ERROR: ESP-IDF not found at $IDF_PATH" >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null 2>&1 || {
    echo "ERROR: export.sh failed; run: . $IDF_PATH/export.sh" >&2
    return 1 2>/dev/null || exit 1
}

echo "idf-env: IDF_PATH=$IDF_PATH"
echo "idf-env: python_env=$IDF_PYTHON_ENV_PATH"
echo "idf-env: build dir must be on WSL native disk (e.g. ~/esp-build/buddy154)"

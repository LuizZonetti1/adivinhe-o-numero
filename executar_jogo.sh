#!/usr/bin/env bash
# Wrapper para executar o .bat a partir do bash (Git Bash/MSYS2)
set -euo pipefail
cd "$(dirname "$0")"
exec cmd.exe /c executar_jogo.bat

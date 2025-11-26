@echo off
setlocal
REM Compila o jogo de adivinhação de número

REM Garante que o diretório atual seja o da pasta do script
pushd "%~dp0"

REM Verifica/garante GCC (MinGW) no PATH
call :ensure_gcc
if errorlevel 1 goto :no_gcc

gcc adivinhacao_servidor.c -o adivinhacao_servidor.exe -lws2_32
if errorlevel 1 goto :erro_compilar_servidor

gcc adivinhacao_cliente.c -o adivinhacao_cliente.exe -lws2_32
if errorlevel 1 goto :erro_compilar_cliente

echo Compilacao concluida!
echo Para jogar, abriremos TRES terminais:
echo 1. Servidor (porta 12345)
start "Servidor (porta 12345)" "%cd%\adivinhacao_servidor.exe"
timeout /t 1 >nul
echo 2. Cliente P1 (conecta em 127.0.0.1:12345)
start "Cliente P1" "%cd%\adivinhacao_cliente.exe"
timeout /t 1 >nul
echo 3. Cliente P2 (conecta em 127.0.0.1:12345)
start "Cliente P2" "%cd%\adivinhacao_cliente.exe"
pause
goto :eof

:no_gcc
echo [ERRO] Nao foi encontrado o compilador GCC acessivel neste ambiente.
echo Abra um novo Prompt de Comando e rode:  where gcc
echo Se funcionar no seu CMD, feche e reabra este editor/terminal para atualizar as variaveis de ambiente.
echo Alternativamente, instale e/ou adicione ao PATH um destes caminhos comuns:
echo   C:\msys64\ucrt64\bin  ou  C:\msys64\mingw64\bin  ou  C:\MinGW\bin
echo Dica: voce tambem pode editar este .bat e setar manualmente um PATH para o GCC.
pause
goto :eof

:erro_compilar_servidor
echo Erro ao compilar adivinhacao_servidor.c
pause
goto :eof

:erro_compilar_cliente
echo Erro ao compilar adivinhacao_cliente.c
pause
goto :eof

:ensure_gcc
REM 1) Tenta gcc no PATH atual
gcc --version >nul 2>&1
if not errorlevel 1 (
	exit /b 0
)

where gcc >nul 2>&1
if not errorlevel 1 (
	exit /b 0
)

REM 2) Tenta adicionar caminhos comuns e revalidar
for %%D in ("C:\\msys64\\ucrt64\\bin" "C:\\msys64\\mingw64\\bin" "C:\\msys64\\mingw32\\bin" "C:\\MinGW\\bin" "C:\\Program Files\\mingw-w64\\mingw64\\bin" "C:\\Program Files (x86)\\mingw-w64\\mingw64\\bin") do (
	if exist "%%~D\gcc.exe" (
		set "PATH=%%~D;%PATH%"
		gcc --version >nul 2>&1
		if not errorlevel 1 (
			exit /b 0
		)
	)
)

REM 3) Falhou
exit /b 1

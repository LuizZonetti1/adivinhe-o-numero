# Jogo de Adivinhação (2 Jogadores, Cliente/Servidor)

Servidor e clientes em C (Winsock) para um jogo competitivo de adivinhação. Dois jogadores se conectam, cada um escolhe um número secreto (1–100) guardado apenas no servidor. Depois que ambos definem, os palpites são liberados em paralelo e o servidor faz mutações no número do adversário quando alguém erra muito.

## Arquivos
- `adivinhacao_servidor.c` — servidor TCP para dois clientes; controla estado e regras.
- `adivinhacao_cliente.c` — cliente de terminal para jogar.
- `executar_jogo.bat` — compila (MinGW/MSYS2) e abre servidor + 2 clientes em janelas separadas.
- `executar_jogo.sh` — esqueleto para ambientes POSIX (código é dependente de Winsock/Windows).

## Pré-requisitos
- Windows 10/11
- GCC (MinGW-w64 ou MSYS2) no PATH — teste com `where gcc`
- Winsock já incluído no Windows (linke com `-lws2_32`)

## Como compilar e executar
Automático (Windows):
1. Dê duplo clique em `executar_jogo.bat` (ou rode no Prompt).
2. O script compila e abre 3 terminais: servidor, cliente P1 e cliente P2.

Manual:
```bat
gcc adivinhacao_servidor.c -o adivinhacao_servidor.exe -lws2_32
gcc adivinhacao_cliente.c -o adivinhacao_cliente.exe -lws2_32

adivinhacao_servidor.exe
adivinhacao_cliente.exe   REM P1
adivinhacao_cliente.exe   REM P2
```

## Regras do jogo
- Cada jogador define um número secreto (1–100); fica só no servidor.
- Após ambos definirem, palpites ficam liberados sem turno. Feedback imediato: `MAIOR`, `MENOR` ou `ACERTOU`; o oponente é avisado do palpite.
- A cada **3 erros** do jogador que está palpitando, o servidor tenta mutar o número do oponente com uma das operações: `MULTIPLICADO_POR_2`, `SOMADO_7`, `DIVIDIDO_POR_2`, `SUBTRAIDO_5`. Só são escolhidas operações cujo resultado permanece em 1–100; se nenhuma couber, não há mutação.
- Quem tem o número alterado recebe `NUMERO_ATUALIZADO <novo> <OPERACAO>`; quem errou recebe `NUMERO_DO_OPONENTE_MUDOU <OPERACAO>`.
- Vence quem acertar primeiro o número adversário. `sair` encerra seu cliente e notifica o oponente.

## Protocolo (resumo)
- `BEM_VINDO P{1|2}` — identificação.
- `DEFINA_SECRETO <min> <max>` — envie um inteiro válido.
- `LIBERADO_PALPITES` + `VOCES_PODEM_ADIVINHAR_A_QUALQUER_MOMENTO` — palpites liberados (`PALPITE <valor>` ou só `<valor>`).
- `PALPITE_RESULTADO <MAIOR|MENOR|ACERTOU|AGUARDE|PARTIDA_ENCERRADA|ENTRADA_INVALIDA>` — retorno local.
- `OPONENTE_TENTOU <valor> <tag>` — palpite recebido do adversário.
- `NUMERO_ATUALIZADO <novo_valor> <OPERACAO>` / `NUMERO_DO_OPONENTE_MUDOU <OPERACAO>` — mutações.
- `FIM_PARTIDA VENCEU|PERDEU`, `OPONENTE_DESCONECTOU`, `ENCERRAR` — encerramentos.

## Notas de implementação
- Servidor: 2 threads (uma por jogador) via `_beginthreadex`, estado compartilhado protegido por `CRITICAL_SECTION`.
- Cliente: thread separado para leitura do teclado; alterna modos `INPUT_SECRET` e `INPUT_GUESS`.
- Intervalo padrão (1–100) pode ser alterado nas `#define` de servidor/cliente.
- Dependência única de rede: `-lws2_32`.

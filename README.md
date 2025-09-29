# Jogo de Adivinhação (2 Jogadores, Cliente/Servidor)

Este projeto implementa um jogo de adivinhação competitivo para dois jogadores via TCP (Winsock) no Windows.

## Arquivos
- `adivinhacao_servidor.c` — servidor TCP que coordena 2 clientes; cada jogador escolhe seu número secreto (1–100).
- `adivinhacao_cliente.c` — cliente de terminal; define seu número secreto, realiza palpites e recebe feedback.
- `executar_jogo.bat` — script para compilar (via gcc/MinGW) e abrir servidor e cliente automaticamente.

## Pré-requisitos
- Windows 10/11
- Compilador C (GCC do MinGW-w64 ou MSYS2) no PATH
  - Verifique com: `where gcc` (no Prompt de Comando)
- Biblioteca Winsock já faz parte do Windows (linkada com `-lws2_32`).

## Como compilar e executar
Você pode usar o script automaticamente (agora abre 3 janelas: 1 servidor + 2 clientes):

1. Clique duas vezes em `executar_jogo.bat` (ou execute no Prompt de Comando).
2. O script compila os binários e abre três terminais: servidor, cliente P1 e cliente P2.

Caso queira compilar manualmente (no Prompt de Comando com gcc no PATH):

```
gcc adivinhacao_servidor.c -o adivinhacao_servidor.exe -lws2_32
gcc adivinhacao_cliente.c -o adivinhacao_cliente.exe -lws2_32
```

Depois, rode em três terminais:

```
adivinhacao_servidor.exe
adivinhacao_cliente.exe   # P1
adivinhacao_cliente.exe   # P2
```

## Regras (2 jogadores)
- Ambos os jogadores definem um número secreto (1–100) no início da partida.
- As jogadas são alternadas: P1 tenta adivinhar o número de P2, depois P2 tenta adivinhar o número de P1.
- O servidor informa MAIOR/MENOR/ACERTOU para quem chutou e notifica o oponente sobre o palpite recebido.
- Vence quem acertar primeiro o número do oponente.
- Após o fim, o servidor pergunta aos dois se desejam jogar novamente; a partida só reinicia se ambos responderem `1`.

## Protocolo (mensagens principais)
- `BEM_VINDO P{1|2}` — identificação do jogador.
- `DEFINA_SECRETO 1 100` — o cliente deve responder com um inteiro (1–100).
- `SUA_VEZ` — o cliente deve enviar seu palpite (inteiro, 1–100).
- `VEZ_DO_OPONENTE` — aguarde a jogada do outro.
- `RESULTADO MAIOR|MENOR|ACERTOU|ENTRADA_INVALIDA` — retorno da sua jogada.
- `OPONENTE_CHUTOU <n> <MAIOR|MENOR|ACERTOU>` — notificação informativa.
- `OPONENTE_ACERTOU` — o oponente acertou seu número.
- `JOGAR_NOVAMENTE? 1-Sim 2-Nao` — ambos respondem com `1` ou `2`.

## Observações
- O servidor aceita uma conexão por vez (fila 1).
- Ambos usam buffers de 128 bytes e chamadas `send/recv` de tamanho fixo.
- Se desejar mudar a porta ou o IP do servidor, altere as `#define` nos arquivos `.c`.

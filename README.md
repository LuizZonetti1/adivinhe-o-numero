# Jogo de Adivinhação (2 Jogadores, Cliente/Servidor)

Este projeto implementa um jogo de adivinhação competitivo para dois jogadores via TCP (Winsock) no Windows. O servidor mantém os números secretos em memória compartilhada entre duas threads — uma por jogador — permitindo tentativas simultâneas e regras dinâmicas.

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
- Ambos os jogadores definem um número secreto no início (intervalo padrão 1–100). Os valores são armazenados somente no servidor.
- Após os dois confirmarem seus números, o servidor libera os palpites: cada jogador pode tentar descobrir o número do oponente a qualquer momento, sem depender de "vez".
- Cada tentativa recebe um feedback imediato: `MAIOR`, `MENOR` ou `ACERTOU`. O oponente também é notificado do palpite recebido.
- A cada 3 palpites errados de um jogador, o servidor aplica automaticamente uma operação matemática (somar, multiplicar, dividir ou subtrair) sobre o número secreto **do oponente**. O dono do número recebe o novo valor, enquanto quem errou é avisado apenas de que o alvo mudou.
- Vence quem descobrir primeiro o número do adversário. Após o encerramento, a sessão é finalizada (para uma nova partida basta executar novamente servidor e clientes).
- Durante o jogo, o cliente pode digitar `sair` para abandonar a partida; o oponente será notificado.

## Protocolo (mensagens principais)
- `BEM_VINDO P{1|2}` — identificação do jogador.
- `DEFINA_SECRETO <min> <max>` — o cliente deve enviar um inteiro dentro do intervalo informado.
- `SECRETO_REGISTRADO` / `AGUARDE_OPONENTE_DEFINIR` — confirmações do servidor.
- `LIBERADO_PALPITES` + `VOCES_PODEM_ADIVINHAR_A_QUALQUER_MOMENTO` — habilita tentativas paralelas; o cliente passa a enviar comandos `PALPITE <valor>` quando desejar.
- `PALPITE_RESULTADO <MAIOR|MENOR|ACERTOU|AGUARDE|PARTIDA_ENCERRADA|ENTRADA_INVALIDA>` — retorno imediato da tentativa local.
- `OPONENTE_TENTOU <valor> <tag>` — informa o palpite recebido do adversário.
- `NUMERO_ATUALIZADO <novo_valor> <OPERACAO>` — enviado ao jogador cujo número foi modificado após 3 erros consecutivos.
- `NUMERO_DO_OPONENTE_MUDOU <OPERACAO>` — alerta o outro jogador sobre a mudança (sem revelar o valor).
- `FIM_PARTIDA VENCEU|PERDEU` e `ENCERRAR` — encerramento normal da sessão.
- `OPONENTE_DESCONECTOU` — o outro cliente abandonou a partida.

## Observações
- O servidor mantém duas threads (Winsock + `_beginthreadex`), compartilhando o estado da partida com `CRITICAL_SECTION`.
- Os clientes executam um thread dedicado para entrada do usuário; enquanto o modo "palpites" estiver ativo, basta digitar novos números (ou `sair`).
- O intervalo padrão permanece 1–100, mas pode ser alterado nas `#define` de servidor/cliente.
- Cada compilação continua dependendo apenas de Winsock (`-lws2_32`).

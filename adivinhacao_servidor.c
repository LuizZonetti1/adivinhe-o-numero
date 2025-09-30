// Servidor do jogo de adivinhação de número (2 jogadores)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA 12345

static int send_all(SOCKET s, const char* buf, int len) {
    const char* p = buf;
    while (len > 0) {
        int n = send(s, p, len, 0);
        if (n == SOCKET_ERROR) return SOCKET_ERROR;
        p += n;
        len -= n;
    }
    return 0;
}

// Envia uma linha terminada por '\n'
static int envia_ln(SOCKET s, const char* msg) {
    int len = (int)strlen(msg);
    if (len > 0) {
        if (send_all(s, msg, len) == SOCKET_ERROR) return SOCKET_ERROR;
    }
    char nl = '\n';
    if (send_all(s, &nl, 1) == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

// Recebe até '\n' (ignora '\r'). Retorna número de bytes (sem '\n') ou <=0 em erro/fechamento.
static int recebe_ln(SOCKET s, char* out, int outsz) {
    if (outsz <= 1) return -1;
    int pos = 0;
    for (;;) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r == 0) { // conexão fechada
            if (pos == 0) return 0;
            break;
        }
        if (r == SOCKET_ERROR) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c == '\r') continue;
        if (pos < outsz - 1) out[pos++] = c;
    }
    out[pos] = '\0';
    return pos;
}

int main(void) {
    WSADATA wsa;
    SOCKET servidor = INVALID_SOCKET, clientes[2] = {INVALID_SOCKET, INVALID_SOCKET};
    struct sockaddr_in addr_servidor, addr_cliente;
    int tam_cliente = sizeof(addr_cliente);
    char buffer[256];

    printf("Iniciando servidor de adivinhacao (2 jogadores)...\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }

    servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor == INVALID_SOCKET) {
        printf("Falha ao criar socket.\n");
        WSACleanup();
        return 1;
    }

    // Reuso rápido de porta (evita TIME_WAIT atrapalhar)
    BOOL opt = 1;
    setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    addr_servidor.sin_addr.s_addr = INADDR_ANY;
    addr_servidor.sin_port = htons(PORTA);

    if (bind(servidor, (struct sockaddr*)&addr_servidor, sizeof(addr_servidor)) == SOCKET_ERROR) {
        printf("Erro no bind. Porta %d pode estar em uso.\n", PORTA);
        closesocket(servidor);
        WSACleanup();
        return 1;
    }

    if (listen(servidor, 2) == SOCKET_ERROR) {
        printf("Erro no listen.\n");
        closesocket(servidor);
        WSACleanup();
        return 1;
    }

    printf("Aguardando 2 conexoes na porta %d...\n", PORTA);
    for (int i = 0; i < 2; i++) {
        tam_cliente = sizeof(addr_cliente);
        clientes[i] = accept(servidor, (struct sockaddr*)&addr_cliente, &tam_cliente);
        if (clientes[i] == INVALID_SOCKET) {
            printf("Erro no accept para jogador %d.\n", i+1);
            for (int j = 0; j < 2; j++) if (clientes[j] != INVALID_SOCKET) closesocket(clientes[j]);
            closesocket(servidor);
            WSACleanup();
            return 1;
        }
        snprintf(buffer, sizeof(buffer), "BEM_VINDO P%d", i+1);
        envia_ln(clientes[i], buffer);
    }

    int continuar = 1;
    while (continuar) {
        // Cada jogador define seu numero secreto
        int secreto[2] = {0, 0};
        for (int i = 0; i < 2; i++) {
            // Informa o outro jogador que deve aguardar
            int outro = 1 - i;
            if (i == 0) {
                // P2 aguarda P1 escolher
                envia_ln(clientes[outro], "AGUARDE_ADVERSARIO_ESCOLHER");
            } else {
                // P1 aguarda P2 escolher  
                envia_ln(clientes[outro], "AGUARDE_ADVERSARIO_ESCOLHER");
            }
            
            for (;;) {
                envia_ln(clientes[i], "DEFINA_SECRETO 1 100");
                int r = recebe_ln(clientes[i], buffer, sizeof(buffer));
                if (r <= 0) { continuar = 0; break; }
                int num = 0;
                if (sscanf(buffer, "%d", &num) == 1 && num >= 1 && num <= 100) {
                    secreto[i] = num;
                    break;
                } else {
                    envia_ln(clientes[i], "ENTRADA_INVALIDA");
                }
            }
            if (!continuar) break;
        }
        if (!continuar) break;

        // Laço de turnos
        int vez = 0; // 0 = P1 joga, 1 = P2 joga
        for (;;) {
            int atual = vez;
            int outro = 1 - vez;

            envia_ln(clientes[atual], "SUA_VEZ");
            envia_ln(clientes[outro], "VEZ_DO_OPONENTE");

            // Recebe palpite do jogador da vez
            int r = recebe_ln(clientes[atual], buffer, sizeof(buffer));
            if (r <= 0) { continuar = 0; break; }
            int palpite = 0;
            if (sscanf(buffer, "%d", &palpite) != 1) {
                envia_ln(clientes[atual], "RESULTADO ENTRADA_INVALIDA");
                continue; // repete mesma vez
            }

            const char* tag = NULL;
            if (palpite < secreto[outro]) tag = "MAIOR";
            else if (palpite > secreto[outro]) tag = "MENOR";
            else tag = "ACERTOU";

            // Envia resultado para quem jogou
            snprintf(buffer, sizeof(buffer), "RESULTADO %s", tag);
            envia_ln(clientes[atual], buffer);

            // Informa oponente do palpite e do resultado
            snprintf(buffer, sizeof(buffer), "OPONENTE_CHUTOU %d %s", palpite, tag);
            envia_ln(clientes[outro], buffer);

            if (strcmp(tag, "ACERTOU") == 0) {
                envia_ln(clientes[outro], "OPONENTE_ACERTOU");
                // Final da partida: informa quem venceu/perdeu
                envia_ln(clientes[atual], "FIM_PARTIDA VENCEU");
                envia_ln(clientes[outro], "FIM_PARTIDA PERDEU");
                break;
            } else {
                vez = outro; // alterna a vez
            }
        }
        if (!continuar) break;

        // Pergunta se querem jogar novamente (ambos)
        envia_ln(clientes[0], "JOGAR_NOVAMENTE? 1-Sim 2-Nao");
        envia_ln(clientes[1], "JOGAR_NOVAMENTE? 1-Sim 2-Nao");

        char respbuf1[32] = {0};
        char respbuf2[32] = {0};
        int r1 = recebe_ln(clientes[0], respbuf1, sizeof(respbuf1));
        int r2 = recebe_ln(clientes[1], respbuf2, sizeof(respbuf2));
        int resp1 = (r1 > 0 && respbuf1[0] == '1') ? 1 : 0;
        int resp2 = (r2 > 0 && respbuf2[0] == '1') ? 1 : 0;

        if (!(resp1 && resp2)) {
            // Notifica ambos para encerrar
            envia_ln(clientes[0], "ENCERRAR");
            envia_ln(clientes[1], "ENCERRAR");
            continuar = 0;
        }
    }

    for (int i = 0; i < 2; i++) if (clientes[i] != INVALID_SOCKET) closesocket(clientes[i]);
    closesocket(servidor);
    WSACleanup();
    return 0;
}

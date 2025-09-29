// Cliente do jogo de adivinhação (linha-por-linha)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345

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

// Envia linha terminada com '\n'
static int envia_ln(SOCKET s, const char* msg) {
    int len = (int)strlen(msg);
    if (len > 0) {
        if (send_all(s, msg, len) == SOCKET_ERROR) return SOCKET_ERROR;
    }
    char nl = '\n';
    if (send_all(s, &nl, 1) == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

// Recebe linha até '\n' (ignora '\r')
static int recebe_ln(SOCKET s, char* out, int outsz) {
    if (outsz <= 1) return -1;
    int pos = 0;
    for (;;) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r == 0) {
            if (pos == 0) return 0;
            break;
        }
        if (r == SOCKET_ERROR) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        if (pos < outsz - 1) out[pos++] = c;
    }
    out[pos] = '\0';
    return pos;
}

static void trim_crlf(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static void prompt_and_send_number(SOCKET sock, const char* pergunta, int minv, int maxv) {
    char linha[128];
    for (;;) {
        printf("%s (%d-%d): ", pergunta, minv, maxv);
        fflush(stdout);
        if (!fgets(linha, sizeof(linha), stdin)) {
            // EOF no console -> envia algo inválido e tenta continuar
            envia_ln(sock, "0");
            return;
        }
        trim_crlf(linha);
        // valida simples
        char* end = NULL;
        long val = strtol(linha, &end, 10);
        if (end == linha || *end != '\0') {
            printf("Entrada invalida. Digite um numero.\n");
            continue;
        }
        if (val < minv || val > maxv) {
            printf("Fora do intervalo.\n");
            continue;
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "%ld", val);
        envia_ln(sock, msg);
        return;
    }
}

static void prompt_and_send_choice(SOCKET sock, const char* pergunta) {
    char linha[16];
    for (;;) {
        printf("%s ", pergunta);
        fflush(stdout);
        if (!fgets(linha, sizeof(linha), stdin)) {
            envia_ln(sock, "2"); // se EOF, considera "Nao"
            return;
        }
        trim_crlf(linha);
        if (linha[0] == '1') { envia_ln(sock, "1"); return; }
        if (linha[0] == '2') { envia_ln(sock, "2"); return; }
        printf("Digite 1 para Sim ou 2 para Nao.\n");
    }
}

int main(void) {
    // stdout sem buffer para ver prompts imediatamente
    setvbuf(stdout, NULL, _IONBF, 0);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Falha ao criar socket.\n");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);
    serv.sin_addr.s_addr = inet_addr(SERVER_IP);

    printf("Conectando ao servidor %s:%d...\n", SERVER_IP, SERVER_PORT);
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
        printf("Nao foi possivel conectar.\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("Conectado. Aguarde instrucoes...\n");

    char linha[512];
    int jogando = 1;
    while (jogando) {
        int r = recebe_ln(sock, linha, sizeof(linha));
        if (r <= 0) {
            printf("Conexao encerrada pelo servidor.\n");
            break;
        }

        // Decodifica comandos
        if (strncmp(linha, "BEM_VINDO", 9) == 0) {
            printf("[SERVIDOR] %s\n", linha);
        } else if (strncmp(linha, "DEFINA_SECRETO", 14) == 0) {
            // formato: DEFINA_SECRETO min max
            int minv = 1, maxv = 100;
            sscanf(linha, "DEFINA_SECRETO %d %d", &minv, &maxv);
            prompt_and_send_number(sock, "Defina seu numero secreto", minv, maxv);
        } else if (strcmp(linha, "SUA_VEZ") == 0) {
            prompt_and_send_number(sock, "Seu palpite", 1, 100);
        } else if (strcmp(linha, "VEZ_DO_OPONENTE") == 0) {
            printf("Aguarde: vez do oponente...\n");
        } else if (strncmp(linha, "RESULTADO ", 10) == 0) {
            const char* res = linha + 10;
            if (strcmp(res, "MAIOR") == 0) printf("Resposta: o numero e maior.\n");
            else if (strcmp(res, "MENOR") == 0) printf("Resposta: o numero e menor.\n");
            else if (strcmp(res, "ACERTOU") == 0) printf("Voce ACERTOU!\n");
            else if (strcmp(res, "ENTRADA_INVALIDA") == 0) printf("Entrada invalida. Tente novamente.\n");
            else printf("RESULTADO: %s\n", res);
        } else if (strncmp(linha, "OPONENTE_CHUTOU ", 16) == 0) {
            int palpite = 0;
            char tag[32] = {0};
            sscanf(linha, "OPONENTE_CHUTOU %d %31s", &palpite, tag);
            printf("Oponente chutou %d (%s)\n", palpite, tag);
        } else if (strcmp(linha, "OPONENTE_ACERTOU") == 0) {
            printf("Oponente acertou o seu numero.\n");
        } else if (strncmp(linha, "FIM_PARTIDA ", 12) == 0) {
            const char* res = linha + 12;
            if (strcmp(res, "VENCEU") == 0) printf("==> Fim da partida: VOCE VENCEU!\n");
            else if (strcmp(res, "PERDEU") == 0) printf("==> Fim da partida: voce perdeu.\n");
            else printf("==> Fim da partida: %s\n", res);
        } else if (strncmp(linha, "JOGAR_NOVAMENTE?", 16) == 0) {
            prompt_and_send_choice(sock, "Jogar novamente? 1-Sim 2-Nao:");
        } else if (strcmp(linha, "ENCERRAR") == 0) {
            printf("Encerrando conforme solicitado pelo servidor.\n");
            jogando = 0;
        } else if (strcmp(linha, "ENTRADA_INVALIDA") == 0) {
            printf("Entrada invalida. Tente novamente.\n");
        } else {
            // Mensagem desconhecida (log)
            printf("[SERVIDOR] %s\n", linha);
        }
    }

    printf("Pressione Enter para sair...");
    fflush(stdout);
    getchar();

    closesocket(sock);
    WSACleanup();
    return 0;
}

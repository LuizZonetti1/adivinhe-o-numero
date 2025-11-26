// Cliente do jogo de adivinhação (modo simultâneo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 12345
#define VALOR_MIN   1
#define VALOR_MAX   100

typedef enum {
    INPUT_IDLE = 0,
    INPUT_SECRET,
    INPUT_GUESS
} InputMode;

typedef struct {
    SOCKET sock;
    volatile LONG running;
    volatile LONG mode;
    volatile LONG secret_min;
    volatile LONG secret_max;
} InputContext;

static InputContext g_input = { INVALID_SOCKET, 0, INPUT_IDLE, VALOR_MIN, VALOR_MAX };

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

static int envia_ln(SOCKET s, const char* msg) {
    int len = (int)strlen(msg);
    if (len > 0) {
        if (send_all(s, msg, len) == SOCKET_ERROR) return SOCKET_ERROR;
    }
    char nl = '\n';
    if (send_all(s, &nl, 1) == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

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

static InputMode get_mode(void) {
    return (InputMode)InterlockedCompareExchange(&g_input.mode, 0, 0);
}

static void set_mode(InputMode novo) {
    InterlockedExchange(&g_input.mode, (LONG)novo);
}

static LONG is_running(void) {
    return InterlockedCompareExchange(&g_input.running, 0, 0);
}

static void stop_running(void) {
    InterlockedExchange(&g_input.running, 0);
}

static void set_secret_bounds(int minv, int maxv) {
    InterlockedExchange(&g_input.secret_min, (LONG)minv);
    InterlockedExchange(&g_input.secret_max, (LONG)maxv);
}

static int prompt_and_send_value(const char* pergunta, int minv, int maxv, const char* prefixo) {
    char linha[128];
    while (is_running()) {
        printf("%s (%d-%d) [digite 'sair' para abandonar]: ", pergunta, minv, maxv);
        fflush(stdout);
        if (!fgets(linha, sizeof(linha), stdin)) {
            return 0;
        }
        trim_crlf(linha);
        if (_stricmp(linha, "sair") == 0) {
            envia_ln(g_input.sock, "SAIR");
            stop_running();
            return 0;
        }
        char* end = NULL;
        long valor = strtol(linha, &end, 10);
        if (end == linha || *end != '\0') {
            printf("[!] Entrada invalida. Use apenas numeros.\n");
            continue;
        }
        if (valor < minv || valor > maxv) {
            printf("[X] Valor fora do intervalo permitido.\n");
            continue;
        }
        InputMode esperado = prefixo ? INPUT_GUESS : INPUT_SECRET;
        if (get_mode() != esperado) {
            printf("[!] Estado alterado. Aguarde novas instrucoes.\n");
            return 0;
        }
        char msg[64];
        if (prefixo) snprintf(msg, sizeof(msg), "%s %ld", prefixo, valor);
        else snprintf(msg, sizeof(msg), "%ld", valor);
        envia_ln(g_input.sock, msg);
        if (prefixo) printf("[OK] Palpite %ld enviado!\n", valor);
        else printf("[OK] Numero secreto %ld registrado!\n", valor);
        return 1;
    }
    return 0;
}

static unsigned __stdcall input_thread(void* param) {
    (void)param;
    while (is_running()) {
        InputMode modo = get_mode();
        if (modo == INPUT_SECRET) {
            int minv = (int)InterlockedCompareExchange(&g_input.secret_min, 0, 0);
            int maxv = (int)InterlockedCompareExchange(&g_input.secret_max, 0, 0);
            if (prompt_and_send_value("[?] Escolha seu numero secreto", minv, maxv, NULL)) {
                set_mode(INPUT_IDLE);
                printf("\n[...] Numero enviado, aguardando adversario...\n");
            }
            continue;
        }
        if (modo == INPUT_GUESS) {
            prompt_and_send_value("[>] Seu palpite", VALOR_MIN, VALOR_MAX, "PALPITE");
            continue;
        }
        Sleep(80);
    }
    return 0;
}

int main(void) {
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

    printf("\n+----------------------------------------------------------+\n");
    printf("|                  >> JOGO DE ADIVINHACAO <<              |\n");
    printf("+----------------------------------------------------------+\n");
    printf("| Conectando ao servidor %s:%d...                    |\n", SERVER_IP, SERVER_PORT);
    printf("+----------------------------------------------------------+\n\n");
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) == SOCKET_ERROR) {
        printf("[X] ERRO: Nao foi possivel conectar ao servidor.\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }
    printf("[OK] Conectado com sucesso! Aguarde instrucoes...\n\n");

    g_input.sock = sock;
    InterlockedExchange(&g_input.running, 1);
    set_mode(INPUT_IDLE);
    HANDLE hInput = (HANDLE)_beginthreadex(NULL, 0, input_thread, NULL, 0, NULL);

    char linha[512];
    while (is_running()) {
        int r = recebe_ln(sock, linha, sizeof(linha));
        if (r <= 0) {
            printf("[X] Conexao encerrada pelo servidor.\n");
            stop_running();
            break;
        }

        if (strncmp(linha, "BEM_VINDO", 9) == 0) {
            printf("\n>> %s! Bem-vindo ao jogo simultaneo! <<\n", linha);
            printf("==========================================================\n");
        } else if (strncmp(linha, "DEFINA_SECRETO", 14) == 0) {
            int minv = VALOR_MIN, maxv = VALOR_MAX;
            sscanf(linha, "DEFINA_SECRETO %d %d", &minv, &maxv);
            set_secret_bounds(minv, maxv);
            set_mode(INPUT_SECRET);
            printf("[!] Defina o seu numero secreto no intervalo %d-%d.\n", minv, maxv);
        } else if (strcmp(linha, "SECRETO_REGISTRADO") == 0) {
            printf("[+] Numero secreto registrado no servidor.\n");
        } else if (strcmp(linha, "AGUARDE_OPONENTE_DEFINIR") == 0 || strcmp(linha, "AGUARDE_ADVERSARIO_ESCOLHER") == 0) {
            set_mode(INPUT_IDLE);
            printf("[...] Aguardando o adversario definir o numero.\n");
        } else if (strncmp(linha, "LIBERADO_PALPITES", 17) == 0) {
            set_mode(INPUT_GUESS);
            printf("\n[*] Ambos podem palpitar livremente! Digite seus palpites quando quiser (ou 'sair' para abandonar).\n");
        } else if (strcmp(linha, "VOCES_PODEM_ADIVINHAR_A_QUALQUER_MOMENTO") == 0) {
            printf("[>] Palpites liberados em paralelo. Boa sorte!\n");
        } else if (strncmp(linha, "PALPITE_RESULTADO ", 18) == 0 || strncmp(linha, "RESULTADO ", 10) == 0) {
            const char* res = (linha[0] == 'P') ? linha + 18 : linha + 10;
            if (strcmp(res, "MAIOR") == 0) printf("[^] O numero escondido e MAIOR que seu palpite.\n");
            else if (strcmp(res, "MENOR") == 0) printf("[v] O numero escondido e MENOR que seu palpite.\n");
            else if (strcmp(res, "ACERTOU") == 0) printf("[*] Voce acertou! Aguarde a confirmacao final.\n");
            else if (strcmp(res, "AGUARDE") == 0) printf("[...] Aguarde: o adversario ainda nao definiu o numero.\n");
            else if (strcmp(res, "PARTIDA_ENCERRADA") == 0) printf("[!] Partida ja encerrada. Aguarde novos comandos.\n");
            else if (strcmp(res, "ENTRADA_INVALIDA") == 0) printf("[!] Entrada invalida. Tente novamente.\n");
            else printf("RESULTADO: %s\n", res);
        } else if (strncmp(linha, "OPONENTE_TENTOU ", 16) == 0 || strncmp(linha, "OPONENTE_CHUTOU ", 16) == 0) {
            int palpite = 0;
            char tag[64] = {0};
            sscanf(linha, "OPONENTE_%*[^ ] %d %63s", &palpite, tag);
            printf("[~] Oponente chutou %d (%s).\n", palpite, tag);
        } else if (strncmp(linha, "NUMERO_ATUALIZADO ", 19) == 0) {
            int novo = 0;
            char oper[64] = {0};
            sscanf(linha, "NUMERO_ATUALIZADO %d %63s", &novo, oper);
            printf("[!] O servidor alterou seu numero para %d (%s). Compartilhe esse valor com cuidado!\n", novo, oper);
        } else if (strncmp(linha, "NUMERO_DO_OPONENTE_MUDOU ", 25) == 0) {
            const char* desc = linha + 25;
            printf("[!] O numero do oponente foi alterado (%s). Reavalie seus palpites!\n", desc);
        } else if (strcmp(linha, "OPONENTE_DESCONECTOU") == 0) {
            set_mode(INPUT_IDLE);
            printf("[X] O oponente desconectou. Partida encerrada.\n");
        } else if (strcmp(linha, "OPONENTE_ACERTOU") == 0) {
            set_mode(INPUT_IDLE);
            printf("[X] O adversario acertou o seu numero!\n");
        } else if (strncmp(linha, "FIM_PARTIDA ", 12) == 0) {
            set_mode(INPUT_IDLE);
            const char* res = linha + 12;
            printf("\n+==========================================================+\n");
            if (strcmp(res, "VENCEU") == 0) {
                printf("|            [*] PARABENS! VOCE VENCEU! [*]              |\n");
            } else if (strcmp(res, "PERDEU") == 0) {
                printf("|            [X] O ADVERSARIO VENCEU AGORA. [X]          |\n");
            } else {
                printf("|                RESULTADO FINAL: %s                |\n", res);
            }
            printf("+==========================================================+\n");
        } else if (strcmp(linha, "ENCERRAR") == 0) {
            printf("\n[!] Servidor solicitou encerramento.\n");
            stop_running();
            break;
        } else if (strcmp(linha, "ENTRADA_INVALIDA") == 0) {
            printf("[!] Entrada invalida.\n");
        } else {
            printf("[SERVIDOR] %s\n", linha);
        }
    }

    set_mode(INPUT_IDLE);
    stop_running();
    if (hInput) {
        WaitForSingleObject(hInput, INFINITE);
        CloseHandle(hInput);
    }

    closesocket(sock);
    WSACleanup();

    printf("\n+============================================================+\n");
    printf("|            Obrigado por jogar! Pressione Enter...        |\n");
    printf("+============================================================+\n");
    fflush(stdout);
    getchar();
    return 0;
}

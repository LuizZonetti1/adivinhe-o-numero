// -----------------------------------------------------------------------------
// Servidor do jogo de adivinhação (modo simultâneo com memória compartilhada)
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define PORTA               12345
#define MIN_SECRETO         1
#define MAX_SECRETO         100
#define ERROS_PARA_MUTACAO  3

typedef enum {
    MUT_MULTIPLICADO_POR_2 = 0,
    MUT_SOMADO_7,
    MUT_DIVIDIDO_POR_2,
    MUT_SUBTRAIDO_5,
    MUT_TOTAL
} MutationType;

static const char* kMutationLabels[MUT_TOTAL] = {
    "MULTIPLICADO_POR_2",
    "SOMADO_7",
    "DIVIDIDO_POR_2",
    "SUBTRAIDO_5"
};

typedef struct {
    SOCKET sock;
    int id;
    int secret;
    int secret_definido;
    int erros_consecutivos;
    int conectado;
} PlayerState;

typedef struct {
    PlayerState jogadores[2];
    int secretos_prontos;
    int partida_ativa;
    int encerrado;
    int vencedor;
    CRITICAL_SECTION lock;
} GameState;

typedef struct {
    GameState* jogo;
    int jogador_id;
} ThreadArgs;

static GameState g_jogo;

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
    if (s == INVALID_SOCKET) return SOCKET_ERROR;
    int len = (int)strlen(msg);
    if (len > 0) {
        if (send_all(s, msg, len) == SOCKET_ERROR) return SOCKET_ERROR;
    }
    char nl = '\n';
    if (send_all(s, &nl, 1) == SOCKET_ERROR) return SOCKET_ERROR;
    return 0;
}

static int envia_fmt(SOCKET s, const char* fmt, ...) {
    if (s == INVALID_SOCKET) return SOCKET_ERROR;
    char buffer[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return envia_ln(s, buffer);
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
        if (r == SOCKET_ERROR) {
            return -1;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        if (pos < outsz - 1) out[pos++] = c;
    }
    out[pos] = '\0';
    return pos;
}

static void safe_shutdown(SOCKET s) {
    if (s != INVALID_SOCKET) shutdown(s, SD_BOTH);
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int aplicar_operacao_mutacao(int valor, MutationType operacao) {
    switch (operacao) {
        case MUT_MULTIPLICADO_POR_2: return valor * 2;
        case MUT_SOMADO_7:          return valor + 7;
        case MUT_DIVIDIDO_POR_2:    return valor / 2;
        case MUT_SUBTRAIDO_5:       return valor - 5;
        default:                    return valor;
    }
}

static int mutacao_dentro_do_intervalo(int valor_atual,
                                       MutationType operacao,
                                       int* valor_resultante) {
    int candidato = aplicar_operacao_mutacao(valor_atual, operacao);
    if (candidato < MIN_SECRETO || candidato > MAX_SECRETO) {
        return 0;
    }
    if (valor_resultante) *valor_resultante = candidato;
    return 1;
}

static int registrar_secreto(GameState* jogo, int jogador, int valor) {
    int iniciar = 0;
    EnterCriticalSection(&jogo->lock);
    if (!jogo->jogadores[jogador].secret_definido && !jogo->encerrado) {
        jogo->jogadores[jogador].secret = valor;
        jogo->jogadores[jogador].secret_definido = 1;
        jogo->secretos_prontos++;
        if (jogo->secretos_prontos == 2) {
            jogo->partida_ativa = 1;
            iniciar = 1;
        }
    }
    LeaveCriticalSection(&jogo->lock);
    return iniciar;
}

static void avisar_inicio(GameState* jogo) {
    for (int i = 0; i < 2; i++) {
        envia_ln(jogo->jogadores[i].sock, "LIBERADO_PALPITES 1 100");
        envia_ln(jogo->jogadores[i].sock, "VOCES_PODEM_ADIVINHAR_A_QUALQUER_MOMENTO");
    }
}

static int aplicar_mutacao(GameState* jogo,
                           int alvo,
                           char* msg_para_alvo,
                           size_t tam_msg_alvo,
                           char* msg_para_palpiteiro,
                           size_t tam_msg_palpiteiro) {
    int valor_atual = jogo->jogadores[alvo].secret;
    MutationType opcoes_validas[MUT_TOTAL];
    int total_validas = 0;

    for (int op = 0; op < MUT_TOTAL; ++op) {
        if (mutacao_dentro_do_intervalo(valor_atual,
                                        (MutationType)op,
                                        NULL)) {
            opcoes_validas[total_validas++] = (MutationType)op;
        }
    }

    if (total_validas == 0) {
        return 0; // Nenhuma mutação manteria o valor dentro do intervalo.
    }

    MutationType operacao = opcoes_validas[rand() % total_validas];
    int novo_valor = aplicar_operacao_mutacao(valor_atual, operacao);
    jogo->jogadores[alvo].secret = novo_valor;

    snprintf(msg_para_alvo, tam_msg_alvo,
             "NUMERO_ATUALIZADO %d %s", novo_valor, kMutationLabels[operacao]);
    snprintf(msg_para_palpiteiro, tam_msg_palpiteiro,
             "NUMERO_DO_OPONENTE_MUDOU %s", kMutationLabels[operacao]);
    return 1;
}

static int finalizar_partida(GameState* jogo, int vencedor) {
    int deve_finalizar = 0;
    EnterCriticalSection(&jogo->lock);
    if (!jogo->encerrado) {
        jogo->encerrado = 1;
        jogo->partida_ativa = 0;
        jogo->vencedor = vencedor;
        deve_finalizar = 1;
    }
    LeaveCriticalSection(&jogo->lock);
    if (!deve_finalizar) return 0;

    int perdedor = 1 - vencedor;
    SOCKET sv = jogo->jogadores[vencedor].sock;
    SOCKET sp = jogo->jogadores[perdedor].sock;

    envia_ln(sv, "FIM_PARTIDA VENCEU");
    envia_ln(sp, "FIM_PARTIDA PERDEU");
    envia_ln(sv, "ENCERRAR");
    envia_ln(sp, "ENCERRAR");

    safe_shutdown(sv);
    safe_shutdown(sp);
    return 1;
}

static void tratar_desconexao(GameState* jogo, int jogador) {
    int outro = 1 - jogador;
    SOCKET soponente = jogo->jogadores[outro].sock;

    EnterCriticalSection(&jogo->lock);
    if (!jogo->encerrado) {
        jogo->encerrado = 1;
        jogo->partida_ativa = 0;
    }
    LeaveCriticalSection(&jogo->lock);

    envia_ln(soponente, "OPONENTE_DESCONECTOU");
    envia_ln(soponente, "ENCERRAR");
    safe_shutdown(soponente);
}

static void tratar_palpite(GameState* jogo, int jogador, int palpite) {
    const int alvo = 1 - jogador;
    char resultado[16] = {0};
    char msg_para_alvo[128] = {0};
    char msg_para_palpiteiro[128] = {0};
    int houve_mutacao = 0;
    int secreto_do_oponente = 0;
    int partida_ativa = 0;
    int oponente_pronto = 0;
    int encerrar_com_vitoria = 0;

    EnterCriticalSection(&jogo->lock);
    partida_ativa = jogo->partida_ativa;
    oponente_pronto = jogo->jogadores[alvo].secret_definido;

    if (!partida_ativa || !oponente_pronto) {
        strcpy(resultado, partida_ativa ? "AGUARDE" : "PARTIDA_ENCERRADA");
        LeaveCriticalSection(&jogo->lock);
        envia_fmt(jogo->jogadores[jogador].sock, "PALPITE_RESULTADO %s", resultado);
        return;
    }

    secreto_do_oponente = jogo->jogadores[alvo].secret;
    if (palpite < secreto_do_oponente) {
        strcpy(resultado, "MAIOR");
    } else if (palpite > secreto_do_oponente) {
        strcpy(resultado, "MENOR");
    } else {
        strcpy(resultado, "ACERTOU");
        jogo->jogadores[jogador].erros_consecutivos = 0;
        encerrar_com_vitoria = 1;
    }

    if (!encerrar_com_vitoria) {
        jogo->jogadores[jogador].erros_consecutivos++;
        if (jogo->jogadores[jogador].erros_consecutivos % ERROS_PARA_MUTACAO == 0) {
            houve_mutacao = aplicar_mutacao(jogo,
                                            alvo,
                                            msg_para_alvo,
                                            sizeof(msg_para_alvo),
                                            msg_para_palpiteiro,
                                            sizeof(msg_para_palpiteiro));
        }
    }
    LeaveCriticalSection(&jogo->lock);

    envia_fmt(jogo->jogadores[jogador].sock, "PALPITE_RESULTADO %s", resultado);
    envia_fmt(jogo->jogadores[alvo].sock, "OPONENTE_TENTOU %d %s", palpite, resultado);

    if (houve_mutacao) {
        envia_ln(jogo->jogadores[alvo].sock, msg_para_alvo);
        envia_ln(jogo->jogadores[jogador].sock, msg_para_palpiteiro);
    }

    if (encerrar_com_vitoria) {
        finalizar_partida(jogo, jogador);
    }
}

static unsigned __stdcall rotina_jogador(void* param) {
    ThreadArgs* args = (ThreadArgs*)param;
    GameState* jogo = args->jogo;
    int jogador = args->jogador_id;
    SOCKET sock = jogo->jogadores[jogador].sock;
    char linha[256];

    envia_fmt(sock, "BEM_VINDO P%d", jogador + 1);
    envia_ln(sock, "DEFINA_SECRETO 1 100");

    while (1) {
        int r = recebe_ln(sock, linha, sizeof(linha));
        if (r <= 0) {
            int ja_finalizado = 0;
            EnterCriticalSection(&jogo->lock);
            ja_finalizado = jogo->encerrado;
            LeaveCriticalSection(&jogo->lock);
            if (!ja_finalizado) {
                printf("Jogador %d desconectou inesperadamente.\n", jogador + 1);
                tratar_desconexao(jogo, jogador);
            }
            break;
        }

        if (!jogo->jogadores[jogador].secret_definido) {
            int valor = 0;
            if (sscanf(linha, "%d", &valor) == 1 && valor >= MIN_SECRETO && valor <= MAX_SECRETO) {
                int iniciar = registrar_secreto(jogo, jogador, valor);
                envia_ln(sock, "SECRETO_REGISTRADO");
                if (!iniciar) {
                    envia_ln(sock, "AGUARDE_OPONENTE_DEFINIR");
                } else {
                    avisar_inicio(jogo);
                }
            } else {
                envia_ln(sock, "ENTRADA_INVALIDA");
            }
            continue;
        }

        if (strncmp(linha, "PALPITE", 7) == 0) {
            int palpite = 0;
            if (sscanf(linha + 7, "%d", &palpite) == 1) {
                tratar_palpite(jogo, jogador, palpite);
            } else {
                envia_ln(sock, "PALPITE_RESULTADO ENTRADA_INVALIDA");
            }
            continue;
        }

        int valor_livre = 0;
        if (sscanf(linha, "%d", &valor_livre) == 1) {
            tratar_palpite(jogo, jogador, valor_livre);
            continue;
        }

        if (_stricmp(linha, "SAIR") == 0) {
            tratar_desconexao(jogo, jogador);
            break;
        }

        envia_ln(sock, "PALPITE_RESULTADO COMANDO_DESCONHECIDO");
    }

    closesocket(sock);
    return 0;
}

int main(void) {
    WSADATA wsa;
    SOCKET servidor = INVALID_SOCKET;
    struct sockaddr_in addr_servidor, addr_cliente;
    int tam_cliente = sizeof(addr_cliente);
    HANDLE threads[2] = {0};
    ThreadArgs args[2];

    srand((unsigned)time(NULL));
    memset(&g_jogo, 0, sizeof(g_jogo));
    InitializeCriticalSection(&g_jogo.lock);

    printf("Servidor de adivinhacao simultaneo - aguardando jogadores...\n");
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Falha no WSAStartup.\n");
        DeleteCriticalSection(&g_jogo.lock);
        return 1;
    }

    servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor == INVALID_SOCKET) {
        printf("Falha ao criar socket.\n");
        WSACleanup();
        DeleteCriticalSection(&g_jogo.lock);
        return 1;
    }

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
        DeleteCriticalSection(&g_jogo.lock);
        return 1;
    }

    if (listen(servidor, 2) == SOCKET_ERROR) {
        printf("Erro no listen.\n");
        closesocket(servidor);
        WSACleanup();
        DeleteCriticalSection(&g_jogo.lock);
        return 1;
    }

    for (int i = 0; i < 2; ++i) {
        tam_cliente = sizeof(addr_cliente);
        g_jogo.jogadores[i].sock = accept(servidor, (struct sockaddr*)&addr_cliente, &tam_cliente);
        if (g_jogo.jogadores[i].sock == INVALID_SOCKET) {
            printf("Erro ao aceitar jogador %d.\n", i + 1);
            for (int j = 0; j < i; ++j) closesocket(g_jogo.jogadores[j].sock);
            closesocket(servidor);
            WSACleanup();
            DeleteCriticalSection(&g_jogo.lock);
            return 1;
        }
        g_jogo.jogadores[i].id = i;
        g_jogo.jogadores[i].conectado = 1;
        printf("Jogador %d conectado.\n", i + 1);
    }

    for (int i = 0; i < 2; ++i) {
        args[i].jogo = &g_jogo;
        args[i].jogador_id = i;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, rotina_jogador, &args[i], 0, NULL);
    }

    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    for (int i = 0; i < 2; ++i) {
        if (threads[i]) CloseHandle(threads[i]);
    }

    closesocket(servidor);
    WSACleanup();
    DeleteCriticalSection(&g_jogo.lock);
    printf("Servidor finalizado.\n");
    return 0;
}

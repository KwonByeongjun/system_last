// src/server.c

#include "../include/server.h"
#include "../include/game.h"
#include "../include/json.h"

#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // close()
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BOARD_SIZE 8
#define MAX_CLIENTS 2
#define TIMEOUT 5  // 예시: select 타임아웃

typedef struct {
    int socket;
    char username[32];
    char color;    // 'R' 또는 'B'
    int registered;
} Player;

typedef struct {
    Player players[MAX_CLIENTS];
    int current_turn;           // 0=R, 1=B
    char board[BOARD_SIZE][BOARD_SIZE];
} GameState;

static GameState game;
static int global_listen_fd = -1;

// ==============================
// (1) init_game_state: GameState를 시작 상태로 초기화
// ==============================
void init_game_state(GameState *g) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g->players[i].socket = -1;
        g->players[i].username[0] = '\0';
        g->players[i].color = (i == 0 ? 'R' : 'B');
        g->players[i].registered = 0;
    }
    g->current_turn = 0;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            g->board[r][c] = '.';
        }
    }
    g->board[3][3] = 'R';
    g->board[3][4] = 'B';
    g->board[4][3] = 'B';
    g->board[4][4] = 'R';
}

// ==============================
// (2) create_listen_socket: <port>로 바인딩 후 listen 상태로 만든 뒤 소켓 디스크립터 반환
// ==============================
static int create_listen_socket(const char *port) {
    struct addrinfo hints, *res, *p;
    int listen_fd, rv;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) continue;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listen_fd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to bind on port %s\n", port);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

// ==============================
// (3) reject_late_clients 스레드: 이미 2명이 등록된 뒤 들어오는 연결은 NACK & close
// ==============================
static void *reject_late_clients(void *arg) {
    (void)arg;
    while (global_listen_fd >= 0) {
        struct sockaddr_storage cli_addr;
        socklen_t sin_size = sizeof cli_addr;
        int new_fd = accept(global_listen_fd, (struct sockaddr *)&cli_addr, &sin_size);
        if (new_fd >= 0) {
            cJSON *nack = cJSON_CreateObject();
            cJSON_AddStringToObject(nack, "type", "register_nack");
            cJSON_AddStringToObject(nack, "reason", "Server already has 2 players");
            send_json(new_fd, nack);
            cJSON_Delete(nack);
            close(new_fd);
        }
    }
    return NULL;
}

// ==============================
// (4) accept_and_register: 최대 MAX_CLIENTS명까지 accept + “register” 처리
// ==============================
static int accept_and_register(int listen_fd) {
    global_listen_fd = listen_fd;

    pthread_t reject_thread;
    pthread_create(&reject_thread, NULL, reject_late_clients, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct sockaddr_storage cli_addr;
        socklen_t sin_size = sizeof cli_addr;
        int new_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &sin_size);
        if (new_fd < 0) {
            perror("accept");
            i--;
            continue;
        }

        cJSON *msg = recv_json(new_fd);
        if (!msg) {
            close(new_fd);
            i--;
            continue;
        }
        cJSON *jtype = cJSON_GetObjectItem(msg, "type");
        cJSON *jusername = cJSON_GetObjectItem(msg, "username");
        if (!jtype || !jusername || strcmp(jtype->valuestring, "register") != 0) {
            cJSON *nack = cJSON_CreateObject();
            cJSON_AddStringToObject(nack, "type", "register_nack");
            cJSON_AddStringToObject(nack, "reason", "Invalid register message");
            send_json(new_fd, nack);
            cJSON_Delete(nack);
            cJSON_Delete(msg);
            close(new_fd);
            i--;
            continue;
        }

        // 올바른 등록
        strncpy(game.players[i].username, jusername->valuestring, sizeof(game.players[i].username)-1);
        game.players[i].username[sizeof(game.players[i].username)-1] = '\0';
        game.players[i].socket = new_fd;
        game.players[i].registered = 1;
        game.players[i].color = (i == 0 ? 'R' : 'B');

        // register_ack 브로드캐스트
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "register_ack");
        cJSON *jplayers = cJSON_AddArrayToObject(ack, "players");
        for (int k = 0; k <= i; k++) {
            cJSON_AddItemToArray(jplayers, cJSON_CreateString(game.players[k].username));
        }
        for (int k = 0; k <= i; k++) {
            send_json(game.players[k].socket, ack);
        }
        cJSON_Delete(ack);
        cJSON_Delete(msg);
    }
    return 0;
}

// ==============================
// (5) board_to_json: GameState → JSON 객체로 변환 ({ "type":"board_update", "board":[…] })
// ==============================
static cJSON *board_to_json(const GameState *g) {
    cJSON *job = cJSON_CreateObject();
    cJSON_AddStringToObject(job, "type", "board_update");
    cJSON *jboard = cJSON_CreateArray();
    for (int r = 0; r < BOARD_SIZE; r++) {
        char line[BOARD_SIZE+1];
        for (int c = 0; c < BOARD_SIZE; c++) {
            line[c] = g->board[r][c];
        }
        line[BOARD_SIZE] = '\0';
        cJSON_AddItemToArray(jboard, cJSON_CreateString(line));
    }
    cJSON_AddItemToObject(job, "board", jboard);
    return job;
}

// ==============================
// (6) game_loop: "your_turn" → move/pass 처리 → "move_ok"/"pass" broadcast → 턴 교대 → "game_over"
// ==============================
static void game_loop(void) {
    init_game_state(&game);

    // 초기 game_start 브로드캐스트
    {
        cJSON *start_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(start_msg, "type", "game_start");
        cJSON *jboard = cJSON_CreateArray();
        for (int r = 0; r < BOARD_SIZE; r++) {
            char line[BOARD_SIZE+1];
            for (int c = 0; c < BOARD_SIZE; c++) {
                line[c] = game.board[r][c];
            }
            line[BOARD_SIZE] = '\0';
            cJSON_AddItemToArray(jboard, cJSON_CreateString(line));
        }
        cJSON_AddItemToObject(start_msg, "board", jboard);
        send_json(game.players[0].socket, start_msg);
        send_json(game.players[1].socket, start_msg);
        cJSON_Delete(start_msg);
    }

    while (1) {
        int cur = game.current_turn;       // 0=R, 1=B
        int opp = 1 - cur;
        int cur_fd = game.players[cur].socket;
        char cur_color = (cur == 0 ? 'R' : 'B');

        // your_turn
        cJSON *your_turn = cJSON_CreateObject();
        cJSON_AddStringToObject(your_turn, "type", "your_turn");
        cJSON *jboard = cJSON_CreateArray();
        for (int r = 0; r < BOARD_SIZE; r++) {
            char line[BOARD_SIZE+1];
            for (int c = 0; c < BOARD_SIZE; c++) {
                line[c] = game.board[r][c];
            }
            line[BOARD_SIZE] = '\0';
            cJSON_AddItemToArray(jboard, cJSON_CreateString(line));
        }
        cJSON_AddItemToObject(your_turn, "board", jboard);
        send_json(cur_fd, your_turn);
        cJSON_Delete(your_turn);

        // 응답 대기 (select + TIMEOUT)
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(cur_fd, &readfds);
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        int rv = select(cur_fd + 1, &readfds, NULL, NULL, &tv);
        if (rv == 0) {
            // TIMEOUT → pass
            cJSON *pass_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(pass_msg, "type", "pass");
            cJSON *jboard2 = cJSON_CreateArray();
            for (int r = 0; r < BOARD_SIZE; r++) {
                char line[BOARD_SIZE+1];
                for (int c = 0; c < BOARD_SIZE; c++) {
                    line[c] = game.board[r][c];
                }
                line[BOARD_SIZE] = '\0';
                cJSON_AddItemToArray(jboard2, cJSON_CreateString(line));
            }
            cJSON_AddItemToObject(pass_msg, "board", jboard2);
            send_json(cur_fd, pass_msg);
            cJSON_Delete(pass_msg);
            game.current_turn = opp;
            continue;
        }
        else if (rv < 0) {
            perror("select");
            break;
        }

        // 클라이언트 응답 수신
        cJSON *req = recv_json(cur_fd);
        if (!req) {
            printf("Player %s disconnected.\n", game.players[cur].username);
            break;
        }
        cJSON *rtype = cJSON_GetObjectItem(req, "type");
        if (!rtype || !rtype->valuestring) {
            cJSON_Delete(req);
            continue;
        }
        const char *type = rtype->valuestring;

        if (strcmp(type, "move") == 0) {
            cJSON *jfrom = cJSON_GetObjectItem(req, "from");
            cJSON *jto   = cJSON_GetObjectItem(req, "to");
            if (jfrom && jto && cJSON_IsArray(jfrom) && cJSON_IsArray(jto)) {
                int r1 = cJSON_GetArrayItem(jfrom, 0)->valueint;
                int c1 = cJSON_GetArrayItem(jfrom, 1)->valueint;
                int r2 = cJSON_GetArrayItem(jto,   0)->valueint;
                int c2 = cJSON_GetArrayItem(jto,   1)->valueint;

                if (isValidMove(game.board, cur, r1, c1, r2, c2)) {
                    Move(game.board, cur, r1, c1, r2, c2);

                    cJSON *move_ok = cJSON_CreateObject();
                    cJSON_AddStringToObject(move_ok, "type", "move_ok");
                    cJSON *jboard3 = cJSON_CreateArray();
                    for (int rr = 0; rr < BOARD_SIZE; rr++) {
                        char line[BOARD_SIZE+1];
                        for (int cc = 0; cc < BOARD_SIZE; cc++) {
                            line[cc] = game.board[rr][cc];
                        }
                        line[BOARD_SIZE] = '\0';
                        cJSON_AddItemToArray(jboard3, cJSON_CreateString(line));
                    }
                    cJSON_AddItemToObject(move_ok, "board", jboard3);
                    send_json(game.players[0].socket, move_ok);
                    send_json(game.players[1].socket, move_ok);
                    cJSON_Delete(move_ok);

                    game.current_turn = opp;
                }
                else {
                    cJSON *inv = cJSON_CreateObject();
                    cJSON_AddStringToObject(inv, "type", "invalid_move");
                    send_json(cur_fd, inv);
                    cJSON_Delete(inv);
                }
            }
            cJSON_Delete(req);
        }
        else if (strcmp(type, "pass") == 0) {
            game.current_turn = opp;

            cJSON *pass_brd = cJSON_CreateObject();
            cJSON_AddStringToObject(pass_brd, "type", "pass");
            cJSON *jboard4 = cJSON_CreateArray();
            for (int rr = 0; rr < BOARD_SIZE; rr++) {
                char line[BOARD_SIZE+1];
                for (int cc = 0; cc < BOARD_SIZE; cc++) {
                    line[cc] = game.board[rr][cc];
                }
                line[BOARD_SIZE] = '\0';
                cJSON_AddItemToArray(jboard4, cJSON_CreateString(line));
            }
            cJSON_AddItemToObject(pass_brd, "board", jboard4);
            send_json(game.players[0].socket, pass_brd);
            send_json(game.players[1].socket, pass_brd);
            cJSON_Delete(pass_brd);

            cJSON_Delete(req);
        }
        else {
            cJSON_Delete(req);
        }
    }

    // 게임 종료 → broadcast game_over
    {
        cJSON *go = cJSON_CreateObject();
        cJSON_AddStringToObject(go, "type", "game_over");
        cJSON *jboard5 = cJSON_CreateArray();
        for (int r = 0; r < BOARD_SIZE; r++) {
            char line[BOARD_SIZE+1];
            for (int c = 0; c < BOARD_SIZE; c++) {
                line[c] = game.board[r][c];
            }
            line[BOARD_SIZE] = '\0';
            cJSON_AddItemToArray(jboard5, cJSON_CreateString(line));
        }
        cJSON_AddItemToObject(go, "board", jboard5);

        int rcount = countR(game.board);
        int bcount = countB(game.board);
        if (rcount > bcount) {
            cJSON_AddStringToObject(go, "winner", game.players[0].username);
        } else if (bcount > rcount) {
            cJSON_AddStringToObject(go, "winner", game.players[1].username);
        } else {
            cJSON_AddStringToObject(go, "winner", "Draw");
        }

        send_json(game.players[0].socket, go);
        send_json(game.players[1].socket, go);
        cJSON_Delete(go);
    }
}

// ==============================
// (7) server_run: main.c에서 호출되는 진입점
// ==============================
int server_run(const char *port) {
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to create listening socket on port %s\n", port);
        return EXIT_FAILURE;
    }
    global_listen_fd = listen_fd;

    accept_and_register(listen_fd);
    game_loop();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].socket >= 0) {
            close(game.players[i].socket);
        }
    }
    close(listen_fd);
    global_listen_fd = -1;
    printf("Server stopped.\n");
    return EXIT_SUCCESS;
}

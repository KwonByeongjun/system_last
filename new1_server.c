#include "../include/server.h"
#include "../include/game.h"
#include "../include/json.h"
#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>

#define BOARD_SIZE 8
#define MAX_CLIENTS 2
#define TIMEOUT 5

static GameState game;          // server.h에 선언된 GameState 타입
static int global_listen_fd = -1;

// ---------------------------
// init_game_state: 보드와 플레이어 초기화
// ---------------------------
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
    // 예시 초기 배치(필요 없으면 지워도 무방)
    g->board[3][3] = 'R';
    g->board[3][4] = 'B';
    g->board[4][3] = 'B';
    g->board[4][4] = 'R';
}

// ---------------------------
// broadcast_json: 모든 등록된 클라이언트에 JSON 브로드캐스트
// ---------------------------
void broadcast_json(const cJSON *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].socket >= 0) {
            send_json(game.players[i].socket, msg);
        }
    }
}

// ---------------------------
// create_listen_socket: 포트 바인딩 후 listen 소켓 생성
// ---------------------------
static int create_listen_socket(const char *port) {
    struct addrinfo hints, *res, *p;
    int listen_fd, yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }
    for (p = res; p; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) continue;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(listen_fd);
    }
    freeaddrinfo(res);
    if (!p) return -1;
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}

// ---------------------------
// accept_and_register: 최대 MAX_CLIENTS까지 register 처리
// ---------------------------
static int accept_and_register(int listen_fd) {
    int registered = 0;
    global_listen_fd = listen_fd;

    while (registered < MAX_CLIENTS) {
        struct sockaddr_storage cli_addr;
        socklen_t sin_size = sizeof cli_addr;
        int new_fd = accept(global_listen_fd, (struct sockaddr *)&cli_addr, &sin_size);
        if (new_fd < 0) {
            perror("accept");
            continue;
        }

        if (registered >= MAX_CLIENTS) {
            cJSON *nack = cJSON_CreateObject();
            cJSON_AddStringToObject(nack, "type", "register_nack");
            cJSON_AddStringToObject(nack, "reason", "game already full");
            send_json(new_fd, nack);
            cJSON_Delete(nack);
            close(new_fd);
            continue;
        }

        cJSON *msg = recv_json(new_fd);
        if (!msg) {
            close(new_fd);
            continue;
        }
        cJSON *jtype = cJSON_GetObjectItem(msg, "type");
        cJSON *juser = cJSON_GetObjectItem(msg, "username");
        cJSON *resp = cJSON_CreateObject();

        if (jtype && jtype->valuestring
            && strcmp(jtype->valuestring, "register") == 0
            && juser && juser->valuestring)
        {
            int dup = 0;
            for (int i = 0; i < registered; i++) {
                if (strcmp(game.players[i].username, juser->valuestring) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                cJSON_AddStringToObject(resp, "type", "register_nack");
                cJSON_AddStringToObject(resp, "reason", "username exists");
            } else {
                strncpy(game.players[registered].username,
                        juser->valuestring,
                        sizeof(game.players[0].username)-1);
                game.players[registered].username[sizeof(game.players[0].username)-1] = '\0';
                game.players[registered].socket = new_fd;
                game.players[registered].registered = 1;
                registered++;
                cJSON_AddStringToObject(resp, "type", "register_ack");
            }
        } else {
            cJSON_AddStringToObject(resp, "type", "register_nack");
            cJSON_AddStringToObject(resp, "reason", "invalid register");
        }

        send_json(new_fd, resp);
        cJSON_Delete(resp);
        cJSON_Delete(msg);
    }
    return 0;
}

// ---------------------------
// reject_late_clients: 이미 MAX_CLIENTS 등록된 후 들어온 연결 즉시 NACK
// ---------------------------
static void *reject_late_clients(void *arg) {
    (void)arg;
    while (1) {
        if (global_listen_fd < 0) break;
        struct sockaddr_storage cli_addr;
        socklen_t sin_size = sizeof cli_addr;
        int new_fd = accept(global_listen_fd, (struct sockaddr *)&cli_addr, &sin_size);
        if (new_fd >= 0) {
            cJSON *nack = cJSON_CreateObject();
            cJSON_AddStringToObject(nack, "type", "register_nack");
            cJSON_AddStringToObject(nack, "reason", "game already full");
            send_json(new_fd, nack);
            cJSON_Delete(nack);
            close(new_fd);
        }
        sleep(1);
    }
    return NULL;
}

// ---------------------------
// board_to_json: 2D 보드를 JSON 배열로 변환
// ---------------------------
static cJSON *board_to_json(const GameState *g) {
    cJSON *jboard = cJSON_CreateArray();
    for (int r = 0; r < BOARD_SIZE; r++) {
        char line[BOARD_SIZE+1];
        for (int c = 0; c < BOARD_SIZE; c++) {
            line[c] = g->board[r][c];
        }
        line[BOARD_SIZE] = '\0';
        cJSON_AddItemToArray(jboard, cJSON_CreateString(line));
    }
    return jboard;
}

// ---------------------------
// game_loop: your_turn → move/pass 처리 → 브로드캐스트 → 턴 교대 → game_over
// ---------------------------
static void game_loop(void) {
    init_game_state(&game);

    // game_start 브로드캐스트
    {
        cJSON *start = cJSON_CreateObject();
        cJSON_AddStringToObject(start, "type", "game_start");
        cJSON *jplayers = cJSON_AddArrayToObject(start, "players");
        cJSON_AddItemToArray(jplayers, cJSON_CreateString(game.players[0].username));
        cJSON_AddItemToArray(jplayers, cJSON_CreateString(game.players[1].username));
        broadcast_json(start);
        cJSON_Delete(start);
    }

    int turn = 0;
    int passCount = 0;

    while (!isGameOver(game.board)) {
        int cur_fd = game.players[turn].socket;
        char cur_color = game.players[turn].color;

        // your_turn
        {
            cJSON *yt = cJSON_CreateObject();
            cJSON_AddStringToObject(yt, "type", "your_turn");
            cJSON_AddItemToObject(yt, "board", board_to_json(&game));
            cJSON_AddNumberToObject(yt, "timeout", TIMEOUT);
            send_json(cur_fd, yt);
            cJSON_Delete(yt);
        }

        // select 대기
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(cur_fd, &readfds);
        struct timeval tv = { .tv_sec = TIMEOUT, .tv_usec = 0 };
        int sel = select(cur_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel < 0) {
            perror("select");
            break;
        }
        if (sel == 0) {
            // TIMEOUT → 자동 패스
            cJSON *pm = cJSON_CreateObject();
            passCount++;
            cJSON_AddStringToObject(pm, "type", "pass");
            cJSON_AddItemToObject(pm, "board", board_to_json(&game));
            cJSON_AddStringToObject(pm, "next_player", game.players[1 - turn].username);
            broadcast_json(pm);
            cJSON_Delete(pm);

            if (passCount == 2 || isGameOver(game.board)) break;
            turn = 1 - turn;
            continue;
        }

        // 클라이언트 요청 수신
        cJSON *req = recv_json(cur_fd);
        if (!req) break;
        cJSON *jtype = cJSON_GetObjectItem(req, "type");
        cJSON *resp = cJSON_CreateObject();

        if (jtype && strcmp(jtype->valuestring, "move") == 0) {
            passCount = 0;
            int r1 = cJSON_GetObjectItem(req, "sx")->valueint - 1;
            int c1 = cJSON_GetObjectItem(req, "sy")->valueint - 1;
            int r2 = cJSON_GetObjectItem(req, "tx")->valueint - 1;
            int c2 = cJSON_GetObjectItem(req, "ty")->valueint - 1;

            if (r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1) {
                if (hasValidMove(game.board, cur_color)) {
                    cJSON_AddStringToObject(resp, "type", "invalid_move");
                } else {
                    passCount++;
                    cJSON_AddStringToObject(resp, "type", "pass");
                    cJSON_AddItemToObject(resp, "board", board_to_json(&game));
                    cJSON_AddStringToObject(resp, "next_player", game.players[1 - turn].username);
                    broadcast_json(resp);
                    cJSON_Delete(resp);
                    cJSON_Delete(req);
                    if (passCount == 2 || isGameOver(game.board)) break;
                    turn = 1 - turn;
                    continue;
                }
            } else if (isValidInput(game.board, r1, c1, r2, c2) &&
                       isValidMove(game.board, cur_color, r1, c1, r2, c2))
            {
                Move(game.board, turn, r1, c1, r2, c2);
                cJSON_AddStringToObject(resp, "type", "move_ok");
                turn = 1 - turn;
            } else {
                cJSON_AddStringToObject(resp, "type", "invalid_move");
            }
        } else {
            cJSON_Delete(resp);
            cJSON_Delete(req);
            continue;
        }

        // move_ok or invalid_move 브로드캐스트
        cJSON_AddItemToObject(resp, "board", board_to_json(&game));
        cJSON_AddStringToObject(resp, "next_player", game.players[1 - turn].username);
        broadcast_json(resp);
        cJSON_Delete(resp);
        cJSON_Delete(req);
    }

    // game_over 브로드캐스트
    {
        cJSON *go = cJSON_CreateObject();
        cJSON_AddStringToObject(go, "type", "game_over");
        cJSON_AddItemToObject(go, "board", board_to_json(&game));
        cJSON *scores = cJSON_CreateObject();
        cJSON_AddNumberToObject(scores, game.players[0].username, countR(game.board));
        cJSON_AddNumberToObject(scores, game.players[1].username, countB(game.board));
        cJSON_AddItemToObject(go, "scores", scores);
        broadcast_json(go);
        cJSON_Delete(go);
    }
}

// ---------------------------
// server_run: main.c에서 호출되는 진입점
// ---------------------------
int server_run(const char *port) {
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create listening socket on port %s\n", port);
        return EXIT_FAILURE;
    }
    printf("Server started on port %s\n", port);

    init_game_state(&game);

    // 기존에 update_led_matrix 호출 전부 제거
    pthread_t reject_thread;
    pthread_create(&reject_thread, NULL, reject_late_clients, NULL);
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

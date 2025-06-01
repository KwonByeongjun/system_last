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

// 전역 게임 상태 구조체
static GameState game;
static int global_listen_fd = -1;

// ==============================
// (1) init_game_state: GameState를 시작 상태로 초기화
// ==============================
void init_game_state(GameState *g) {
    // 1) 모든 플레이어 슬롯 초기화
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g->players[i].socket = -1;
        g->players[i].username[0] = '\0';
        g->players[i].color = (i == 0 ? 'R' : 'B'); // 첫 번째 등록자는 'R', 두 번째는 'B'
        g->players[i].registered = 0;
    }
    // 2) current_turn 을 0으로 설정 (0 = Red의 턴)
    g->current_turn = 0;
    // 3) 보드를 8×8 점(.)으로 초기화한 다음, 중간 4칸에 R/B 배치
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            g->board[r][c] = '.';
        }
    }
    // Othello 초기 배치 (중앙 4칸)
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
    hints.ai_family = AF_INET;        // IPv4만
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;      // bind용

    if ((rv = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) continue;

        // 주소 재사용 옵션
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
// (3) accept_and_register: 최대 MAX_CLIENTS(=2)명까지 accept + "register" 메시지 처리
// ==============================
//   - 클라이언트가 { "type": "register", "username": "..." } 형태의 JSON을 보내면,
//     서버는 { "type": "register_ack", "players": [name0,name1] } 또는
//     { "type": "register_nack", "reason": "..." } 를 돌려준다.
// ==============================
static void *reject_late_clients(void *arg) {
    (void)arg;
    // 이미 등록이 완료된 뒤 새로운 accept 요청이 오면 즉시 NACK해 주는 스레드
    while (global_listen_fd >= 0) {
        struct sockaddr_storage cli_addr;
        socklen_t sin_size = sizeof cli_addr;
        int new_fd = accept(global_listen_fd, (struct sockaddr *)&cli_addr, &sin_size);
        if (new_fd >= 0) {
            // 즉시 NACK 보내고 닫기
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

static int accept_and_register(int listen_fd) {
    global_listen_fd = listen_fd;

    // 동시에 들어오는 세션을 최대 MAX_CLIENTS까지 받아야 하므로
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

        // recv_json을 통해 { "type": "register", "username": "..." }를 받는다
        cJSON *msg = recv_json(new_fd);
        if (!msg) {
            close(new_fd);
            i--;
            continue;
        }

        cJSON *jtype = cJSON_GetObjectItem(msg, "type");
        cJSON *jusername = cJSON_GetObjectItem(msg, "username");
        if (!jtype || !jusername || strcmp(jtype->valuestring, "register") != 0) {
            // 형식이 잘못된 메시지 → NACK
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

        // 올바른 등록 요청일 경우, 플레이어 슬롯에 기록
        strncpy(game.players[i].username, jusername->valuestring, sizeof(game.players[i].username)-1);
        game.players[i].username[sizeof(game.players[i].username)-1] = '\0';
        game.players[i].socket = new_fd;
        game.players[i].registered = 1;
        game.players[i].color = (i == 0 ? 'R' : 'B'); // 첫 번째=R, 두 번째=B

        // register_ack 메시지를 두 플레이어 모두에게 브로드캐스트
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "register_ack");
        cJSON *jplayers = cJSON_AddArrayToObject(ack, "players");
        for (int k = 0; k <= i; k++) {
            cJSON_AddItemToArray(jplayers, cJSON_CreateString(game.players[k].username));
        }
        // 바로 보낸 후 delete
        for (int k = 0; k <= i; k++) {
            send_json(game.players[k].socket, ack);
        }
        cJSON_Delete(ack);
        cJSON_Delete(msg);
    }

    // 더 이상 추가 접속은 reject_late_clients 스레드가 처리
    return 0;
}

// ==============================
// (4) Helper: GameState → JSON 객체로 변환해서 리턴
//     { "type": "...", "board": [ "........", ... ], ... }
// ==============================
static cJSON *board_to_json(const GameState *g) {
    cJSON *job = cJSON_CreateObject();
    cJSON_AddStringToObject(job, "type", "board_update");

    // board를 “문자열 8개짜리 배열”로 보낸다
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
// (5) game_loop: 실제 게임 로직 + JSON 송수신
// ==============================
static void game_loop(void) {
    // 1) 게임 초기 상태 세팅
    init_game_state(&game);

    // 2) 첫 번째 초기 보드를 두 플레이어에게 모두 전송
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

        // player[0], player[1]에게 전송
        send_json(game.players[0].socket, start_msg);
        send_json(game.players[1].socket, start_msg);
        cJSON_Delete(start_msg);
    }

    // 3) 메인 게임 루프: 턴마다 “your_turn” → 클라이언트 응답 받기 → 보드 갱신 → “move_ok” → 턴 교대
    while (!isGameOver(game.board)) {
        int cur = game.current_turn;            // 0=Red, 1=Blue
        int opp = 1 - cur;
        int cur_fd = game.players[cur].socket;
        char cur_color = (cur == 0 ? 'R' : 'B');

        // (a) your_turn 메시지 전송
        cJSON *your_turn = cJSON_CreateObject();
        cJSON_AddStringToObject(your_turn, "type", "your_turn");
        // 현재 보드 상태
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

        // 합법적 이동(legal_moves)은 클라이언트 쪽에서 생성해도 되고, 
        // 서버에서 보내주고 싶으면 아래와 같이 추가할 수도 있음. (생략해도 무방)
        // cJSON *jlegal = cJSON_CreateArray();
        // ... legal moves 반복해서 jlegal에 추가 ...
        // cJSON_AddItemToObject(your_turn, "legal_moves", jlegal);

        send_json(cur_fd, your_turn);
        cJSON_Delete(your_turn);

        // (b) 클라이언트 응답 기다리기 (TIMEOUT 초 제한)
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(cur_fd, &readfds);
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;

        int rv = select(cur_fd + 1, &readfds, NULL, NULL, &tv);
        if (rv == 0) {
            // TIMEOUT 발생 → 해당 플레이어 패스 처리
            cJSON *pass_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(pass_msg, "type", "pass");
            // 현재 보드 상태 포함 가능
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

            // 턴 교대만 하고 continue
            game.current_turn = opp;
            continue;
        }
        else if (rv < 0) {
            perror("select");
            break;
        }

        // (c) 클라이언트가 보낸 JSON 메시지 수신
        cJSON *req = recv_json(cur_fd);
        if (!req) {
            // 연결 끊김
            printf("Player %s disconnected.\n", game.players[cur].username);
            break;
        }

        cJSON *rtype = cJSON_GetObjectItem(req, "type");
        if (!rtype || !rtype->valuestring) {
            cJSON_Delete(req);
            continue;
        }
        const char *type = rtype->valuestring;

        // (c1) move 요청: { "type": "move", "from": [r1,c1], "to": [r2,c2] }
        if (strcmp(type, "move") == 0) {
            cJSON *jfrom = cJSON_GetObjectItem(req, "from");
            cJSON *jto   = cJSON_GetObjectItem(req, "to");
            if (jfrom && jto && cJSON_IsArray(jfrom) && cJSON_IsArray(jto)) {
                int r1 = cJSON_GetArrayItem(jfrom, 0)->valueint;
                int c1 = cJSON_GetArrayItem(jfrom, 1)->valueint;
                int r2 = cJSON_GetArrayItem(jto,   0)->valueint;
                int c2 = cJSON_GetArrayItem(jto,   1)->valueint;

                // (c1-a) 유효한 움직임인지 검사
                if (isValidMove(game.board, cur_color, r1, c1, r2, c2)) {
                    // (c1-b) 보드 갱신
                    Move(game.board, game.current_turn, r1, c1, r2, c2);

                    // (c1-c) move_ok 메시지 작성 및 브로드캐스트
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

                    // 턴 교대
                    game.current_turn = opp;
                }
                else {
                    // (c1-d) invalid_move 메시지 전송
                    cJSON *inv = cJSON_CreateObject();
                    cJSON_AddStringToObject(inv, "type", "invalid_move");
                    send_json(cur_fd, inv);
                    cJSON_Delete(inv);
                    // 이 턴을 반복 (turn을 바꾸지 않음)
                }
            }
            cJSON_Delete(req);
        }
        // (c2) pass 요청: { "type": "pass" }
        else if (strcmp(type, "pass") == 0) {
            // (c2-a) pass 메시지 받았을 때, 단순히 턴 교대
            game.current_turn = opp;

            // (c2-b) 게임 상태를 두 플레이어에게 알려주기 위해 broadcast
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
            // 알 수 없는 메시지: 아무것도 안 함
            cJSON_Delete(req);
        }
    }

    // 4) 게임 종료: isGameOver == true
    //    결과를 두 플레이어에게 broadcast
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

        // 승패 결과
        int rcount = countR(game.board);
        int bcount = countB(game.board);
        if (rcount > bcount) {
            cJSON_AddStringToObject(go, "winner", game.players[0].username);
        }
        else if (bcount > rcount) {
            cJSON_AddStringToObject(go, "winner", game.players[1].username);
        }
        else {
            cJSON_AddStringToObject(go, "winner", "Draw");
        }

        send_json(game.players[0].socket, go);
        send_json(game.players[1].socket, go);
        cJSON_Delete(go);
    }
}

// ==============================
// (6) server_run: main.c에서 호출하는 진입점
// ==============================
int server_run(const char *port) {
    // 1) 소켓 생성 및 바인딩
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to create listening socket on port %s\n", port);
        return EXIT_FAILURE;
    }
    global_listen_fd = listen_fd;

    // 2) 클라이언트 2명까지 accept + register
    accept_and_register(listen_fd);

    // 3) 게임 루프 실행
    game_loop();

    // 4) 연결 종료 및 자원 정리
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

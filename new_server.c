#include "../include/client.h"   // client.h에 generate_move 프로토타입 있음
#include "../include/game.h"
#include "../libs/cJSON.h"
#include "../include/json.h"
// #include "../include/board.h"   // LED 관련 파일 삭제 (더 이상 필요 없음)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>

static GameState game;
static int global_listen_fd;

// 게임 상태 초기화
void init_game(GameState *game) {
    game->current_turn = 0; // Red's turn
    memset(game->board, '.', sizeof(game->board));
    game->board[0][0] = 'R';
    game->board[0][BOARD_SIZE - 1] = 'B';
    game->board[BOARD_SIZE - 1][0] = 'B';
    game->board[BOARD_SIZE - 1][BOARD_SIZE - 1] = 'R';
    for (int i = 0; i < MAX_CLIENTS; i++) {
        game->players[i].socket = -1;
        game->players[i].username[0] = '\0';
        game->players[i].color = (i == 0 ? 'R' : 'B');
        game->players[i].registered = 0;
    }
}

// JSON 브로드캐스트 함수 (서버가 클라이언트들에게 메시지 보낼 때)
void broadcast_json(const cJSON *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].socket >= 0) {
            send_json(game.players[i].socket, msg);
        }
    }
}

// 특정 클라이언트에게만 JSON 전송
void send_to_client(int sockfd, const cJSON *msg) {
    if (send_json(sockfd, msg) < 0) {
        perror("send_json");
    }
}

// GameState의 board를 JSON 배열로 변환
static cJSON *board_to_json(const GameState *game) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < BOARD_SIZE; i++) {
        cJSON *row = cJSON_CreateString(game->board[i]);
        cJSON_AddItemToArray(arr, row);
    }
    return arr;
}

static int create_listen_socket(const char *port) {
    struct addrinfo hints, *res, *p;
    int listen_fd, yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
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

static int accept_and_register(int listen_fd) {
    int registered = 0;
    // 클라이언트가 2명 등록될 때까지 accept→register 반복
    while (registered < MAX_CLIENTS) {
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&addr, &addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        // 이미 최대 인원이거나 잘못된 요청이면 즉시 NACK 보내기
        if (registered >= MAX_CLIENTS) {
            cJSON *nack = cJSON_CreateObject();
            cJSON_AddStringToObject(nack, "type", "register_nack");
            cJSON_AddStringToObject(nack, "reason", "game is already running");
            send_to_client(client_fd, nack);
            cJSON_Delete(nack);
            close(client_fd);
            continue;
        }

        // 클라이언트가 보낸 JSON(=register 요청) 읽기
        cJSON *req = recv_json(client_fd);
        if (!req) {
            close(client_fd);
            continue;
        }

        cJSON *jtype = cJSON_GetObjectItem(req, "type");
        cJSON *juser = cJSON_GetObjectItem(req, "username");
        cJSON *resp = NULL;

        // "type":"register", "username":"..." 여부 확인
        if (jtype && jtype->valuestring
            && strcmp(jtype->valuestring, "register") == 0
            && juser && juser->valuestring) {
            // 중복 검사
            int dup = 0;
            for (int i = 0; i < registered; i++) {
                if (strcmp(game.players[i].username, juser->valuestring) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                // 중복된 username → register_nack
                resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "register_nack");
                cJSON_AddStringToObject(resp, "reason", "username exists");
            } else {
                // 정상 등록
                game.players[registered].socket = client_fd;
                strncpy(game.players[registered].username,
                        juser->valuestring,
                        sizeof(game.players[0].username) - 1);
                game.players[registered].username
                    [sizeof(game.players[0].username) - 1] = '\0';
                game.players[registered].registered = 1;
                registered++;

                resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "register_ack");
            }
        } else {
            // 잘못된 register 요청
            resp = cJSON_CreateObject();
            cJSON_AddStringToObject(resp, "type", "register_nack");
            cJSON_AddStringToObject(resp, "reason", "invalid register");
        }
        send_to_client(client_fd, resp);
        cJSON_Delete(resp);
        cJSON_Delete(req);
    }
    return 0;
}

static void *reject_late_clients(void *arg) {
    (void)arg;
    while (1) {
        sleep(1);
        if (global_listen_fd < 0) break; // 서버가 중지되면 스레드 종료
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        int client_fd = accept(global_listen_fd, (struct sockaddr*)&addr, &addrlen);
        if (client_fd < 0) continue;
        cJSON *nack = cJSON_CreateObject();
        cJSON_AddStringToObject(nack, "type", "register_nack");
        cJSON_AddStringToObject(nack, "reason", "game is already running");
        send_to_client(client_fd, nack);
        cJSON_Delete(nack);
        close(client_fd);
    }
    return NULL;
}

static void game_loop(void) {
    // *** LED 호출 모두 삭제 ***
    // update_led_matrix(game.board);

    int countPass = 0;

    // --- game_start 메시지 보내기 ---
    cJSON *game_start = cJSON_CreateObject();
    cJSON_AddStringToObject(game_start, "type", "game_start");
    cJSON *players = cJSON_AddArrayToObject(game_start, "players");
    cJSON_AddItemToArray(players,
                        cJSON_CreateString(game.players[0].username));
    cJSON_AddItemToArray(players,
                        cJSON_CreateString(game.players[1].username));
    cJSON_AddStringToObject(game_start,
                            "first_player", game.players[0].username);
    broadcast_json(game_start);
    cJSON_Delete(game_start);

    int turn = 0; // 0: Red, 1: Blue

    while (!isGameOver(game.board)) {
        // 1) your_turn 메시지 전송
        cJSON *your_turn = cJSON_CreateObject();
        cJSON_AddStringToObject(your_turn, "type", "your_turn");
        cJSON_AddItemToObject(your_turn, "board",
                              board_to_json(&game));
        cJSON_AddNumberToObject(your_turn, "timeout", TIMEOUT);
        send_to_client(game.players[turn].socket, your_turn);
        cJSON_Delete(your_turn);

        // 2) select()로 클라이언트 응답 대기
        int client_fd = game.players[turn].socket;
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = TIMEOUT; // TIMEOUT 초 대기
        tv.tv_usec = 0;

        int sel = select(client_fd + 1, &readfds, NULL, NULL, &tv);

        if (sel < 0) {
            perror("select");
            break;
        }

        if (sel == 0) {
            // 시간 초과 → 패스 처리
            cJSON *resp = cJSON_CreateObject();
            countPass++;
            cJSON_AddStringToObject(resp, "type", "pass");
            cJSON_AddItemToObject(resp, "board",
                                  board_to_json(&game));
            cJSON_AddStringToObject(resp, "next_player",
                                    game.players[1 - turn].username);
            broadcast_json(resp);
            cJSON_Delete(resp);

            if (countPass == 2) break; // 양쪽 다 패스 → 게임 종료
            if (isGameOver(game.board)) break;
            turn = 1 - turn;
            continue;
        }

        // sel > 0: 읽을 데이터가 있다는 의미
        cJSON *req = recv_json(client_fd);
        if (!req) {
            // 연결 끊김 또는 파싱 에러
            break;
        }

        cJSON *jtype = cJSON_GetObjectItem(req, "type");
        cJSON *resp = cJSON_CreateObject();

        if (jtype && strcmp(jtype->valuestring, "move") == 0) {
            countPass = 0;  // 패스 카운트 초기화
            int r1 = cJSON_GetObjectItem(req, "sx")->valueint - 1;
            int c1 = cJSON_GetObjectItem(req, "sy")->valueint - 1;
            int r2 = cJSON_GetObjectItem(req, "tx")->valueint - 1;
            int c2 = cJSON_GetObjectItem(req, "ty")->valueint - 1;

            // (0,0,0,0)이 넘어오면 “진짜 패스”가 아닌 invalid_move 판정
            if (r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1) {
                if (hasValidMove(game.board,
                                 game.players[turn].color)) {
                    cJSON_AddStringToObject(resp, "type", "invalid_move");
                } else {
                    countPass++;
                    cJSON_AddStringToObject(resp, "type", "pass");
                    cJSON_AddItemToObject(resp, "board",
                                          board_to_json(&game));
                    cJSON_AddStringToObject(resp, "next_player",
                                            game.players[1 - turn].username);
                    broadcast_json(resp);
                    cJSON_Delete(resp);
                    cJSON_Delete(req);

                    if (countPass == 2) break;
                    if (isGameOver(game.board)) break;
                    turn = 1 - turn;
                    continue;
                }
            }
            else if (isValidInput(game.board,
                                  r1, c1, r2, c2) &&
                     isValidMove(game.board,
                                 game.players[turn].color,
                                 r1, c1, r2, c2)) {
                Move(game.board, turn, r1, c1, r2, c2);
                // *** LED 호출 삭제 ***
                // update_led_matrix(game.board);
                cJSON_AddStringToObject(resp, "type", "move_ok");
                turn = 1 - turn;
            } else {
                cJSON_AddStringToObject(resp, "type", "invalid_move");
            }
        }
        else {
            // move가 아닌 잘못된 요청은 무시
            cJSON_Delete(resp);
            cJSON_Delete(req);
            continue;
        }

        // move_ok or invalid_move일 때 board와 next_player 추가 후 브로드캐스트
        cJSON_AddItemToObject(resp, "board",
                              board_to_json(&game));
        cJSON_AddStringToObject(resp, "next_player",
                                game.players[1 - turn].username);
        broadcast_json(resp);
        cJSON_Delete(resp);
        cJSON_Delete(req);
    }

    // 게임 종료 처리
    cJSON *over = cJSON_CreateObject();
    cJSON_AddStringToObject(over, "type", "game_over");
    cJSON *final_board = board_to_json(&game);
    cJSON_AddItemToObject(over, "board", final_board);
    cJSON *scores = cJSON_CreateObject();
    cJSON_AddNumberToObject(scores, game.players[0].username,
                            countR(game.board));
    cJSON_AddNumberToObject(scores, game.players[1].username,
                            countB(game.board));
    cJSON_AddItemToObject(over, "scores", scores);
    broadcast_json(over);
    cJSON_Delete(over);
}

int server_run(const char *port) {
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create listen socket on port %s\n", port);
        return EXIT_FAILURE;
    }
    printf("Server started on port %s\n", port);
    global_listen_fd = listen_fd;
    init_game(&game);
    // *** LED 호출 삭제 ***
    // update_led_matrix(game.board);

    accept_and_register(listen_fd);
    pthread_t reject_thread;
    pthread_create(&reject_thread, NULL, reject_late_clients, NULL);
    game_loop();

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game.players[i].socket >= 0) {
            close(game.players[i].socket);
        }
    }
    close(listen_fd);
    global_listen_fd = -1; // 서버 중지 표시
    printf("Server stopped.\n");
    return EXIT_SUCCESS;
}

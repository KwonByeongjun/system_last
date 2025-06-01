// src/server.c

#include "../include/server.h"
#include "../include/game.h"
#include "../include/json.h"

#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>   // close()

static GameState game;
static int global_listen_fd;

// 내부 함수들 선언 (game_loop, accept_and_register 등은 game.h/json.h에 정의되어 있다고 가정)
void init_game(GameState *game);
void broadcast_json(const cJSON *msg);
void send_to_client(int sockfd, const cJSON *msg);
static cJSON *board_to_json(const GameState *game);
static int create_listen_socket(const char *port);
static void *reject_late_clients(void *arg);
static int accept_and_register(int listen_fd);
static void game_loop(void);

// 서버 모드 진입 함수
int server_run(const char *port) {
    // 1) 소켓 생성 및 바인딩
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: Failed to create listening socket on port %s\n", port);
        return EXIT_FAILURE;
    }
    global_listen_fd = listen_fd;

    // 2) 최대 2명의 클라이언트 등록
    accept_and_register(listen_fd);

    // 3) 게임 상태 초기화
    init_game(&game);

    // 4) 메인 게임 루프
    game_loop();

    // 5) 게임 종료 시 모든 소켓 닫기
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

// … 이하에 accept_and_register, game_loop, broadcast_json 등 실제 구현이 이어진다고 가정

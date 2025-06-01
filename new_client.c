#include "../include/client.h"
#include "../include/game.h"
#include "../include/json.h"
#include "../include/board.h"

#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // close()
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define BOARD_SIZE 8

// 전역 board 배열
static char board[BOARD_SIZE][BOARD_SIZE];

/*
  client.h 에는 다음과 같이 선언만 되어 있었음:
    static int connect_to_server(const char *ip, const char *port);

  아래에 실제로 구현을 추가하여 “used but never defined” 문제 해결합니다.
*/
int connect_to_server(const char *ip, const char *port) {
    struct addrinfo hints, *res, *p;
    int sockfd;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP

    if ((rv = getaddrinfo(ip, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to connect to %s:%s\n", ip, port);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sockfd;
}

// ==============================
// (1) JSON → board[8][8] 변환
// ==============================
void parse_board_from_json(cJSON *root, char board[BOARD_SIZE][BOARD_SIZE]) {
    cJSON *board_arr = cJSON_GetObjectItem(root, "board");
    if (!board_arr || !cJSON_IsArray(board_arr)) return;
    for (int r = 0; r < BOARD_SIZE; r++) {
        cJSON *row = cJSON_GetArrayItem(board_arr, r);
        if (!row || !cJSON_IsString(row)) continue;
        const char *s = row->valuestring;
        for (int c = 0; c < BOARD_SIZE; c++) {
            board[r][c] = s[c];
        }
    }
}

// ==============================
// (2) 터미널(stdout)으로 보드 출력 (디버그용)
// ==============================
void print_board_to_stdout(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            putchar(board[r][c]);
        }
        putchar('\n');
    }
    putchar('\n');
}

// ==============================
// (3) client_run: main.c에서 호출되는 진입점
// ==============================
int client_run(const char *server_ip, const char *server_port, const char *username) {
    // ─── LED 초기화 ─────────────────────────────────────────────────────────
    // main.c에서 이미 init_led_matrix(&led_argc,&led_argv)를 호출했으므로,
    // 여기서는 기본으로 초기화만 수행합니다.
    if (init_led_matrix(NULL, NULL) < 0) {
        fprintf(stderr, "Error: LED initialization failed.\n");
        return EXIT_FAILURE;
    }

    // ─── 서버 연결 ───────────────────────────────────────────────────────────
    int sockfd = connect_to_server(server_ip, server_port);
    if (sockfd < 0) {
        fprintf(stderr, "Error: Failed to connect to %s:%s\n", server_ip, server_port);
        close_led_matrix();
        return EXIT_FAILURE;
    }

    // ─── JSON 메시지 처리 루프 ───────────────────────────────────────────────────
    while (1) {
        cJSON *msg = recv_json(sockfd);
        if (!msg) {
            // 서버 연결 종료 또는 오류
            break;
        }
        cJSON *jtype = cJSON_GetObjectItem(msg, "type");
        if (!jtype || !jtype->valuestring) {
            cJSON_Delete(msg);
            continue;
        }
        const char *type = jtype->valuestring;

        if (strcmp(type, "register_ack") == 0) {
            printf("Registered as %s\n", username);
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "register_nack") == 0) {
            cJSON *jreason = cJSON_GetObjectItem(msg, "reason");
            if (jreason && jreason->valuestring) {
                printf("Register failed: %s\n", jreason->valuestring);
            } else {
                printf("Register failed (unknown reason)\n");
            }
            cJSON_Delete(msg);
            break;
        }
        else if (strcmp(type, "game_start") == 0) {
            printf("Game started\n");
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "your_turn") == 0) {
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);

            // generate_move: game.h에 정의
            int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
            char my_color = board[0][0];  // 예: 첫 칸으로 R/B를 판단
            generate_move(board, my_color, &r1, &c1, &r2, &c2);

            // move 메시지 생성
            cJSON *move_req = cJSON_CreateObject();
            cJSON_AddStringToObject(move_req, "type", "move");

            // 【수정】 임시 배열을 미리 선언하여 cJSON_CreateIntArray에 넘김
            int from_arr[2] = { r1, c1 };
            int to_arr[2]   = { r2, c2 };
            cJSON *jfrom = cJSON_CreateIntArray(from_arr, 2);
            cJSON *jto   = cJSON_CreateIntArray(to_arr,   2);
            cJSON_AddItemToObject(move_req, "from", jfrom);
            cJSON_AddItemToObject(move_req, "to",   jto);
            send_json(sockfd, move_req);
            cJSON_Delete(move_req);

            cJSON_Delete(msg);
        }
        else if (strcmp(type, "move_ok") == 0) {
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "invalid_move") == 0) {
            printf("Invalid move!\n");
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "pass") == 0) {
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            printf("Passed.\n");
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "game_over") == 0) {
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);

            cJSON *winner_item = cJSON_GetObjectItem(msg, "winner");
            if (winner_item && winner_item->valuestring) {
                printf("Game Over! Winner: %s\n", winner_item->valuestring);
            } else {
                printf("Game Over! (No winner field)\n");
            }
            cJSON_Delete(msg);
            break;
        }
        else {
            cJSON_Delete(msg);
        }
    }

    // ─── 종료: 소켓 닫기 ─────────────────────────────────────────────────────
    close(sockfd);

    // ─── LED 매트릭스 해제 ─────────────────────────────────────────────────────
    close_led_matrix();
    return EXIT_SUCCESS;
}

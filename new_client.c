// src/client.c

#include "../include/client.h"
#include "../include/game.h"
#include "../include/json.h"
#include "../include/board.h"

#include "../libs/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>    // close()

#define BOARD_SIZE 8

// 전역 board 배열
static char board[BOARD_SIZE][BOARD_SIZE];

// JSON → board 배열 변환 함수
void parse_board_from_json(cJSON *root, char board[BOARD_SIZE][BOARD_SIZE]) {
    cJSON *board_arr = cJSON_GetObjectItem(root, "board");
    for (int r = 0; r < BOARD_SIZE; r++) {
        const char *row = cJSON_GetArrayItem(board_arr, r)->valuestring;
        for (int c = 0; c < BOARD_SIZE; c++) {
            board[r][c] = row[c];
        }
    }
}

// 터미널(stdout)으로 보드 출력 (디버그용)
void print_board_to_stdout(char board[BOARD_SIZE][BOARD_SIZE]) {
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            putchar(board[r][c]);
        }
        putchar('\n');
    }
    putchar('\n');
}

// 클라이언트 메시지 처리 루프
int client_run(const char *server_ip, const char *server_port, const char *username) {
    // 1) LED 매트릭스 초기화 (main.c에서 이미 옵션 전달됨)
    if (init_led_matrix(NULL, NULL) < 0) {
        fprintf(stderr, "Error: LED initialization failed.\n");
        return EXIT_FAILURE;
    }

    // 2) TA 서버로 연결
    int sockfd = connect_to_server(server_ip, server_port, username);
    if (sockfd < 0) {
        fprintf(stderr, "Error: Failed to connect to %s:%s\n", server_ip, server_port);
        close_led_matrix();
        return EXIT_FAILURE;
    }

    // 3) JSON 메시지 처리
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
            // 예: 등록 성공
            printf("Registered: %s\n", username);
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "register_nack") == 0) {
            // 예: 등록 실패
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
            cJSON *jplayers = cJSON_GetObjectItem(msg, "players");
            char my_color = 'R';
            if (jplayers && cJSON_IsArray(jplayers)) {
                const cJSON *p0 = cJSON_GetArrayItem(jplayers, 0);
                if (p0 && p0->valuestring && strcmp(username, p0->valuestring) != 0) {
                    my_color = 'B';
                }
            }
            // board 정보가 있는 경우 갱신
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "your_turn") == 0) {
            // (가) 보드 갱신
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);

            // (나) 이동 결정 (generate_move는 game.h에 정의된 함수)
            int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
            generate_move(board, /*player_color*/ board[0][0], &r1, &c1, &r2, &c2);
            send_move(sockfd, r1, c1, r2, c2);
        }
        else if (strcmp(type, "move_ok") == 0) {
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);
        }
        else if (strcmp(type, "invalid_move") == 0) {
            fprintf(stderr, "Invalid move!\n");
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
        }
        else if (strcmp(type, "pass") == 0) {
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            fprintf(stderr, "You must pass.\n");
        }
        else if (strcmp(type, "game_over") == 0) {
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);

            cJSON *winner_item = cJSON_GetObjectItem(msg, "winner");
            if (winner_item && winner_item->valuestring) {
                printf("Game Over! Winner: %s\n", winner_item->valuestring);
            }
            cJSON_Delete(msg);
            break;
        }

        cJSON_Delete(msg);
    }

    // 4) 소켓 닫기
    close(sockfd);

    // 5) LED 매트릭스 자원 해제
    close_led_matrix();
    return EXIT_SUCCESS;
}

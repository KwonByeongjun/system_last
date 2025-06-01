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

// ==============================
// (1) JSON → board[8][8]로 변환
//     TA 서버(또는 로컬 서버)가 "board"라는 키에 “8줄×8문자” 배열을 보낸다고 가정
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
// (3) client_run: main.c에서 호출하는 진입점
//     - init_led_matrix() 호출
//     - TA 서버(또는 로컬 서버)와 JSON 메시지 송수신
//     - update_led_matrix() 호출 (각 메시지 타입마다 보드 갱신)
//     - close_led_matrix() 호출
// ==============================
int client_run(const char *server_ip, const char *server_port, const char *username) {
    // ─── LED 초기화 ─────────────────────────────────────────────────────────
    // main.c에서 이미 init_led_matrix(&led_argc, &led_argv)를 호출해 두었기 때문에,
    // 여기서는 단순히 기본값으로 초기화합니다.
    if (init_led_matrix(NULL, NULL) < 0) {
        fprintf(stderr, "Error: LED initialization failed.\n");
        return EXIT_FAILURE;
    }

    // ─── 서버 연결 ───────────────────────────────────────────────────────────
    int sockfd = connect_to_server(server_ip, server_port, username);
    if (sockfd < 0) {
        fprintf(stderr, "Error: Failed to connect to %s:%s\n", server_ip, server_port);
        close_led_matrix();
        return EXIT_FAILURE;
    }

    // ─── 메시지 처리 루프 ────────────────────────────────────────────────────
    while (1) {
        // (a) 서버로부터 JSON 메시지 수신
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

        // (b) 메시지 종류에 따른 처리
        if (strcmp(type, "register_ack") == 0) {
            // 서버가 보낸 첫 번째 메시지: 등록 성공
            printf("Registered as %s\n", username);
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "register_nack") == 0) {
            // 등록 실패 → 프로그램 종료
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
            // 게임 시작 시 초기 보드 수신
            printf("Game started\n");
            parse_board_from_json(msg, board);
            update_led_matrix(board);      // LED에 초기 상태 표시
            print_board_to_stdout(board);  // 터미널에도 출력
            cJSON_Delete(msg);
            continue;
        }
        else if (strcmp(type, "your_turn") == 0) {
            // 내 턴: 보드 상태 수신 → LED 갱신 → 콘솔 출력 → 수 두기
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);

            // generate_move: game.h에 정의된 함수 (전략에 따라 R→B 두 플레이어 모두 사용 가능)
            int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
            // 현재 내 색깔은, 서버가 게임 시작 시 보내준 순서에 따라 결정됨
            // 간단히 “generate_move”가 board 첫 칸(board[0][0])을 보고 R/B를 판단
            char my_color = board[0][0]; 
            generate_move(board, my_color, &r1, &c1, &r2, &c2);

            // “move” 메시지 작성: { "type":"move", "from":[r1,c1], "to":[r2,c2] }
            cJSON *move_req = cJSON_CreateObject();
            cJSON_AddStringToObject(move_req, "type", "move");
            cJSON *jfrom = cJSON_CreateIntArray((int[]){r1, c1}, 2);
            cJSON *jto   = cJSON_CreateIntArray((int[]){r2, c2}, 2);
            cJSON_AddItemToObject(move_req, "from", jfrom);
            cJSON_AddItemToObject(move_req, "to",   jto);
            send_json(sockfd, move_req);
            cJSON_Delete(move_req);

            cJSON_Delete(msg);
        }
        else if (strcmp(type, "move_ok") == 0) {
            // 이동 성공 → 서버가 갱신된 보드를 보내준다
            parse_board_from_json(msg, board);
            update_led_matrix(board);
            print_board_to_stdout(board);
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "invalid_move") == 0) {
            // 잘못된 수 → “invalid_move”만 띄우고 보드가 다시 있으면 갱신
            printf("Invalid move!\n");
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "pass") == 0) {
            // 패스 처리 → 서버가 다시 보드를 보내주면 갱신
            if (cJSON_GetObjectItem(msg, "board")) {
                parse_board_from_json(msg, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
            printf("Passed.\n");
            cJSON_Delete(msg);
        }
        else if (strcmp(type, "game_over") == 0) {
            // 게임 종료 → 최종 보드 수신 → LED 갱신 → 콘솔에 승패 표시 후 루프 탈출
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
            // 알 수 없는 메시지
            cJSON_Delete(msg);
        }
    }

    // ─── 소켓 닫기 ─────────────────────────────────────────────────────────
    close(sockfd);

    // ─── LED 자원 해제 ───────────────────────────────────────────────────────
    close_led_matrix();
    return EXIT_SUCCESS;
}

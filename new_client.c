// src/client.c

#include "client.h"
#include "board.h"      // ★ board 라이브러리 헤더 추가
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"
#include "game.h"

// (1) 전역 board 배열
static char board[8][8];

// (2) JSON → board[8][8] 변환 함수 (TA 서버 포맷에 맞게 수정할 수도 있음)
void parse_board_from_json(cJSON *root, char board[8][8]) {
    // ※ TA 서버가 "board"라는 이름으로 문자열 배열을 준다고 가정
    cJSON *board_arr = cJSON_GetObjectItem(root, "board");
    for (int r = 0; r < 8; r++) {
        const char *row = cJSON_GetArrayItem(board_arr, r)->valuestring;
        for (int c = 0; c < 8; c++) {
            board[r][c] = row[c];
        }
    }
}

// (3) 터미널에 보드 출력 함수 (변경 없음)
void print_board_to_stdout(char board[8][8]) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            putchar(board[r][c]);
        }
        putchar('\n');
    }
    putchar('\n');
}

// (4) 메시지 처리 루프
void client_run(const char *server_ip, const char *server_port, const char *username) {
    int sockfd = connect_to_server(server_ip, server_port, username);
    if (sockfd < 0) {
        fprintf(stderr, "서버 연결 실패\n");
        return;
    }

    while (1) {
        char *msg = recv_json(sockfd);
        if (!msg) break;
        cJSON *root = cJSON_Parse(msg);
        const char *type = cJSON_GetObjectItem(root, "type")->valuestring;

        if (strcmp(type, "game_start") == 0) {
            // (A) 초기 보드
            parse_board_from_json(root, board);

            // ★ (중요) LED에 보드 갱신
            update_led_matrix(board);
            // (선택) 터미널에도 출력
            print_board_to_stdout(board);
        }
        else if (strcmp(type, "your_turn") == 0) {
            // (B) 내 턴 전 보드
            parse_board_from_json(root, board);

            // ★ (중요) LED 갱신
            update_led_matrix(board);
            print_board_to_stdout(board);

            // (AI 로직) move 결정 → 서버에 전송
            int r, c;
            decide_move(board, &r, &c);
            send_move(sockfd, r, c);
        }
        else if (strcmp(type, "move_ok") == 0) {
            // (C) 이동이 성공적으로 처리된 뒤 보드
            parse_board_from_json(root, board);

            // ★ (중요) LED 갱신
            update_led_matrix(board);
            print_board_to_stdout(board);
        }
        else if (strcmp(type, "invalid_move") == 0) {
            fprintf(stderr, "Invalid move\n");
            // (필요하다면) TA 서버가 보드를 같이 보내준다 하면 갱신 가능
            if (cJSON_GetObjectItem(root, "board") != NULL) {
                parse_board_from_json(root, board);
                update_led_matrix(board);
                print_board_to_stdout(board);
            }
        }
        else if (strcmp(type, "game_over") == 0) {
            // (D) 최종 보드
            parse_board_from_json(root, board);

            // ★ (중요) LED 갱신
            update_led_matrix(board);
            print_board_to_stdout(board);

            // (옵션) 승패 결과 출력
            if (cJSON_GetObjectItem(root, "winner") != NULL) {
                const char *winner = cJSON_GetObjectItem(root, "winner")->valuestring;
                printf("게임 종료! 승자는 %s\n", winner);
            }

            cJSON_Delete(root);
            free(msg);
            break;
        }

        cJSON_Delete(root);
        free(msg);
    }

    close(sockfd);
}

// (5) 수정된 main(): LED 초기화/종료를 추가
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <TA서버-IP> <포트> <닉네임> [LED 옵션]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // ★ (중요) LED 매트릭스 초기화
    if (init_led_matrix(&argc, &argv) < 0) {
        fprintf(stderr, "LED 초기화 실패\n");
        return EXIT_FAILURE;
    }

    // ★ 게임 진행 (TA 서버와 통신하며 위에서 update_led_matrix()를 호출)
    client_run(argv[1], argv[2], argv[3]);

    // ★ (중요) 프로그램 종료 전 LED 자원 해제
    close_led_matrix();
    return 0;
}

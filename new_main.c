#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "client.h"
#include "board.h"

// 사용법 안내
void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s server -p <port>\n", prog);
    printf("  %s client -i <ip> -p <port> -u <username>\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // ============================================================
    // 1) "server" 모드: LED 초기화/종료 코드를 모두 제거하고
    //    오직 서버 로직(server_run)만 실행
    // ============================================================
    if (strcmp(argv[1], "server") == 0) {
        int port = 8080;  // 기본 포트
        char port_str[16];

        // -p <port> 파싱
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            }
        }
        snprintf(port_str, sizeof(port_str), "%d", port);

        // *** 여기서 LED 초기화/종료 코드는 전부 제거 ***
        // if (init_led_matrix(&argc, &argv) < 0) {
        //     fprintf(stderr, "Failed to initialize LED Matrix.\n");
        //     return EXIT_FAILURE;
        // }

        int ret = server_run(port_str);

        // close_led_matrix();  // ← 삭제 or 주석 처리
        return ret;
    }

    // ============================================================
    // 2) "client" 모드: 클라이언트만 LED를 초기화/종료할 수 있도록 추가
    // ============================================================
    else if (strcmp(argv[1], "client") == 0) {
        char *ip = NULL;
        int port = 8080;
        char port_str[16];
        char *username = NULL;

        // -i <ip> / -p <port> / -u <username> 파싱
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
                ip = argv[++i];
            } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
                username = argv[++i];
            }
        }
        if (!ip || !username) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        snprintf(port_str, sizeof(port_str), "%d", port);

        // ────────────────────────────────────────────────────────
        // 클라이언트 모드에서도 LED를 초기화하도록 호출
        if (init_led_matrix(&argc, &argv) < 0) {
            // 로컬에 LED 하드웨어가 없거나 권한이 없으면 경고만 출력하고 계속 진행
            fprintf(stderr, "[Client Warning] LED Matrix 초기화 실패 (로컬 하드웨어 없음 혹은 권한 부족)\n");
        }

        // TA 서버 혹은 로컬 서버와 통신하며, board를 받을 때마다 update_led_matrix()를 client_run() 내부에서 호출
        int ret = client_run(ip, port_str, username);

        // 클라이언트 종료 시 LED 닫기
        close_led_matrix();
        return ret;
    }

    // ============================================================
    // 3) 그 외 잘못된 실행법
    // ============================================================
    else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

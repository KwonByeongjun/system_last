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

    // ====== 1) SERVER 모드 ======
    if (strcmp(argv[1], "server") == 0) {
        int port = 8080;
        char port_str[16];

        // -p <port> 파싱
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port = atoi(argv[++i]);
            }
        }
        snprintf(port_str, sizeof(port_str), "%d", port);

        // *** 서버에서는 LED 함수 호출 전혀 없음 ***
        // init_led_matrix(&argc, &argv);
        int ret = server_run(port_str);
        // close_led_matrix();
        return ret;
    }

    // ====== 2) CLIENT 모드 ======
    else if (strcmp(argv[1], "client") == 0) {
        char *ip = NULL;
        int port = 8080;
        char port_str[16];
        char *username = NULL;

        // -i <ip>, -p <port>, -u <username> 파싱
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

        // ▶ 클라이언트만 LED를 초기화/종료하도록 호출
        if (init_led_matrix(&argc, &argv) < 0) {
            fprintf(stderr, "[Client Warning] LED 초기화 실패 (로컬 하드웨어 없음 또는 권한 부족)\n");
            // init 실패해도 클라이언트 로직은 계속 실행
        }

        int ret = client_run(ip, port_str, username);

        close_led_matrix();
        return ret;
    }

    // ====== 3) 그 외 ======
    else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

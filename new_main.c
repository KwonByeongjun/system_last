#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
#include "client.h"
#include "board.h"

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

    if (strcmp(argv[1], "server") == 0) {
        // ---- SERVER 모드 ----
        // LED 초기화/해제 절대 금지 (서버는 순수 게임 로직만 수행)
        int port = 8080;  // 기본값, 실제로는 인자 파싱 필요
        char port_str[16];
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
                port = atoi(argv[++i]);
        }
        snprintf(port_str, sizeof(port_str), "%d", port);

        return server_run(port_str);

    } else if (strcmp(argv[1], "client") == 0) {
        // ---- CLIENT 모드 ----
        // 클라이언트에서만 LED 초기화/제어/해제
        char *ip = NULL;
        int port = 8080;
        char port_str[16];
        char *username = NULL;

        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "-i") == 0 && i + 1 < argc)
                ip = argv[++i];
            else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
                port = atoi(argv[++i]);
            else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
                username = argv[++i];
        }
        if (!ip || !username) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        snprintf(port_str, sizeof(port_str), "%d", port);

        // LED 매트릭스 초기화 (클라이언트에서만)
        if (init_led_matrix(&argc, &argv) < 0) {
            fprintf(stderr, "Failed to initialize LED Matrix.\n");
            return EXIT_FAILURE;
        }

        int ret = client_run(ip, port_str, username);

        // LED 매트릭스 해제
        close_led_matrix();
        return ret;

    } else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

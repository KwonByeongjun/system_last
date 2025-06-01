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
        // ----- SERVER 모드: LED 초기화/해제 전부 제거 -----
        const char *port_str = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                port_str = argv[i + 1];
                break;
            }
        }
        if (!port_str) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        return server_run(port_str);

    } else if (strcmp(argv[1], "client") == 0) {
        // ----- CLIENT 모드: LED 초기화/해제는 여기서만 -----
        const char *server_ip = NULL;
        const char *server_port = NULL;
        const char *username = NULL;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
                server_ip = argv[++i];
            } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
                server_port = argv[++i];
            } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
                username = argv[++i];
            }
        }
        if (!server_ip || !server_port || !username) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        // LED 매트릭스 초기화 (클라이언트에서만)
        if (init_led_matrix(&argc, &argv) < 0) {
            fprintf(stderr, "Failed to initialize LED matrix\n");
            return EXIT_FAILURE;
        }

        int ret = client_run(server_ip, server_port, username);

        // LED 매트릭스 해제
        close_led_matrix();
        return ret;

    } else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

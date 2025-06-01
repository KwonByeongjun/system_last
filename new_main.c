// src/main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/server.h"
#include "../include/client.h"
#include "../include/board.h"

// 사용법 출력
static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s server -p <port>\n", prog);
    printf("  %s client -i <ip> -p <port> -u <username> [LED options]\n\n", prog);
    printf("LED options (client 모드일 때만):\n");
    printf("  --led-rows=<rows>                (예: --led-rows=64)\n");
    printf("  --led-cols=<cols>                (예: --led-cols=64)\n");
    printf("  --led-gpio-mapping=<mapping>     (예: --led-gpio-mapping=adafruit-hat)\n");
    printf("  --led-brightness=<value>         (예: --led-brightness=75)\n\n");
    printf("Examples:\n");
    printf("  %s server -p 9000\n", prog);
    printf("  %s client -i 10.0.0.5 -p 9000 -u Alice \\\n"
           "      --led-rows=64 --led-cols=64 --led-gpio-mapping=adafruit-hat --led-brightness=75\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // 첫 번째 인자가 "server"인지 "client"인지 판단
    if (strcmp(argv[1], "server") == 0) {
        // ----- Server 모드 -----
        // 최소 인자: main server -p <port>
        if (argc < 4 || strcmp(argv[2], "-p") != 0) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        const char *port_str = argv[3];
        return server_run(port_str);
    }
    else if (strcmp(argv[1], "client") == 0) {
        // ----- Client 모드 (LED 제어 포함) -----
        // 최소 인자: main client -i <ip> -p <port> -u <username>
        if (argc < 8) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        // 인자 파싱: "-i <ip>" "-p <port>" "-u <username>"
        const char *server_ip = NULL;
        const char *server_port = NULL;
        const char *username = NULL;
        int idx = 2;
        while (idx < argc && argv[idx][0] == '-') {
            if (strcmp(argv[idx], "-i") == 0 && idx + 1 < argc) {
                server_ip = argv[idx + 1];
                idx += 2;
            }
            else if (strcmp(argv[idx], "-p") == 0 && idx + 1 < argc) {
                server_port = argv[idx + 1];
                idx += 2;
            }
            else if (strcmp(argv[idx], "-u") == 0 && idx + 1 < argc) {
                username = argv[idx + 1];
                idx += 2;
            }
            else {
                // LED 옵션 시작 지점
                break;
            }
        }

        if (!server_ip || !server_port || !username) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        // 남은 argv[idx..argc-1]은 모두 LED 관련 옵션으로 간주하여 init_led_matrix에 넘김
        int led_argc = argc - idx;
        char **led_argv = NULL;
        if (led_argc > 0) {
            led_argv = malloc(sizeof(char *) * led_argc);
            for (int i = 0; i < led_argc; i++) {
                led_argv[i] = argv[idx + i];
            }
        }

        // *** LED 매트릭스 초기화 ***
        // 내부적으로 여기서 전달된 led_argc, led_argv를 바탕으로
        // e.g. --led-rows=64 --led-cols=64 --led-gpio-mapping=adafruit-hat --led-brightness=75
        // 등이 board.c에 있는 init_led_matrix 함수로 넘어갑니다.
        if (init_led_matrix(&led_argc, &led_argv) < 0) {
            fprintf(stderr, "Error: LED matrix initialization failed.\n");
            free(led_argv);
            return EXIT_FAILURE;
        }
        free(led_argv);

        // *** TA 서버 및 로컬 서버 대응용 게임 진행 ***
        int ret = client_run(server_ip, server_port, username);

        // *** LED 매트릭스 자원 해제 ***
        close_led_matrix();
        return ret;
    }
    else {
        // "server"나 "client" 이외의 첫 번째 인자가 들어왔을 경우
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

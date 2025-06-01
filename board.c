// src/board.c

#include "../libs/rpi-rgb-led-matrix/include/led-matrix-c.h"
#include "../include/board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static struct RGBLedMatrix *matrix = NULL;
static volatile sig_atomic_t local_interrupted = 0;

static void LocalInterruptHandler(int signo) {
    (void)signo;
    local_interrupted = 1;
}

static void draw_grid(struct LedCanvas *canvas) {
    for (int i = 0; i < 64; ++i) {
        if (i % 8 == 0 || i == 63) {
            for (int j = 0; j < 64; ++j) {
                led_canvas_set_pixel(canvas, j, i, 50, 50, 50);
                led_canvas_set_pixel(canvas, i, j, 50, 50, 50);
            }
        }
    }
}

static void draw_piece(struct LedCanvas *canvas, int row, int col, char piece_type) {
    
    int start_x = col * 8 + 1;
    int start_y = row * 8 + 1;
    int end_x = start_x + 6;
    int end_y = start_y + 6;
    uint8_t r = 0, g = 0, b = 0;

    printf("draw_pieces: row=%d col=%d type=%c\n, canvas=%p\n", row, col, piece_type, canvas);

    if (piece_type == 'R') { r = 255; }
    else if (piece_type == 'B') { b = 255; }
    else { return; }

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            led_canvas_set_pixel(canvas, x, y, r, g, b);
        }
    }

}

// → 시그니처를 반드시 아래처럼 수정합니다.
int init_led_matrix(int *argc, char ***argv) {
    struct RGBLedMatrixOptions options;
    memset(&options, 0, sizeof(options));
    options.rows = 64;
    options.cols = 64;
    options.hardware_mapping = "adafruit-hat";

    // (중요) &options, argc, argv 순으로 전달해야 합니다.
    matrix = led_matrix_create_from_options(&options, argc, argv);
    if (matrix == NULL) {
        fprintf(stderr, "Error: Could not create LED matrix. Run with sudo?\n");
        return -1;
    }

    struct LedCanvas *canvas = led_matrix_get_canvas(matrix);


    led_canvas_clear(canvas);
    draw_grid(canvas);
    led_matrix_swap_on_vsync(matrix, canvas);

    printf("LED Matrix Initialized.\n");
    return 0;
}

void update_led_matrix(const char board[8][8]) {
    if (!matrix) return;
    struct LedCanvas *canvas = led_matrix_get_canvas(matrix);
    led_canvas_clear(canvas);
    draw_grid(canvas);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (board[r][c] == 'R' || board[r][c] == 'B') {
                draw_piece(canvas, r, c, board[r][c]);
            }
        }
    }
    led_matrix_swap_on_vsync(matrix, canvas);
}

void close_led_matrix(void) {
    if (matrix) {
        led_canvas_clear(led_matrix_get_canvas(matrix));
        led_matrix_delete(matrix);
        matrix = NULL;
        printf("LED Matrix closed.\n");
    }
}

void local_led_test(void) {
    if (!matrix) {
        fprintf(stderr, "Error: Matrix not initialized for local test.\n");
        return;
    }

    struct sigaction sa;
    sa.sa_handler = LocalInterruptHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char local_board[8][9];
    printf("Enter 8 lines of 8 characters (R, B, ., or #) for local LED test:\n");
    for (int i = 0; i < 8; ++i) {
        printf("Line %d: ", i + 1);
        if (scanf("%8s", local_board[i]) != 1) {
            fprintf(stderr, "Error reading board input.\n");
            return;
        }
        int c; while ((c = getchar()) != '\n' && c != EOF);
    }

    printf("Board received. Displaying on LED Matrix. Press Ctrl+C to exit.\n");
    local_interrupted = 0;
    while (!local_interrupted) {
        update_led_matrix((const char (*)[8])local_board);
        usleep(100000);
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    printf("Local test finished.\n");
}

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <endian.h>

#include "config.h"
//#include "server.c"

#ifdef __MINGW32__
    #include <ncurses/ncurses.h>
#else 
    #include <ncurses.h>
#endif

#define PORT_NUMBER 5555
#define SERVER_IP "127.0.0.1"
#define CLIENT_IDENTIFIER "BOMBERMAN_CLIENT_v1"

#define BOARD_WIDTH 15
#define BOARD_HEIGHT 13

int sock;
uint8_t CLIENT_ID = 255;
int row = 1;
int col = 1;

char game_board[BOARD_HEIGHT][BOARD_WIDTH] = {
    "HHHHHHHHHHHHHHH",
    "H1..S.S.S.S.S.H",
    "H.H.HSHSHSHSHSH",
    "H.....S.S.S.S.H",
    "HSHSHSHSHSHSHSH",
    "H.S.S.S.S.S.S.H",
    "HSHSHSHSHSHSHSH",
    "H.S.S.S.S.S.S.H",
    "HSHSHSHSHSHSHSH",
    "H.S.S.S.S.S.S.H",
    "HSH.HSHSHSHSHSH",
    "H.S.S.S.S.S.S.H",
    "HHHHHHHHHHHHHHH"
};

bool is_move_legal(int row, int col, direction_t dir) {
    switch (dir)
    {
    case DIR_UP:
        if (row <= 1) return false;
        return (game_board[row-1][col] == '.');
    case DIR_DOWN:
        if (row >= BOARD_HEIGHT - 2) return false;
        return (game_board[row+1][col] == '.');
    case DIR_RIGHT:
        if (col >= BOARD_WIDTH - 2) return false;
        return (game_board[row][col+1] == '.');
    case DIR_LEFT:
        if (col <= 1) return false;
        return (game_board[row][col-1] == '.');
    default:
        break;
    }
}

void try_move(direction_t dir) {
    if (is_move_legal(row, col, dir)) {
        send_msg(sock, MSG_MOVE_ATTEMPT, 255, sizeof(direction_t), &dir);
    }
}

void render_board() {
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        for (int j = 0; j < BOARD_WIDTH; j++) {
            mvwaddch(stdscr, 5+i, 10 + j*2, game_board[i][j]);
        }
    }
    refresh();
}

void send_msg(int socket, msg_type_t type, uint8_t target_id, size_t payload_size, const void* payload) {
    size_t total_size = sizeof(msg_generic_t) + payload_size;
    uint8_t *buffer = malloc(total_size);
    if (!buffer) return;

    msg_generic_t message;
    message.msg_type = type;
    message.sender_id = CLIENT_ID;
    message.target_id = target_id;

    memcpy(buffer, &message, sizeof(message));
    if (payload && payload_size > 0) {
        memcpy(buffer + sizeof(message), payload, payload_size);
    }

    send(socket, buffer, total_size, 0);
    free(buffer);
}

int main() {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(PORT_NUMBER) };
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    printf("[Connecting to server...]\n");
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Connect failed");
        return 1;
    }

    hello_payload_t hello;
    strcpy(hello.identifier, CLIENT_IDENTIFIER);
    printf("Ievadiet vārdu - ");
    scanf("%s", hello.name);
    // Send HELLO
    send_msg(sock, MSG_HELLO, 255, sizeof(hello_payload_t), &hello);

    while (1) {
        
    }

    initscr();
    noecho();
    cbreak(); //accept inputs without pressing enter
    render_board();
    refresh();

    getch(); //wait for input

    endwin(); //clean up

    return 0;
}
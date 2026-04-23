#include <stdio.h>
#include <time.h>
#include "config.h"

//Must be odd
#define BOARD_WIDTH 15
#define BOARD_HEIGHT 13
#define WALLS_COUNT 50

void start_server() {
}

void receive_inputs() {
}

void send_gamestate() {
}

void update_game() {
}

/* Tiles: 
. = empty, 
H = unbreakable wall, 
S = breakable wall,
1..8 = players,
B = bomb,
A = speed up,
R = bomb radius up,
T = bomb time up
*/

void fill_board(char game_board[BOARD_HEIGHT][BOARD_WIDTH]) {
    int walls = 0;

    for (int a = 0; a < BOARD_HEIGHT; a++) {
        for (int b = 0; b < BOARD_WIDTH; b++) {
            // Fills unbreakable walls 
            if (a == 0 || a == BOARD_HEIGHT - 1 || 
                b == 0 || b == BOARD_WIDTH - 1 || 
                (a % 2 == 0 && b % 2 == 0))
                game_board[a][b] = 'H';
            else game_board[a][b] = '.';
        }
    }

    while (walls < WALLS_COUNT) {
        int a = rand() % BOARD_HEIGHT;
        int b = rand() % BOARD_WIDTH;
        // Skip 2x2 corners
        if ((a < 3 && b < 3) ||                        // top-left
            (a < 3 && b >= BOARD_WIDTH - 3) ||        // top-right
            (a >= BOARD_HEIGHT - 3 && b < 3) ||       // bottom-left
            (a >= BOARD_HEIGHT - 3 && b >= BOARD_WIDTH - 3)) // bottom-right
            {
            continue;
        }

        // Fill breakable walls
        if (game_board[a][b] == '.' || game_board[a][b] == 0) {
            game_board[a][b] = 'S';
            walls++;
        }

    }
}

int main() {

    
    //Seeds random number generator
    srand(time(NULL));

    char game_board[BOARD_HEIGHT][BOARD_WIDTH];
    fill_board(game_board);

    //print board
    for (int a = 0; a < BOARD_HEIGHT; a++) {
        for (int b = 0; b < BOARD_WIDTH; b++) {
            printf("%c ", game_board[a][b]);
        }
        printf("\n");
    }
    return 0;
}
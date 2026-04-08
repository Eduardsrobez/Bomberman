#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BOARD_WIDTH 15
#define BOARD_HEIGHT 13
#define WALLS_COUNT 55

// Tiles: 
// 0-empty, 
// 1-unbreakable wall, 
// 2-breakable wall, ...

void create_board(int game_board[BOARD_HEIGHT][BOARD_WIDTH]) {
    int walls = 0;

    for (int a = 0; a < BOARD_HEIGHT; a++) {
        for (int b = 0; b < BOARD_WIDTH; b++) {
            // Fills unbreakable walls 
            if (a == 0 || a == BOARD_HEIGHT - 1 || 
                b == 0 || b == BOARD_WIDTH - 1 || 
                (a % 2 == 0 && b % 2 == 0))
                game_board[a][b] = 1;
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
        if (game_board[a][b] == 0) {
            game_board[a][b] = 2;
            walls++;
        }

    }
}

int main() {
    //Seeds random number generator
    srand(time(NULL));

    int game_board[BOARD_HEIGHT][BOARD_WIDTH] = {0};
    create_board(game_board);

    for (int a = 0; a < BOARD_HEIGHT; a++) {
        for (int b = 0; b < BOARD_WIDTH; b++) {
            printf("%d ", game_board[a][b]);
        }
        printf("\n");
    }
    return 0;
}
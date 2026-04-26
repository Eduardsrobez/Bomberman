#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <GL/glut.h>
#include <string.h>

#include "config.h"
//#include "server.c"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SPRITE_WIDTH 256
#define SPRITE_HEIGHT 240

#define TILE_SIZE 16
#define TILE_SCALE 48
#define BOARD_WIDTH 15
#define BOARD_HEIGHT 13
#define CHARACTER_HEIGHT 19
#define CHARACTER_SCALE 3
#define CHARACTER_RENDERED_HEIGHT (CHARACTER_HEIGHT * CHARACTER_SCALE)
#define CHARACTER_RENDERED_WIDTH (15 * CHARACTER_SCALE)
#define WALK_SPEED 3.0f  // blocks per second

#define GRID_WIDTH (BOARD_WIDTH * TILE_SCALE)
#define GRID_HEIGHT (BOARD_HEIGHT * TILE_SCALE)
#define WINDOW_WIDTH GRID_WIDTH
#define WINDOW_HEIGHT (GRID_HEIGHT + 50)

void render(void);
void try_move_player(direction_t direction);
void update_player(float dt);
int can_move_to(float x, float y);
GLuint get_player_sprite(void);
void render_player(void);

typedef struct {
    float x, y;
    direction_t facing;
    int is_walking;
    direction_t walk_direction;
    float start_x, start_y;
    float target_x, target_y;
    float walk_progress;   // 0..1
} player_sprite_t;

typedef enum {
    STATE_MENU,
    STATE_GAME
} game_state_t;
GLuint menu_texture = 0;
GLuint tile_blank = 0;
GLuint tile_hard = 0;
GLuint tile_soft = 0;
GLuint char_sprites[9] = {0};  // down, walk_down1, walk_down2, up, walk_up1, walk_up2, right, walk_right1, walk_right2
unsigned char *full_spritesheet = NULL;
int spritesheet_width = 0;
int spritesheet_height = 0;
game_state_t current_state = STATE_MENU;
int current_window_width = WINDOW_WIDTH;
int current_window_height = WINDOW_HEIGHT;
player_sprite_t player;

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

void display_callback() {
    render();
}

int key_w = 0, key_a = 0, key_s = 0, key_d = 0;

void keyboard_up_callback(unsigned char key, int x, int y) {
    if (key == 'w' || key == 'W') key_w = 0;
    else if (key == 'a' || key == 'A') key_a = 0;
    else if (key == 's' || key == 'S') key_s = 0;
    else if (key == 'd' || key == 'D') key_d = 0;
}

void keyboard_callback(unsigned char key, int x, int y) {
    if (key == 27) exit(0);

    if (key == ' ') {
        if (current_state == STATE_MENU)
            current_state = STATE_GAME;
        glutPostRedisplay();
        return;
    }

    if (current_state != STATE_GAME) return;

    if (key == 'w' || key == 'W') {
        key_w = 1;
        player.facing = DIR_UP;
        try_move_player(DIR_UP);
    }
    else if (key == 'a' || key == 'A') {
        key_a = 1;
        player.facing = DIR_LEFT;
        try_move_player(DIR_LEFT);
    }
    else if (key == 's' || key == 'S') {
        key_s = 1;
        player.facing = DIR_DOWN;
        try_move_player(DIR_DOWN);
    }
    else if (key == 'd' || key == 'D') {
        key_d = 1;
        player.facing = DIR_RIGHT;
        try_move_player(DIR_RIGHT);
    }
}

void special_keys_callback(int key, int x, int y) {
    if (key == GLUT_KEY_UP) {
        try_move_player(DIR_UP);
        glutPostRedisplay();
    } else if (key == GLUT_KEY_DOWN) {
        try_move_player(DIR_DOWN);
        glutPostRedisplay();
    } else if (key == GLUT_KEY_LEFT) {
        try_move_player(DIR_LEFT);
        glutPostRedisplay();
    } else if (key == GLUT_KEY_RIGHT) {
        try_move_player(DIR_RIGHT);
        glutPostRedisplay();
    }
}

void timer_callback(int value) {
    update_player(0.016f);  // ~60 FPS
    glutPostRedisplay();
    glutTimerFunc(16, timer_callback, 0);
}

void reshape_callback(int width, int height) {
    // Enforce strict minimum window size
    if (width < GRID_WIDTH || height < GRID_HEIGHT) {
        glutReshapeWindow(GRID_WIDTH, GRID_HEIGHT);
        return;
    }
    
    current_window_width = width;
    current_window_height = height;
    
    // Set viewport to grid size only, prevents scaling
    glViewport(0, 0, GRID_WIDTH, GRID_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, GRID_WIDTH, GRID_HEIGHT, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
}

unsigned char* extract_tile(int x, int y, int size) {
    if (!full_spritesheet) return NULL;
    
    unsigned char *tile = (unsigned char *)malloc(size * size * 4);
    
    for (int ty = 0; ty < size; ty++) {
        for (int tx = 0; tx < size; tx++) {
            int src_idx = ((y + ty) * spritesheet_width + (x + tx)) * 4;
            int dst_idx = (ty * size + tx) * 4;
            
            if (x + tx < spritesheet_width && y + ty < spritesheet_height) {
                tile[dst_idx + 0] = full_spritesheet[src_idx + 0];
                tile[dst_idx + 1] = full_spritesheet[src_idx + 1];
                tile[dst_idx + 2] = full_spritesheet[src_idx + 2];
                tile[dst_idx + 3] = full_spritesheet[src_idx + 3];
            } else {
                tile[dst_idx + 0] = 0;
                tile[dst_idx + 1] = 0;
                tile[dst_idx + 2] = 0;
                tile[dst_idx + 3] = 255;
            }
        }
    }
    
    return tile;
}

unsigned char* extract_sprite_rect(int x, int y, int width, int height) {
    if (!full_spritesheet) return NULL;
    
    unsigned char *sprite = (unsigned char *)malloc(width * height * 4);
    
    for (int ty = 0; ty < height; ty++) {
        for (int tx = 0; tx < width; tx++) {
            int src_idx = ((y + ty) * spritesheet_width + (x + tx)) * 4;
            int dst_idx = (ty * width + tx) * 4;
            
            if (x + tx < spritesheet_width && y + ty < spritesheet_height) {
                sprite[dst_idx + 0] = full_spritesheet[src_idx + 0];
                sprite[dst_idx + 1] = full_spritesheet[src_idx + 1];
                sprite[dst_idx + 2] = full_spritesheet[src_idx + 2];
                sprite[dst_idx + 3] = full_spritesheet[src_idx + 3];
            } else {
                sprite[dst_idx + 0] = 0;
                sprite[dst_idx + 1] = 0;
                sprite[dst_idx + 2] = 0;
                sprite[dst_idx + 3] = 255;
            }
        }
    }
    
    return sprite;
}

GLuint create_tile_texture(int x, int y, int size) {
    unsigned char *tile_data = extract_tile(x, y, size);
    if (!tile_data) return 0;
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, tile_data);
    
    free(tile_data);
    return texture;
}
GLuint create_character_sprite(int x, int y, int width, int height) {
    unsigned char *sprite_data = extract_sprite_rect(x, y, width, height);
    if (!sprite_data) return 0;
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite_data);
    
    free(sprite_data);
    return texture;
}
void game_init() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, GRID_WIDTH, GRID_HEIGHT, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    
    int channels;
    unsigned char *data = stbi_load("spritesheet.png", &spritesheet_width, &spritesheet_height, &channels, STBI_rgb_alpha);
    
    if (data) {
        full_spritesheet = data;
        
        // Create menu texture - first 256x240
        unsigned char *menu_data = (unsigned char *)malloc(SPRITE_WIDTH * SPRITE_HEIGHT * 4);
        for (int y = 0; y < SPRITE_HEIGHT && y < spritesheet_height; y++) {
            for (int x = 0; x < SPRITE_WIDTH && x < spritesheet_width; x++) {
                int src_idx = (y * spritesheet_width + x) * 4;
                int dst_idx = (y * SPRITE_WIDTH + x) * 4;
                menu_data[dst_idx + 0] = full_spritesheet[src_idx + 0];
                menu_data[dst_idx + 1] = full_spritesheet[src_idx + 1];
                menu_data[dst_idx + 2] = full_spritesheet[src_idx + 2];
                menu_data[dst_idx + 3] = full_spritesheet[src_idx + 3];
            }
        }
        
        glGenTextures(1, &menu_texture);
        glBindTexture(GL_TEXTURE_2D, menu_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE_WIDTH, SPRITE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, menu_data);
        free(menu_data);
        
        // Extract tiles: blank (17,538), hard (34,538), soft (0,623)
        tile_blank = create_tile_texture(17, 538, TILE_SIZE);
        tile_hard = create_tile_texture(34, 538, TILE_SIZE);
        tile_soft = create_tile_texture(0, 623, TILE_SIZE);
        
        // Load character sprites from (1, 272)
        // Positions calculated with separators: down, walk_down1, walk_down2, up, walk_up1, walk_up2, right, walk_right1, walk_right2
        char_sprites[0] = create_character_sprite(1, 272, 14, CHARACTER_HEIGHT);      // looking down
        char_sprites[1] = create_character_sprite(25, 272, 15, CHARACTER_HEIGHT);     // walking down 1
        char_sprites[2] = create_character_sprite(48, 272, 15, CHARACTER_HEIGHT);     // walking down 2
        char_sprites[3] = create_character_sprite(73, 272, 14, CHARACTER_HEIGHT);     // looking up
        char_sprites[4] = create_character_sprite(96, 272, 15, CHARACTER_HEIGHT);     // walking up 1
        char_sprites[5] = create_character_sprite(121, 272, 15, CHARACTER_HEIGHT);    // walking up 2
        char_sprites[6] = create_character_sprite(144, 272, 14, CHARACTER_HEIGHT);    // looking right
        char_sprites[7] = create_character_sprite(169, 272, 15, CHARACTER_HEIGHT);    // walking right 1
        char_sprites[8] = create_character_sprite(194, 272, 15, CHARACTER_HEIGHT);    // walking right 2
        
        // Initialize player
        player.x = 1.0f; player.y = 1.0f;
        player.start_x = player.x; player.start_y = player.y;
        player.target_x = player.x; player.target_y = player.y;
        player.walk_progress = 0.0f;
        player.is_walking = 0;
        player.facing = DIR_DOWN;
        player.walk_direction = DIR_DOWN;
        game_board[1][1] = '.';
        
        glEnable(GL_TEXTURE_2D);
    } else {
        printf("Error loading spritesheet.png\n");
    }
}

int can_move_to(float x, float y) {
    int grid_x = (int)x;
    int grid_y = (int)y;
    
    if (grid_x < 0 || grid_x >= BOARD_WIDTH || grid_y < 0 || grid_y >= BOARD_HEIGHT) {
        return 0;
    }
    
    char tile = game_board[grid_y][grid_x];
    return (tile == '.'||tile == '1');
}

void update_player(float dt) {
    if (current_state != STATE_GAME) return;
    if (!player.is_walking) return;

    player.walk_progress += WALK_SPEED * dt;

    if (player.walk_progress >= 1.0f)
        player.walk_progress = 1.0f;

    float t = player.walk_progress;
    player.x = player.start_x + (player.target_x - player.start_x) * t;
    player.y = player.start_y + (player.target_y - player.start_y) * t;

    if (player.walk_progress >= 1.0f) {
        player.x = player.target_x;
        player.y = player.target_y;
        player.is_walking = 0;

        if (key_w) try_move_player(DIR_UP);
        else if (key_s) try_move_player(DIR_DOWN);
        else if (key_a) try_move_player(DIR_LEFT);
        else if (key_d) try_move_player(DIR_RIGHT);
    }
}

void try_move_player(direction_t direction) {
    if (current_state != STATE_GAME || player.is_walking) return;

    int dx = 0, dy = 0;
    if (direction == DIR_UP) dy = -1;
    else if (direction == DIR_DOWN) dy = 1;
    else if (direction == DIR_LEFT) dx = -1;
    else if (direction == DIR_RIGHT) dx = 1;
    else return;

    float next_x = player.x + dx;
    float next_y = player.y + dy;

    if (!can_move_to(next_x, next_y)) return;

    player.is_walking = 1;
    player.walk_direction = direction;
    player.facing = direction;
    player.start_x = player.x;
    player.start_y = player.y;
    player.target_x = next_x;
    player.target_y = next_y;
    player.walk_progress = 0.0f;
}

GLuint get_player_sprite() {
    if (player.is_walking) {
        float anim_progress = player.walk_progress;
        int frame = (int)(anim_progress * 2.0f) % 2;
        
        if (player.walk_direction == DIR_DOWN) {
            return char_sprites[1 + frame];
        } else if (player.walk_direction == DIR_UP) {
            return char_sprites[4 + frame];
        } else if (player.walk_direction == DIR_LEFT) {
            return char_sprites[7 + frame];
        } else if (player.walk_direction == DIR_RIGHT) {
            return char_sprites[7 + frame];
        }
    } else {
        if (player.facing == DIR_DOWN) {
            return char_sprites[0];
        } else if (player.facing == DIR_UP) {
            return char_sprites[3];
        } else if (player.facing == DIR_LEFT) {
            return char_sprites[6];
        } else if (player.facing == DIR_RIGHT) {
            return char_sprites[6];
        }
    }
    return char_sprites[0];
}

void render_player() {
    GLuint texture = get_player_sprite();
    if (texture <= 0) return;
    
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0f, 1.0f, 1.0f);
    
    float tile_center_x = player.x * TILE_SCALE + TILE_SCALE / 2.0f;
    float tile_bottom_y = (player.y + 1.0f) * TILE_SCALE;
    
    float scaled_width = 14 * CHARACTER_SCALE;
    if (player.is_walking) {
        float progress = player.walk_progress;
        int frame = (int)(progress * 2.0f) % 2;
        scaled_width = (frame == 0 ? 15 : 15) * CHARACTER_SCALE;
    }
    
    float render_height = CHARACTER_RENDERED_HEIGHT;
    float render_width = scaled_width;
    
    float x1 = tile_center_x - render_width / 2.0f;
    float y1 = tile_bottom_y - render_height;
    float x2 = x1 + render_width;
    float y2 = tile_bottom_y;
    
    float tex_left = 0.0f;
    float tex_right = 1.0f;
    
    // Mirror texture for left-facing
    if (player.is_walking && player.walk_direction == DIR_LEFT) {
        tex_left = 1.0f;
        tex_right = 0.0f;
    } else if (!player.is_walking && player.facing == DIR_LEFT) {
        tex_left = 1.0f;
        tex_right = 0.0f;
    }
    
    glBegin(GL_QUADS);
    glTexCoord2f(tex_left, 0.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(tex_right, 0.0f);
    glVertex2f(x2, y1);
    glTexCoord2f(tex_right, 1.0f);
    glVertex2f(x2, y2);
    glTexCoord2f(tex_left, 1.0f);
    glVertex2f(x1, y2);
    glEnd();
}

void render() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    if (current_state == STATE_MENU) {
        if (menu_texture > 0) {
            glBindTexture(GL_TEXTURE_2D, menu_texture);
            glColor3f(1.0f, 1.0f, 1.0f);

            glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(0, GRID_HEIGHT);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(GRID_WIDTH, GRID_HEIGHT);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(GRID_WIDTH, 0);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(0, 0);
            glEnd();
        }
    } else {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                char tile = game_board[y][x];
                GLuint texture = 0;

                if (tile == '.' || tile == '1') texture = tile_blank;
                else if (tile == 'H') texture = tile_hard;
                else if (tile == 'S') texture = tile_soft;

                if (texture > 0) {
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glColor3f(1.0f, 1.0f, 1.0f);

                    float x1 = x * TILE_SCALE;
                    float y1 = y * TILE_SCALE;
                    float x2 = x1 + TILE_SCALE;
                    float y2 = y1 + TILE_SCALE;

                    glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(x1, y2);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(x2, y2);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(x2, y1);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(x1, y1);
                    glEnd();
                }
            }
        }

        render_player();
    }

    glutSwapBuffers();
}

void close_game() {
    exit(0);
}

void gameloop() {
    //Vajag šo?
}

int main(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(GRID_WIDTH, GRID_HEIGHT);
    glutCreateWindow("Bomberman");

    // for png transparency to work
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Set reshape callback BEFORE game_init to handle initial setup
    glutReshapeFunc(reshape_callback);
    
    game_init();
    
    glutDisplayFunc(display_callback);
    glutReshapeFunc(reshape_callback);
    glutKeyboardUpFunc(keyboard_up_callback);
    glutKeyboardFunc(keyboard_callback);
    glutSpecialFunc(special_keys_callback);
    glutCloseFunc(close_game);
    glutTimerFunc(16, timer_callback, 0);
    
    glutMainLoop();
    return 0;
}
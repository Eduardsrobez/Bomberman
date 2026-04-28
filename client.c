#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ncurses.h>
#include <time.h>
#include "config.h"

#define SERVER_PORT 8888

int sock;
uint64_t last_move_ms = 0;
uint8_t my_id = 255;
uint8_t winner_id = 255;
game_status_t current_game_status = GAME_LOBBY;
uint8_t *game_map = NULL;
uint8_t map_w = 0, map_h = 0; 
player_t players[MAX_PLAYERS];
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

bool needs_redraw = true;

uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void redraw() {
    pthread_mutex_lock(&state_mutex);
    if (!needs_redraw) {
        pthread_mutex_unlock(&state_mutex);
        return;
    }
    erase(); 

    if (current_game_status == GAME_LOBBY) {
        mvprintw(1, 2, "--- BOMBERMAN LOBBY ---");
        mvprintw(2, 2, "Status: Waiting for players to be READY");
        mvprintw(4, 2, "Controls: ENTER to Ready, Q to Quit");
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if(players[i].id != 255) {
                mvprintw(6 + i, 4, "[%s] Player %d: %s %s", 
                    players[i].ready ? "READY" : "WAIT ", i + 1, players[i].name, (i == my_id) ? "(YOU)" : "");
            }
        }
    } 
    else if (current_game_status == GAME_RUNNING && game_map && map_w > 0) {
        mvprintw(0, 0, "ID: %d | Pos: %d,%d | HP: %s", my_id, players[my_id].row, players[my_id].col, players[my_id].alive ? "ALIVE" : "DEAD");

        for (int r = 0; r < map_h; r++) {
            for (int c = 0; c < map_w; c++) {
                uint8_t cell = game_map[r * map_w + c];
                int color = 1;
                if (cell == 'H' || cell == 'S') color = 1; 
                else if (cell == 'A' || cell == 'R' || cell == 'T' || cell == 'N') color = 2; 
                else if (cell == '*' || cell == '@') color = 3; 

                attron(COLOR_PAIR(color));
                mvaddch(r + 1, c * 2, cell);
                attroff(COLOR_PAIR(color));
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].id != 255 && players[i].alive) {
                if (i == my_id) attron(A_REVERSE | A_BOLD);
                mvaddch(players[i].row + 1, players[i].col * 2, (i + '1'));
                if (i == my_id) attroff(A_REVERSE | A_BOLD);
            }
        }
    } 
    else if (current_game_status == GAME_END) {
        mvprintw(LINES / 2, (COLS / 2) - 10, "--- GAME OVER ---");
        if (winner_id == 255) mvprintw((LINES / 2) + 1, (COLS / 2) - 5, " DRAW! ");
        else mvprintw((LINES / 2) + 1, (COLS / 2) - 15, " WINNER: PLAYER %s ", players[winner_id].name);
    }
    
    refresh();
    needs_redraw = false;
    pthread_mutex_unlock(&state_mutex);
}

void *network_thread(void *arg) {
    msg_generic_t msg;
    while (recv(sock, &msg, sizeof(msg_generic_t), 0) > 0) {
        pthread_mutex_lock(&state_mutex);
        if (msg.msg_type == MSG_HELLO) {
            char client_ver[20];
            char player_name[30];
            recv(sock, client_ver, 20, 0);
            recv(sock, player_name, 30, 0);

            players[msg.sender_id].id = msg.sender_id;
            players[msg.sender_id].ready = false;
            strncpy(players[msg.sender_id].name, player_name, MAX_NAME_LEN);
            players[msg.sender_id].name[MAX_NAME_LEN] = '\0';
        }
        else if (msg.msg_type == MSG_WELCOME) {
            char srv_name[20]; uint8_t st, p_count;
            recv(sock, srv_name, 20, 0); 
            recv(sock, &st, 1, 0);
            recv(sock, &p_count, 1, 0);
            my_id = msg.target_id; 
            current_game_status = (game_status_t)st;
            for(int i = 0; i < p_count; i++) {
                uint8_t pid; bool pready; char pname[30];
                recv(sock, &pid, 1, 0); 
                recv(sock, &pready, 1, 0); 
                recv(sock, pname, 30, 0);
                players[pid].id = pid; 
                players[pid].ready = pready; 
                strncpy(players[pid].name, pname, MAX_NAME_LEN);
            }
        }
        else if (msg.msg_type == MSG_DISCONNECT) {
            pthread_mutex_unlock(&state_mutex);
            close(sock);
            endwin();
            exit(0);
        }
        else if (msg.msg_type == MSG_PING) {
            msg_generic_t pong = { MSG_PONG, my_id, 255 };
            send(sock, &pong, sizeof(pong), 0);
        }
        else if (msg.msg_type == MSG_PONG) {
            // optional: mark server alive
        }
        else if (msg.msg_type == MSG_ERROR) {
            char errbuf[256] = {0};
            recv(sock, errbuf, sizeof(errbuf) - 1, 0); // adapt if you define exact format
            mvprintw(LINES - 1, 0, "ERROR: %s", errbuf);
        }

        else if (msg.msg_type == MSG_LEAVE) {
            players[msg.sender_id].id = 255;
            players[msg.sender_id].ready = false;
            players[msg.sender_id].alive = false;
            players[msg.sender_id].name[0] = '\0';
        }
        
        else if (msg.msg_type == MSG_SYNC_BOARD) {
            uint8_t h, w; recv(sock, &h, 1, 0); recv(sock, &w, 1, 0);
            map_h = h; map_w = w; game_map = realloc(game_map, map_h * map_w);
            recv(sock, game_map, map_h * map_w, 0);
        } 
        else if (msg.msg_type == MSG_BLOCK_DESTROYED) {
            uint16_t p; recv(sock, &p, 2, 0); uint16_t idx = ntohs(p);
            if (game_map && idx < map_h * map_w) game_map[idx] = '.';
        }
        else if (msg.msg_type == MSG_EXPLOSION_START) {
            struct { uint8_t rad; uint16_t pos; } __attribute__((packed)) pkg;
            recv(sock, &pkg, sizeof(pkg), 0);
            uint16_t center = ntohs(pkg.pos);
            int r = center / map_w, c = center % map_w;
            if (game_map) {
                // Only draw fire if it's not a powerup
                if (game_map[center] == '.' || game_map[center] == '@') game_map[center] = '*';

                int dr[] = {-1, 1, 0, 0}, dc[] = {0, 0, -1, 1};
                for(int i=0; i<4; i++) {
                    for(int rad=1; rad<=pkg.rad; rad++) {
                        int nr = r + dr[i]*rad, nc = c + dc[i]*rad;
                        if(nr<0 || nr>=map_h || nc<0 || nc>=map_w) break;
                        uint8_t cell = game_map[nr*map_w + nc];
                        if(cell == 'H') break; 
                        
                        // FIX: Only replace with fire if it's not a power-up
                        if (cell == '.' || cell == 'S') {
                            game_map[nr*map_w + nc] = '*';
                        }
                        
                        if(cell == 'S') break; 
                    }
                }
            }
        }
        else if (msg.msg_type == MSG_EXPLOSION_END) {
            struct { uint8_t rad; uint16_t pos; } __attribute__((packed)) pkg;
            recv(sock, &pkg, sizeof(pkg), 0);
            uint16_t center = ntohs(pkg.pos);
            int r = center / map_w, c = center % map_w;
            if (game_map) {
                if (game_map[center] == '*') game_map[center] = '.';
                int dr[] = {-1, 1, 0, 0}, dc[] = {0, 0, -1, 1};
                for(int i=0; i<4; i++) {
                    for(int rad=1; rad<=pkg.rad; rad++) {
                        int nr = r + dr[i]*rad, nc = c + dc[i]*rad;
                        if(nr<0 || nr>=map_h || nc<0 || nc>=map_w) break;
                        uint8_t cell = game_map[nr*map_w + nc];
                        if (cell == '*') game_map[nr*map_w + nc] = '.';
                        else if (cell == 'H' || cell == 'S') break;
                    }
                }
            }
        }
        else if (msg.msg_type == MSG_SET_STATUS) {
            uint8_t st; recv(sock, &st, 1, 0); current_game_status = (game_status_t)st;
        } 
        else if (msg.msg_type == MSG_SET_READY) {
            if (current_game_status != GAME_LOBBY) {
                pthread_mutex_unlock(&state_mutex);
                continue;
            }
            players[msg.sender_id].ready = true; 
            players[msg.sender_id].id = msg.sender_id;
        }
        else if (msg.msg_type == MSG_MOVED) {
            uint16_t p; recv(sock, &p, 2, 0); p = ntohs(p);
            if (map_w > 0 && msg.sender_id < MAX_PLAYERS) {
                players[msg.sender_id].row = p / map_w;
                players[msg.sender_id].col = p % map_w;
                players[msg.sender_id].id = msg.sender_id;
                players[msg.sender_id].alive = true;
            }
        }
        else if (msg.msg_type == MSG_BOMB) {
            uint16_t p; recv(sock, &p, 2, 0);
            if (game_map) game_map[ntohs(p)] = '@';
        } 
        else if (msg.msg_type == MSG_BONUS_AVAILABLE) {
            struct { uint8_t type; uint16_t pos; } __attribute__((packed)) pkg;
            recv(sock, &pkg, sizeof(pkg), 0);
            uint16_t p = ntohs(pkg.pos);
            if (game_map && p < map_h * map_w) {
                game_map[p] = (pkg.type == BONUS_SPEED) ? 'A' :
                            (pkg.type == BONUS_RADIUS) ? 'R' :
                            (pkg.type == BONUS_TIMER) ? 'T' : 'N';
            }
        }
        else if (msg.msg_type == MSG_BONUS_RETRIEVED) {
            struct { uint8_t pid; uint16_t pos; } __attribute__((packed)) pkg;
            recv(sock, &pkg, sizeof(pkg), 0);
            uint16_t p = ntohs(pkg.pos);
            if (game_map && p < map_h * map_w) {
                game_map[p] = '.';
            }
        }
        else if (msg.msg_type == MSG_DEATH) players[msg.sender_id].alive = false;
        else if (msg.msg_type == MSG_WINNER) { 
            winner_id = msg.sender_id; 
            current_game_status = GAME_END; 
        }
        needs_redraw = true; 
        pthread_mutex_unlock(&state_mutex);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { 
        printf("Usage: %s <name> [ip]\n", argv[0]); 
        return 1; 
    }
    char *ip = (argc > 2) ? argv[2] : "127.0.0.1";
    for(int i = 0; i < MAX_PLAYERS; i++) players[i].id = 255;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {AF_INET, htons(SERVER_PORT)};
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("Connection failed"); 
        return 1; 
    }

    initscr(); start_color(); cbreak(); noecho(); keypad(stdscr, 1); curs_set(0); timeout(30);
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    msg_generic_t h = {MSG_HELLO, 255, 255};
    char ver[20] = "v1.1"; 
    char name[30] = {0};
    strncpy(name, argv[1], 30);
    send(sock, &h, sizeof(h), 0); 
    send(sock, ver, 20, 0); 
    send(sock, name, 30, 0);
    pthread_t tid; pthread_create(&tid, NULL, network_thread, NULL);
    while (1) {
        int ch = getch(); 
        if (ch == 'q') {
            msg_generic_t leave = { MSG_LEAVE, my_id, 255 };
            send(sock, &leave, sizeof(leave), 0);
            break;
        };
        pthread_mutex_lock(&state_mutex);
        if (ch != ERR) {
            msg_generic_t act = {0, my_id, 255};
            if ((ch == '\n' || ch == KEY_ENTER) && current_game_status == GAME_LOBBY) {
                act.msg_type = MSG_SET_READY; send(sock, &act, sizeof(act), 0);
            } 
            else if (current_game_status == GAME_RUNNING && my_id != 255 && players[my_id].alive) {
                if (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT) {
                    uint64_t t = now_ms();
                    if (t - last_move_ms >= 250) {
                        act.msg_type = MSG_MOVE_ATTEMPT;
                        uint8_t d = (ch == KEY_UP) ? 'U' :
                                    (ch == KEY_DOWN) ? 'D' :
                                    (ch == KEY_LEFT) ? 'L' : 'R';
                        send(sock, &act, sizeof(act), 0);
                        send(sock, &d, 1, 0);
                        last_move_ms = t;
                    }
                } 
                else if (ch == ' ') {
                    act.msg_type = MSG_BOMB_ATTEMPT;
                    uint16_t pos = htons(players[my_id].row * map_w + players[my_id].col);
                    send(sock, &act, sizeof(act), 0);
                    send(sock, &pos, 2, 0);
                }
            }
            needs_redraw = true; 
        }
        pthread_mutex_unlock(&state_mutex);
        redraw(); 
    }
    endwin(); close(sock); return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "config.h"

#define PORT 8888
#define MAX_BOMBS 100
#define BONUS_BOMB_COUNT 4

typedef struct {
    bool active;
    uint16_t pos;
    uint8_t owner_id;
    uint8_t radius;
    int timer_ticks;
} server_bomb_t;

typedef struct {
    int socket;
    player_t data;
    bool connected;
} client_t;

/* ---------- Global state ---------- */

client_t clients[MAX_PLAYERS];
server_bomb_t bombs[MAX_BOMBS];

uint8_t *map_data = NULL;
uint8_t *bonus_data = NULL;
uint8_t map_h = 0;
uint8_t map_w = 0;

game_status_t current_status = GAME_LOBBY;
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

float cfg_speed = 0.0f;
uint8_t cfg_danger_time = 0;
uint8_t cfg_default_radius = 0;
uint8_t cfg_bomb_timer = 0;

/* ---------- Utility ---------- */

void broadcast(uint8_t type, uint8_t sender, uint8_t target, const void *payload, size_t len) {
    msg_generic_t msg = { type, sender, target };

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].connected) {
            continue;
        }

        send(clients[i].socket, &msg, sizeof(msg_generic_t), 0);
        if (payload && len > 0) {
            send(clients[i].socket, payload, len, 0);
        }
    }
}

int count_connected_clients(void) {
    int count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected) {
            count++;
        }
    }

    return count;
}

int count_alive_players(int *winner_id) {
    int count = 0;
    int last_alive = -1;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected && clients[i].data.alive) {
            count++;
            last_alive = i;
        }
    }

    if (winner_id) {
        *winner_id = last_alive;
    }

    return count;
}

void reset_client_slot(int id) {
    clients[id].connected = false;
    memset(&clients[id].data, 0, sizeof(player_t));
    clients[id].data.id = 255;
}

void send_sync_board_to_client(int id) {
    msg_generic_t sync = { MSG_SYNC_BOARD, 255, (uint8_t)id };
    send(clients[id].socket, &sync, sizeof(sync), 0);
    send(clients[id].socket, &map_h, 1, 0);
    send(clients[id].socket, &map_w, 1, 0);
    send(clients[id].socket, map_data, map_h * map_w, 0);
}

void init_player_stats(int id) {
    clients[id].data.id = (uint8_t)id;
    clients[id].data.alive = true;
    clients[id].data.ready = false;
    clients[id].data.lives = 1;
    clients[id].data.bomb_radius = cfg_default_radius;
    clients[id].data.bomb_count = 1;
    clients[id].data.speed = (uint16_t)cfg_speed;
}

void reset_player_for_lobby(int id) {
    clients[id].data.id = (uint8_t)id;
    clients[id].data.ready = false;
    clients[id].data.alive = false;
    clients[id].data.lives = 1;
    clients[id].data.bomb_radius = cfg_default_radius;
    clients[id].data.bomb_count = 1;
    clients[id].data.bomb_timer_ticks = cfg_bomb_timer;
    clients[id].data.speed = (uint16_t)cfg_speed;
}

/* ---------- Config / map loading ---------- */

void load_config_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        exit(1);
    }

    if (fscanf(f, "%hhu %hhu %f %hhu %hhu %hhu",
               &map_h, &map_w,
               &cfg_speed, &cfg_danger_time,
               &cfg_default_radius, &cfg_bomb_timer) != 6) {
        fprintf(stderr, "Error in %s format!\n", filename);
        fclose(f);
        exit(1);
    }

    free(map_data);
    free(bonus_data);

    map_data = malloc(map_h * map_w);
    bonus_data = malloc(map_h * map_w);

    if (!map_data || !bonus_data) {
        perror("malloc");
        fclose(f);
        exit(1);
    }

    for (int i = 0; i < map_h * map_w; i++) {
        char cell;
        if (fscanf(f, " %c", &cell) != 1) {
            break;
        }

        bonus_data[i] = BONUS_NONE;

        if (cell >= '1' && cell <= '8') {
            int idx = cell - '1';
            clients[idx].data.row = (uint16_t)(i / map_w);
            clients[idx].data.col = (uint16_t)(i % map_w);
            map_data[i] = '.';
        } else if (cell == 'A') {
            bonus_data[i] = BONUS_SPEED;
            map_data[i] = 'S';
        } else if (cell == 'R') {
            bonus_data[i] = BONUS_RADIUS;
            map_data[i] = 'S';
        } else if (cell == 'T') {
            bonus_data[i] = BONUS_TIMER;
            map_data[i] = 'S';
        } else if (cell == 'N') {
            bonus_data[i] = BONUS_BOMB_COUNT;
            map_data[i] = 'S';
        } else {
            map_data[i] = (uint8_t)cell;
        }
    }

    fclose(f);
}

void load_config(void) {
    load_config_file("config.txt");
}

void load_config_by_players(int player_count) {
    const char *filename = (player_count > 4) ? "config2.txt" : "config.txt";
    load_config_file(filename);
}

/* ---------- Game state ---------- */

void send_all_names_again(void) {
    char client_ver[20] = "v1.1";

    for (int sender = 0; sender < MAX_PLAYERS; sender++) {
        if (!clients[sender].connected) {
            continue;
        }

        for (int receiver = 0; receiver < MAX_PLAYERS; receiver++) {
            if (!clients[receiver].connected || receiver == sender) {
                continue;
            }

            msg_generic_t hello = { MSG_HELLO, (uint8_t)sender, 255 };
            send(clients[receiver].socket, &hello, sizeof(hello), 0);
            send(clients[receiver].socket, client_ver, 20, 0);
            send(clients[receiver].socket, clients[sender].data.name, 30, 0);
        }
    }
}

void restart_game(void) {
    int conn_count = count_connected_clients();

    load_config_by_players(conn_count);
    memset(bombs, 0, sizeof(bombs));

    current_status = GAME_LOBBY;

    {
        uint8_t status = (uint8_t)current_status;
        broadcast(MSG_SET_STATUS, 255, 255, &status, 1);
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].connected) {
            continue;
        }

        reset_player_for_lobby(i);
        send_sync_board_to_client(i);
    }

    send_all_names_again();
}

void maybe_end_game(void) {
    int winner_id = -1;
    int alive_count = count_alive_players(&winner_id);

    if (current_status != GAME_RUNNING || alive_count > 1) {
        return;
    }

    current_status = GAME_END;

    {
        uint8_t win_id = (winner_id != -1) ? (uint8_t)winner_id : 255;
        broadcast(MSG_WINNER, win_id, 255, NULL, 0);
    }

    pthread_mutex_unlock(&state_mutex);
    sleep(3);
    pthread_mutex_lock(&state_mutex);

    if (current_status == GAME_END) {
        restart_game();
    }
}

/* ---------- Explosion ---------- */

void handle_explosion(int b_idx) {
    static const int dr[4] = { -1, 1, 0, 0 };
    static const int dc[4] = { 0, 0, -1, 1 };

    uint8_t radius = bombs[b_idx].radius;
    uint16_t pos = bombs[b_idx].pos;
    uint16_t row = (uint16_t)(pos / map_w);
    uint16_t col = (uint16_t)(pos % map_w);

    struct {
        uint8_t rad;
        uint16_t pos;
    } __attribute__((packed)) pkg = { radius, htons(pos) };

    bombs[b_idx].active = false;
    map_data[pos] = '.';

    broadcast(MSG_EXPLOSION_START, bombs[b_idx].owner_id, 255, &pkg, sizeof(pkg));

    for (int dir = 0; dir < 4; dir++) {
        for (int step = 1; step <= radius; step++) {
            int nr = (int)row + dr[dir] * step;
            int nc = (int)col + dc[dir] * step;

            if (nr < 0 || nr >= map_h || nc < 0 || nc >= map_w) {
                break;
            }

            uint16_t idx = (uint16_t)(nr * map_w + nc);

            if (map_data[idx] == 'H') {
                break;
            }

            for (int b = 0; b < MAX_BOMBS; b++) {
                if (bombs[b].active && bombs[b].pos == idx && b != b_idx) {
                    handle_explosion(b);
                }
            }

            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (clients[p].connected &&
                    clients[p].data.alive &&
                    clients[p].data.row == nr &&
                    clients[p].data.col == nc) {
                    clients[p].data.alive = false;
                    broadcast(MSG_DEATH, (uint8_t)p, 255, NULL, 0);
                }
            }

            if (map_data[idx] == 'S') {
                if (bonus_data[idx] != BONUS_NONE) {
                    uint8_t bonus_type = bonus_data[idx];

                    struct {
                        uint8_t type;
                        uint16_t pos;
                    } __attribute__((packed)) bonus_pkg = {
                        bonus_type,
                        htons(idx)
                    };

                    map_data[idx] =
                        (bonus_type == BONUS_SPEED)  ? 'A' :
                        (bonus_type == BONUS_RADIUS) ? 'R' :
                        (bonus_type == BONUS_TIMER)  ? 'T' : 'N';

                    broadcast(MSG_BONUS_AVAILABLE, 255, 255, &bonus_pkg, sizeof(bonus_pkg));
                } else {
                    map_data[idx] = '.';
                }

                break;
            }
        }
    }

    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (clients[p].connected &&
            clients[p].data.alive &&
            clients[p].data.row == row &&
            clients[p].data.col == col) {
            clients[p].data.alive = false;
            broadcast(MSG_DEATH, (uint8_t)p, 255, NULL, 0);
        }
    }

    pthread_mutex_unlock(&state_mutex);
    usleep(500000);
    pthread_mutex_lock(&state_mutex);

    broadcast(MSG_EXPLOSION_END, bombs[b_idx].owner_id, 255, &pkg, sizeof(pkg));
    bombs[b_idx].active = false;

    maybe_end_game();
}

void update_tick(void) {
    pthread_mutex_lock(&state_mutex);

    if (current_status == GAME_RUNNING) {
        for (int i = 0; i < MAX_BOMBS; i++) {
            if (bombs[i].active && --bombs[i].timer_ticks <= 0) {
                handle_explosion(i);
            }
        }
    }

    pthread_mutex_unlock(&state_mutex);
}

void *game_thread(void *arg) {
    (void)arg;

    struct timespec ts = { 0, 1000000000 / TICKS_PER_SECOND };

    while (1) {
        nanosleep(&ts, NULL);
        update_tick();
    }

    return NULL;
}

/* ---------- Client handling ---------- */

void send_welcome(int id) {
    msg_generic_t welcome = { MSG_WELCOME, 255, (uint8_t)id };
    char srv_id[20] = "BomberServer2026";
    uint8_t status = (uint8_t)current_status;
    uint8_t p_count = 0;

    send(clients[id].socket, &welcome, sizeof(welcome), 0);
    send(clients[id].socket, srv_id, 20, 0);
    send(clients[id].socket, &status, 1, 0);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected && clients[i].data.id != 255) {
            p_count++;
        }
    }

    send(clients[id].socket, &p_count, 1, 0);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].connected || clients[i].data.id == 255) {
            continue;
        }

        uint8_t pid = clients[i].data.id;
        bool pready = clients[i].data.ready;
        char pname[30] = { 0 };

        strncpy(pname, clients[i].data.name, 30);

        send(clients[id].socket, &pid, 1, 0);
        send(clients[id].socket, &pready, 1, 0);
        send(clients[id].socket, pname, 30, 0);
    }
}

void forward_hello_to_others(int sender_id, const char version[20], const char name[30]) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].connected || i == sender_id || clients[i].data.id == 255) {
            continue;
        }

        msg_generic_t hello = { MSG_HELLO, (uint8_t)sender_id, 255 };
        send(clients[i].socket, &hello, sizeof(hello), 0);
        send(clients[i].socket, version, 20, 0);
        send(clients[i].socket, name, 30, 0);
    }
}

void send_current_player_positions_to_client(int id) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected && clients[i].data.alive) {
            uint16_t pos = htons((uint16_t)(clients[i].data.row * map_w + clients[i].data.col));
            msg_generic_t moved = { MSG_MOVED, (uint8_t)i, (uint8_t)id };
            send(clients[id].socket, &moved, sizeof(moved), 0);
            send(clients[id].socket, &pos, 2, 0);
        }
    }
}

void handle_hello(int id, int sock) {
    char client_ver[20];
    char player_name[30];
    bool participates = (current_status == GAME_LOBBY);

    recv(sock, client_ver, 20, 0);
    recv(sock, player_name, 30, 0);

    strncpy(clients[id].data.name, player_name, 30);
    clients[id].data.name[MAX_NAME_LEN] = '\0';

    clients[id].data.id = (uint8_t)id;
    init_player_stats(id);

    if (!participates) {
        clients[id].data.ready = false;
        clients[id].data.alive = false;
        clients[id].data.row = 0;
        clients[id].data.col = 0;
        clients[id].data.lives = 0;
    }

    if (participates) {
        forward_hello_to_others(id, client_ver, player_name);
    }

    send_welcome(id);
    send_sync_board_to_client(id);

    if (current_status == GAME_RUNNING) {
        send_current_player_positions_to_client(id);
    }
}

void handle_set_ready(int id) {
    clients[id].data.ready = true;
    broadcast(MSG_SET_READY, (uint8_t)id, 255, NULL, 0);

    int ready_count = 0;
    int conn_count = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected) {
            conn_count++;
            if (clients[i].data.ready) {
                ready_count++;
            }
        }
    }

    if (current_status != GAME_LOBBY || conn_count <= 1 || ready_count != conn_count) {
        return;
    }

    load_config_by_players(conn_count);
    current_status = GAME_RUNNING;

    {
        uint8_t status = (uint8_t)current_status;
        broadcast(MSG_SET_STATUS, 255, 255, &status, 1);
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].connected && clients[i].data.ready) {
            init_player_stats(i);

            uint16_t pos = htons((uint16_t)(clients[i].data.row * map_w + clients[i].data.col));
            broadcast(MSG_MOVED, (uint8_t)i, 255, &pos, 2);
        }
    }
}

void handle_move_attempt(int id, int sock) {
    uint8_t dir;
    player_t *p = &clients[id].data;

    recv(sock, &dir, 1, 0);

    if (!p->alive || current_status != GAME_RUNNING) {
        return;
    }

    int nr = p->row;
    int nc = p->col;

    if (dir == 'U') nr--;
    else if (dir == 'D') nr++;
    else if (dir == 'L') nc--;
    else if (dir == 'R') nc++;

    if (nr < 0 || nr >= map_h || nc < 0 || nc >= map_w) {
        return;
    }

    uint16_t next_pos = (uint16_t)(nr * map_w + nc);
    uint8_t target = map_data[next_pos];

    bool bomb_on_target = false;
    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active && bombs[i].pos == next_pos) {
            bomb_on_target = true;
            break;
        }
    }

    if (target == 'H' || target == 'S' || bomb_on_target) {
        return;
    }

    p->row = (uint16_t)nr;
    p->col = (uint16_t)nc;

    {
        uint16_t pos = htons(next_pos);
        broadcast(MSG_MOVED, (uint8_t)id, 255, &pos, 2);
    }

    if (target == 'R') p->bomb_radius++;
    else if (target == 'N') p->bomb_count++;
    else if (target == 'A') p->speed += 2;
    else if (target == 'T') p->bomb_timer_ticks += 10;

    if (target == 'A' || target == 'R' || target == 'T' || target == 'N') {
        struct {
            uint8_t player_id;
            uint16_t pos;
        } __attribute__((packed)) pkg = { (uint8_t)id, htons(next_pos) };

        bonus_data[next_pos] = BONUS_NONE;
        map_data[next_pos] = '.';

        broadcast(MSG_BONUS_RETRIEVED, (uint8_t)id, 255, &pkg, sizeof(pkg));
    }
}

void handle_bomb_attempt(int id, int sock) {
    uint16_t net_pos;
    recv(sock, &net_pos, 2, 0);

    if (current_status != GAME_RUNNING) {
        return;
    }

    uint16_t pos = ntohs(net_pos);
    int active_owned = 0;

    for (int i = 0; i < MAX_BOMBS; i++) {
        if (bombs[i].active && bombs[i].owner_id == (uint8_t)id) {
            active_owned++;
        }
    }

    if (active_owned >= clients[id].data.bomb_count) {
        return;
    }

    for (int i = 0; i < MAX_BOMBS; i++) {
        if (!bombs[i].active) {
            bombs[i] = (server_bomb_t){
                true,
                pos,
                (uint8_t)id,
                clients[id].data.bomb_radius,
                cfg_bomb_timer
            };

            broadcast(MSG_BOMB, (uint8_t)id, 255, &net_pos, 2);
            break;
        }
    }
}

void handle_leave(int id, int sock) {
    (void)sock;

    broadcast(MSG_LEAVE, (uint8_t)id, 255, NULL, 0);

    close(clients[id].socket);
    reset_client_slot(id);

    int conn_count = count_connected_clients();

    if (conn_count == 0) {
        memset(bombs, 0, sizeof(bombs));
        load_config();
        current_status = GAME_LOBBY;
        return;
    }

    if (current_status == GAME_RUNNING) {
        maybe_end_game();
    }
}

void *client_handler(void *arg) {
    int id = *(int *)arg;
    free(arg);

    int sock = clients[id].socket;
    msg_generic_t msg;

    while (recv(sock, &msg, sizeof(msg_generic_t), 0) > 0) {
        pthread_mutex_lock(&state_mutex);

        if (msg.msg_type == MSG_HELLO) {
            handle_hello(id, sock);
        } else if (msg.msg_type == MSG_SET_READY) {
            handle_set_ready(id);
        } else if (msg.msg_type == MSG_MOVE_ATTEMPT) {
            handle_move_attempt(id, sock);
        } else if (msg.msg_type == MSG_BOMB_ATTEMPT) {
            handle_bomb_attempt(id, sock);
        } else if (msg.msg_type == MSG_LEAVE) {
            handle_leave(id, sock);
            pthread_mutex_unlock(&state_mutex);
            return NULL;
        } else if (msg.msg_type == MSG_PING) {
            msg_generic_t pong = { MSG_PONG, 255, (uint8_t)id };
            send(sock, &pong, sizeof(pong), 0);
        } else if (msg.msg_type == MSG_PONG) {
            /* nothing */
        } else if (msg.msg_type == MSG_ERROR) {
            /* nothing */
        }

        pthread_mutex_unlock(&state_mutex);
    }

    pthread_mutex_lock(&state_mutex);

    broadcast(MSG_LEAVE, (uint8_t)id, 255, NULL, 0);
    close(sock);
    reset_client_slot(id);

    if (count_connected_clients() == 0) {
        current_status = GAME_LOBBY;
        memset(bombs, 0, sizeof(bombs));
    }

    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

/* ---------- Main ---------- */

int main(void) {
    srand(time(NULL));
    load_config();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = { AF_INET, htons(PORT), INADDR_ANY };
    int opt = 1;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 8);

    pthread_t tid;
    pthread_create(&tid, NULL, game_thread, NULL);

    printf("Serveris palaists portā %d\n", PORT);

    while (1) {
        int client_sock = accept(server_fd, NULL, NULL);

        pthread_mutex_lock(&state_mutex);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!clients[i].connected) {
                int *id_ptr = malloc(sizeof(int));

                memset(&clients[i].data, 0, sizeof(player_t));
                clients[i].socket = client_sock;
                clients[i].connected = true;

                *id_ptr = i;
                pthread_create(&tid, NULL, client_handler, id_ptr);
                break;
            }
        }

        pthread_mutex_unlock(&state_mutex);
    }

    return 0;
}
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define closesocket close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    typedef int SOCKET;
#endif

#include "config.h"

//Must be odd
#define BOARD_WIDTH 15
#define BOARD_HEIGHT 13
#define WALLS_COUNT 50

#define SERVER_PORT 5555
#define SERVER_IDENTIFIER "BOMBERMAN_SERVER_v1"
#define BACKLOG 8
#define RECEIVE_TIMEOUT_MS 30000

// Extended message structures
typedef struct {
    msg_generic_t base;
    char identifier[20];
    char name[MAX_NAME_LEN + 1];
} msg_hello_t;

typedef struct {
    msg_generic_t base;
    char server_identifier[20];
    game_status_t status;
    uint8_t player_count;
    // Variable length: for each player: id (1), ready (1), name (30)
    uint8_t players_data[MAX_PLAYERS * (1 + 1 + MAX_NAME_LEN + 1)];
} msg_welcome_t;

typedef struct {
    msg_generic_t base;
    char error_msg[256];
} msg_error_t;

// Client structure to track connected clients
typedef struct {
    SOCKET socket;
    time_t last_pong_time;
    uint8_t player_id;
    char name[MAX_NAME_LEN + 1];
    bool ready;
} client_t;

// Global server state
static SOCKET server_socket = INVALID_SOCKET;
static client_t clients[MAX_PLAYERS];
static game_status_t server_status = GAME_LOBBY;
static char server_id[20] = SERVER_IDENTIFIER;

// Initialize clients array
void init_clients() {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clients[i].socket = INVALID_SOCKET;
        clients[i].player_id = i;
        clients[i].last_pong_time = now;
        clients[i].ready = false;
        clients[i].name[0] = '\0';
    }
}

// Remove client from the server
void remove_client(uint8_t index) {
    if (index >= MAX_PLAYERS) return;
    
    if (clients[index].socket != INVALID_SOCKET) {
        closesocket(clients[index].socket);
        clients[index].socket = INVALID_SOCKET;
    }
}

// Send error message to client
void send_error(uint8_t client_index, const char *error_msg) {
    if (client_index >= MAX_PLAYERS || clients[client_index].socket == INVALID_SOCKET) {
        return;
    }
    
    msg_error_t error;
    error.base.msg_type = MSG_ERROR;
    error.base.sender_id = 255; // server
    error.base.target_id = clients[client_index].player_id;
    strncpy(error.error_msg, error_msg, 255);
    error.error_msg[255] = '\0';
    
    send(clients[client_index].socket, (char *)&error, sizeof(msg_error_t), 0);
    remove_client(client_index);
}

// Send message to a client
int send_message(uint8_t client_index, msg_generic_t *msg, size_t msg_size) {
    if (client_index >= MAX_PLAYERS || clients[client_index].socket == INVALID_SOCKET) {
        return -1;
    }
    
    int result = send(clients[client_index].socket, (char *)msg, (int)msg_size, 0);
    if (result == SOCKET_ERROR) {
        remove_client(client_index);
        return -1;
    }
    
    clients[client_index].last_pong_time = time(NULL);
    return result;
}

// Broadcast LEAVE message to all clients except sender
void broadcast_leave(uint8_t player_id) {
    msg_generic_t leave_msg;
    leave_msg.msg_type = MSG_LEAVE;
    leave_msg.sender_id = player_id;
    leave_msg.target_id = 254; // broadcast
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket != INVALID_SOCKET && clients[i].player_id != player_id) {
            send_message(i, &leave_msg, sizeof(msg_generic_t));
        }
    }
}

// Send WELCOME message
int send_welcome(uint8_t client_index) {
    msg_welcome_t welcome_msg;
    welcome_msg.base.msg_type = MSG_WELCOME;
    welcome_msg.base.sender_id = 255; // server
    welcome_msg.base.target_id = clients[client_index].player_id;
    
    strcpy(welcome_msg.server_identifier, server_id);
    welcome_msg.status = server_status;
    welcome_msg.player_count = 0;
    
    // Build player list
    int offset = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket != INVALID_SOCKET && clients[i].name[0] != '\0') {
            welcome_msg.players_data[offset] = clients[i].player_id;
            offset++;
            welcome_msg.players_data[offset] = clients[i].ready ? 1 : 0;
            offset++;
            strncpy((char *)&welcome_msg.players_data[offset], clients[i].name, MAX_NAME_LEN);
            offset += MAX_NAME_LEN + 1;
            welcome_msg.player_count++;
        }
    }
    
    size_t msg_size = sizeof(msg_welcome_t) - MAX_PLAYERS * (1 + 1 + MAX_NAME_LEN + 1) + 
                      welcome_msg.player_count * (1 + 1 + MAX_NAME_LEN + 1);
    
    int result = send_message(client_index, (msg_generic_t *)&welcome_msg, msg_size);
    if (result == SOCKET_ERROR) {
        send_error(client_index, "Failed to send welcome message");
        return SOCKET_ERROR;
    }
    return result;
}

// Send DISCONNECT message
int send_disconnect(SOCKET client_socket) {
    msg_generic_t disconnect_msg;
    disconnect_msg.msg_type = MSG_DISCONNECT;
    disconnect_msg.sender_id = 255; // server
    disconnect_msg.target_id = 255; // not applicable
    
    int result = send(client_socket, (char *)&disconnect_msg, sizeof(msg_generic_t), 0);
    closesocket(client_socket);
    return result;
}

// Process incoming client connection
void handle_new_connection(SOCKET client_socket, struct sockaddr_in *client_addr) {
    // Find free slot
    int free_slot = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket == INVALID_SOCKET) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot == -1) {
        send_disconnect(client_socket);
        return;
    }
    
    // Store socket and setup timeout
    clients[free_slot].socket = client_socket;
    clients[free_slot].last_pong_time = time(NULL);
    clients[free_slot].name[0] = '\0';
    clients[free_slot].ready = false;
    
    // Set receive timeout
    int timeout = RECEIVE_TIMEOUT_MS;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
}

// Process MSG_HELLO from client
void handle_hello(uint8_t client_index, msg_hello_t *hello_msg) {
    // Validate message
    if (client_index >= MAX_PLAYERS || clients[client_index].socket == INVALID_SOCKET) {
        return;
    }
    
    // Check if server can accept more players
    if (server_status != GAME_LOBBY) {
        send_error(client_index, "Server is not in lobby state");
        return;
    }
    
    // Store client name
    strncpy(clients[client_index].name, hello_msg->name, MAX_NAME_LEN);
    clients[client_index].name[MAX_NAME_LEN] = '\0';
    
    // Send WELCOME message
    send_welcome(client_index);
}

// Process incoming message from client
void process_client_message(uint8_t client_index, char *buffer, int bytes_received) {
    if (bytes_received < (int)sizeof(msg_generic_t)) {
        return;
    }
    
    msg_generic_t *generic_msg = (msg_generic_t *)buffer;
    
    switch (generic_msg->msg_type) {
        case MSG_HELLO:
            if (bytes_received >= (int)sizeof(msg_hello_t)) {
                handle_hello(client_index, (msg_hello_t *)buffer);
            }
            break;
            
        case MSG_LEAVE:
            if (clients[client_index].socket != INVALID_SOCKET) {
                uint8_t player_id = clients[client_index].player_id;
                remove_client(client_index);
                broadcast_leave(player_id);
            }
            break;
        
        case MSG_PING: {
            // Respond with PONG
            msg_generic_t pong_msg;
            pong_msg.msg_type = MSG_PONG;
            pong_msg.sender_id = 255; // server
            pong_msg.target_id = generic_msg->sender_id;
            if (send_message(client_index, &pong_msg, sizeof(msg_generic_t)) == SOCKET_ERROR) {
                send_error(client_index, "Failed to send pong");
            }
            break;
        }
        
        case MSG_PONG:
            // Update last pong time
            clients[client_index].last_pong_time = time(NULL);
            break;

        case MSG_SET_READY: {
            if (clients[client_index].name[0] == '\0') {
                send_error(client_index, "Not authenticated");
            } else {
                clients[client_index].ready = true;
            }
            break;
        }
    }
    
    // Allow socket reuse
    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(server_socket);
        server_socket = INVALID_SOCKET;
        return;
    }
    
    // Listen for connections
    if (listen(server_socket, BACKLOG) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(server_socket);
        server_socket = INVALID_SOCKET;
        return;
    }
    
    printf("Server started on port %d\n", SERVER_PORT);
}

void accept_connections() {
    if (server_socket == INVALID_SOCKET) return;
    
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    
    // Non-blocking accept
    SOCKET client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_socket != INVALID_SOCKET) {
        handle_new_connection(client_socket, &client_addr);
    }
}

void check_ping_timeouts() {
    time_t current_time = time(NULL);
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket != INVALID_SOCKET) {
            // Check if no PONG received within 30 seconds
            if ((current_time - clients[i].last_pong_time) > 30) {
                remove_client(i);
            }
        }
    }
}

void send_pings() {
    if (server_socket == INVALID_SOCKET) return;
    
    msg_generic_t ping_msg;
    ping_msg.msg_type = MSG_PING;
    ping_msg.sender_id = 255; // server
    ping_msg.target_id = 254; // broadcast
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket != INVALID_SOCKET) {
            send_message(i, &ping_msg, sizeof(msg_generic_t));
        }
    }
}

void receive_client_data() {
    char buffer[512];
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].socket == INVALID_SOCKET) continue;
        
        int bytes_received = recv(clients[i].socket, buffer, sizeof(buffer), 0);
        
        if (bytes_received > 0) {
            process_client_message(i, buffer, bytes_received);
        } else if (bytes_received == 0) {
            // Connection closed by client
            if (clients[i].name[0] != '\0') {
                uint8_t player_id = clients[i].player_id;
                remove_client(i);
                broadcast_leave(player_id);
            } else {
                remove_client(i);
            }
        }
        // SOCKET_ERROR with timeout is expected, just continue
    }
}

void send_inputs() {
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
N = bomb count up,
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
    
    // Start the server
    start_server();
    
    if (server_socket == INVALID_SOCKET) {
        printf("Failed to start server\n");
        return 1;
    }
    
    printf("Server running. Accepting connections...\n");
    
    // Game loop
    time_t last_ping = time(NULL);
    while (1) {
        accept_connections();
        receive_client_data();
        
        // Send pings every 5 seconds
        time_t now = time(NULL);
        if (now - last_ping >= 5) {
            send_pings();
            last_ping = now;
        }
        
        check_ping_timeouts();
        
        // Sleep to avoid busy waiting
        #ifdef _WIN32
            Sleep(100);
        #else
            usleep(100000);
        #endif
    }
    
    return 0;
}
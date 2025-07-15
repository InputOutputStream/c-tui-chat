#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <ncurses.h>
#include <unistd.h>
#include <chat.h>

// client.h
typedef struct CLIENT_APP {
    // Connexion réseau
    int server_fd;
    struct sockaddr_in server_addr;
    
    // Interface TUI
    window_t *window;
    
    // État local
    chat_t user_chats[MAX_CHATS];
    message_t message_cache[MAX_CHAT_MESSAGES_NUMBER];
    int message_count;
    client_t *client;
    
    // Threads
    pthread_t network_thread;    // Réception messages
    pthread_t ui_thread;         // Interface utilisateur
    pthread_t heartbeat_thread;  // Keep-alive
    pthread_t receive_thread;
    volatile int client_running;

    // Synchronisation
    pthread_mutex_t message_mutex;
    int running;
} client_app_t;

typedef struct ARGS {
    window_t *window;
    client_t *client;
} args_t;

// Core connection functions
client_t *connect_to_server(const char *ip, int port);
client_t *auto_reconnect(const char *ip, int port);
int send_formatted_message(int msg_type, const char *message, client_t *client);
int send_message_to_user(int receiver_id, const char *message, client_t *client);

// Message parsing functions
char *get_sender(char *buffer, char *message);
char *get_message_content(char *buffer);
int get_message_type(char *buffer);
char *extract_delimited_content(char *buffer, char start_char, char end_char, int *start_pos);
char *get_command_arg(const char* input);

// Client management functions
void update_local_client_list(char *client, client_t *clients);
void update_client_list_from_server(window_t *window, const char *client_data);

// Thread functions
void *receive_messages(void *arg);

// Client state management
int is_client_running();
void set_client_running(int running);

// Interface functions (to be implemented in interface.c)
window_t *init_interface();
void cleanup_interface(window_t *window);
void display_messages(window_t *window, const char *sender, const char *message);
void update_status(window_t *window, const char *status);
void update_client_list(window_t *window);

#endif // !CLIENT_H
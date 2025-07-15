#ifndef SERVER_H
#define SERVER_H

#include "chat.h"

// Server functions
extern int start_message_server();
void *accept_connections(void *arg);
void *handle_client(void *arg);
void *handle_client_timeout(void *arg);

// Message handling
extern void broadcast_message(client_t *sender, const char *message);
void send_to_chat(client_t *sender, int *ids, int id_number, char *message);
int safe_send(int sockfd, const char *message, size_t len);
int send_signal(int rec_fd, client_message_type_t msg);

// Message parsing
message_t init_message(client_t client, char *buffer);
client_message_type_t init_type_message(char* msg_types_buffer);
int get_chat_id(char *buffer);
int get_rec_id(char *buffer);
int *get_chat_clients_ids(char *buffer, int *id_numbers);
char* extract_delimited_content(char *buffer, char start_char, char end_char, int *start_pos);

// Client management
void remove_client(client_t *client);
void mark_as_inactive(client_t *client);
void _mark_as_inactive(int i);
client_t* find_client_by_id(int client_id);
void compact_clients();
void clean_up_clients(pthread_t accept_thread);

// Validation and timeout
extern int validate_client_message(client_t *client, message_t *msg);
extern int disconnect_slow_client(client_t *client);

// Chat management
extern void free_chats(chat_t *chat);
int leave_chat(client_t *client, size_t chat_id);
char *list_users();

// Signal handling
void signal_handler(int sig);

// Interface functions (if UI exists)
extern void display_messages(window_t *window, const char *user_name, const char *messages);
extern void update_client_list(window_t *window);
extern void update_status(window_t *window, const char *status);
extern void cleanup_interface(window_t *window);

#endif // SERVER_H

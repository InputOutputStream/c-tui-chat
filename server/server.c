#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "chat.h"
#include "server.h"

// External globals from main.c
extern client_t clients[MAX_CLIENT];
extern chat_t chats[MAX_CHATS];
extern int client_count;
extern pthread_mutex_t clients_mutex;
extern pthread_mutex_t chats_mutex;
extern int server_running;

// Static counter for unique client IDs
static int next_client_id = 0;
static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;

int start_message_server() {
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Set socket options for address reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        return -1;
    }

    // Configure server address
    struct sockaddr_in server_addr = {0};    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return -1;
    }

    // Listen for connections
    if (listen(server_fd, MAX_CLIENT) < 0) {
        perror("Listen failed");
        close(server_fd);
        return -1;
    }

    fprintf(stdout, "Server address = %s:%d\n", 
            inet_ntoa(server_addr.sin_addr), DEFAULT_PORT);

    // Create accept thread
    pthread_t accept_thread;
    int *server_fd_ptr = malloc(sizeof(int));
    if (!server_fd_ptr) {
        perror("Memory allocation failed");
        close(server_fd);
        return -1;
    }
    *server_fd_ptr = server_fd;

    if (pthread_create(&accept_thread, NULL, accept_connections, server_fd_ptr) != 0) {
        perror("Failed to create accept thread");
        free(server_fd_ptr);
        close(server_fd);
        return -1;
    }
    
    pthread_detach(accept_thread);
    return 0;
}

void *accept_connections(void *arg) {
    int server_fd = *((int*)arg);
    free(arg);

    while(server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (server_running) {
                perror("Accept failed");
            }
            continue;
        }

        fprintf(stdout, "New client accepted: addr = %s\n", 
                inet_ntoa(client_addr.sin_addr));

        pthread_mutex_lock(&clients_mutex);
        
        if(client_count < MAX_CLIENT) {
            // Find first available slot
            int slot = -1;
            for (int i = 0; i < MAX_CLIENT; i++) {
                if (!clients[i].active) {
                    slot = i;
                    break;
                }
            }
            
            if (slot == -1) {
                fprintf(stdout, "No available slots\n");
                close(client_fd);
                pthread_mutex_unlock(&clients_mutex);
                continue;
            }
            
            client_t *client = &clients[slot];
            memset(client, 0, sizeof(client_t));
            
            // Get unique client ID
            pthread_mutex_lock(&id_mutex);
            client->client_id = next_client_id++;
            pthread_mutex_unlock(&id_mutex);
            
            client->client_fd = client_fd;
            client->client_addr = client_addr;
            client->client_addr_len = addr_len;
            client->msg_count = 0;
            client->active = 1;
            client->connect_time = time(NULL);
            client->last_activity = client->connect_time;
            snprintf(client->client_name, sizeof(client->client_name), 
                    "User%d", client->client_id);
            
            // Send welcome message with client details
            char welcome_msg[256];
            snprintf(welcome_msg, sizeof(welcome_msg), "ID:%d NAME:%s", 
                    client->client_id, client->client_name);
             
            if(send(client->client_fd, welcome_msg, strlen(welcome_msg), 0) < 0) {
                fprintf(stdout, "Failed to send welcome message\n");
                close(client_fd);
                client->active = 0;
                pthread_mutex_unlock(&clients_mutex);
                continue;
            }

            // Create client handler thread
            if (pthread_create(&client->thread, NULL, handle_client, client) != 0) {
                fprintf(stdout, "Failed to create client thread\n");
                close(client_fd);
                client->active = 0;
                pthread_mutex_unlock(&clients_mutex);
                continue;
            }
            
            client_count++;
        } else {
            fprintf(stdout, "Connection rejected: %s (server full)\n", 
                    inet_ntoa(client_addr.sin_addr));
            close(client_fd);
        }

        pthread_mutex_unlock(&clients_mutex);
    }

    close(server_fd);
    return NULL;
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[MAX_MESSAGE_LENGHT];

    // Start timeout handler
    pthread_t timeout_thread;
    if (pthread_create(&timeout_thread, NULL, handle_client_timeout, client) == 0) {
        pthread_detach(timeout_thread);
    }

    while(server_running && client->active) {
        ssize_t bytes = recv(client->client_fd, buffer, sizeof(buffer)-1, 0);

        if(bytes <= 0) {
            if (bytes == 0) {
                fprintf(stdout, "Client %d disconnected normally\n", client->client_id);
            } else {
                fprintf(stdout, "Client %d connection error\n", client->client_id);
            }
            break;
        }

        buffer[bytes] = '\0';
        client->last_activity = time(NULL);

        // Parse message type
        client_message_type_t msg_type = init_type_message(buffer);
        
        switch(msg_type) {
            case MSG_AUTH_REQ:
                // TODO: Implement authentication
                break;
                
            case MSG_BROADCAST_CLIENTS:
                broadcast_message(client, buffer);
                break;
                
            case MSG_DISCONNECT:
                fprintf(stdout, "Client %d requested disconnect\n", client->client_id);
                remove_client(client);
                goto cleanup;
                
            case MSG_SEND: {
                int rec_id = get_rec_id(buffer);
                if(rec_id < 0) {
                    char error_msg[] = "Invalid receiver ID";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                    break;
                }
                
                client_t *receiver = find_client_by_id(rec_id);
                if(receiver == NULL || !receiver->active) {
                    char error_msg[] = "User not found or offline";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                    break;
                }

                message_t message = init_message(*client, buffer);
                message.receiver_user_id = rec_id;
                
                if (safe_send(receiver->client_fd, (char*)&message, sizeof(message)) < 0) {
                    char error_msg[] = "Failed to deliver message";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                }
                break;
            }

            case MSG_CHANNEL: {
                int id_number = 0;
                int *ids = get_chat_clients_ids(buffer, &id_number);
                if(ids != NULL) {
                    send_to_chat(client, ids, id_number, buffer);
                    free(ids);
                } else {
                    char error_msg[] = "Invalid channel members";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                }
                break;
            }
                
            case MSG_LEAVE_CHANNEL: {
                int chat_id = get_chat_id(buffer);
                if(chat_id >= 0) {
                    leave_chat(client, chat_id);
                } else {
                    char error_msg[] = "Invalid chat ID";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                }
                break;
            }
            
            case MSG_LIST_USERS: {
                char *user_list = list_users();
                if (user_list) {
                    send(client->client_fd, user_list, strlen(user_list), 0);
                    free(user_list);
                } else {
                    char error_msg[] = "Failed to get user list";
                    send(client->client_fd, error_msg, strlen(error_msg), 0);
                }
                break;
            }
            
            case MSG_HEARTBEAT:
                // Heartbeat already updates last_activity above
                break;
                
            case MSG_CREATE_CHANNEL:
                // TODO: Implement channel creation
                break;
                
            case MSG_JOIN_CHANNEL:
                // TODO: Implement channel joining
                break;
                
            default:
                fprintf(stderr, "Unknown message type: %d\n", msg_type);
                break;
        }
    }

cleanup:
    fprintf(stdout, "Client %d handler exiting\n", client->client_id);
    client->active = 0;
    close(client->client_fd);
    remove_client(client);
    return NULL;
}

// Message parsing functions
char* extract_delimited_content(char *buffer, char start_char, char end_char, int *start_pos) {
    if (!buffer || !start_pos) return NULL;
    
    char *pos = strchr(buffer + *start_pos, start_char);
    if (!pos) return NULL;
    
    int start_idx = pos - buffer + 1;
    char *end_pos = strchr(pos + 1, end_char);
    if (!end_pos) return NULL;
    
    int len = end_pos - pos - 1;
    if (len <= 0) return NULL;
    
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    strncpy(result, buffer + start_idx, len);
    result[len] = '\0';
    
    *start_pos = end_pos - buffer + 1;
    return result;
}

int get_chat_id(char *buffer) {
    int start_pos = 0;
    char *content = extract_delimited_content(buffer, '[', ']', &start_pos);
    
    if (!content) return -1;
    
    int result = atoi(content);
    free(content);
    return result;
}

int get_rec_id(char *buffer) {
    int start_pos = 0;
    char *content = extract_delimited_content(buffer, '%', '%', &start_pos);
    
    if (!content) return -1;
    
    int result = atoi(content);
    free(content);
    return result;
}

int *get_chat_clients_ids(char *buffer, int *id_numbers) {
    if (!id_numbers) return NULL;
    
    int start_pos = 0;
    char *content = extract_delimited_content(buffer, '{', '}', &start_pos);
    
    if (!content) {
        *id_numbers = 0;
        return NULL;
    }
    
    // Count commas to determine array size
    int count = 1;
    for (int i = 0; content[i]; i++) {
        if (content[i] == ',') count++;
    }
    
    int *result = malloc(count * sizeof(int));
    if (!result) {
        free(content);
        *id_numbers = 0;
        return NULL;
    }
    
    int index = 0;
    char *token = strtok(content, ",");
    while (token && index < count) {
        result[index++] = atoi(token);
        token = strtok(NULL, ",");
    }
    
    *id_numbers = index;
    free(content);
    return result;
}

client_message_type_t init_type_message(char *msg_types_buffer) {
    if (!msg_types_buffer) return 0;
    
    int start_pos = 0;
    char *content = extract_delimited_content(msg_types_buffer, '#', '#', &start_pos);
    
    if (!content) return 0;

    int result = atoi(content);
    free(content);
    return result;
}

message_t init_message(client_t client, char *buffer) {
    message_t new = {0};
    
    if (buffer) {
        strncpy(new.payload, buffer, sizeof(new.payload) - 1);
        new.payload[sizeof(new.payload) - 1] = '\0';
        new.length = strlen(new.payload);
    }
    
    new.sender_user_id = client.client_id;
    new.receiver_user_id = -1;
    new.timestamp = time(NULL);
    
    return new;
}

// Client management functions
client_t* find_client_by_id(int client_id) {
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(clients[i].active && clients[i].client_id == client_id) {
            pthread_mutex_unlock(&clients_mutex);
            return &clients[i];
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

void _mark_as_inactive(int i) {
    if (i >= 0 && i < MAX_CLIENT) {
        clients[i].active = 0;
        if (clients[i].client_fd > 0) {
            close(clients[i].client_fd);
            clients[i].client_fd = -1;
        }
    }
}

void mark_as_inactive(client_t *client) {
    if (!client) return;
    
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(&clients[i] == client) {
            _mark_as_inactive(i);
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(client_t *client) {
    if (!client) return;
    
    mark_as_inactive(client);
    
    // Update client count
    pthread_mutex_lock(&clients_mutex);
    int active_count = 0;
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(clients[i].active) active_count++;
    }
    client_count = active_count;
    pthread_mutex_unlock(&clients_mutex);
}

// Communication functions
int safe_send(int sockfd, const char *message, size_t len) {
    if (sockfd < 0 || !message || len == 0) return -1;
    
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sockfd, message + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // Brief delay before retry
                continue;
            }
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

void broadcast_message(client_t *sender, const char *message) {
    if (!sender || !message) return;
    
    pthread_mutex_lock(&clients_mutex);
    
    size_t msg_len = strlen(message);
    
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].active && clients[i].client_fd != sender->client_fd) {
            fprintf(stdout, "Broadcasting to %s: %s\n", 
                    clients[i].client_name, message);
            
            if (safe_send(clients[i].client_fd, message, msg_len) < 0) {
                fprintf(stderr, "Failed to send broadcast to client %d\n", 
                        clients[i].client_id);
                _mark_as_inactive(i);
            } else {
                clients[i].msg_count++;
            }
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

void send_to_chat(client_t *sender, int *chat_clients_ids, int id_number, char *message) {
    if (!sender || !chat_clients_ids || !message || id_number <= 0) return;
    
    pthread_mutex_lock(&clients_mutex);
    
    for(int j = 0; j < id_number; j++) {
        client_t *target = find_client_by_id(chat_clients_ids[j]);
        if (target && target->active && target->client_fd != sender->client_fd) {
            if (safe_send(target->client_fd, message, strlen(message)) == 0) {
                target->msg_count++;
            } else {
                fprintf(stderr, "Failed to send to client %d\n", target->client_id);
            }
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

int send_signal(int rec_fd, client_message_type_t msg) {
    if (rec_fd < 0) return -1;
    
    char signal_buffer[32];
    snprintf(signal_buffer, sizeof(signal_buffer), "#%d#", msg);
    
    return safe_send(rec_fd, signal_buffer, strlen(signal_buffer));
}

// Timeout and validation functions
void *handle_client_timeout(void *arg) {
    client_t *client = (client_t*)arg;
    
    while (client->active && server_running) {
        if (time(NULL) - client->last_activity > TIMEOUT) {
            fprintf(stdout, "Client %d timed out\n", client->client_id);
            disconnect_slow_client(client);
            break;
        }
        sleep(60); // Check every minute
    }
    
    return NULL;
}

int disconnect_slow_client(client_t *client) {
    if (!client) return -1;
    
    printf("Disconnecting slow client %d...\n", client->client_id);
    
    if(send_signal(client->client_fd, MSG_DISCONNECT) == -1) {
        fprintf(stderr, "Could not send disconnect signal to %s\n", 
                client->client_name);
    }

    mark_as_inactive(client);
    return 0;
}

int validate_client_message(client_t *client, message_t *msg) {
    if (!client || !msg) return -1;
    
    // Anti-spam check
    if (client->msg_count > MAX_MESSAGES_PER_MINUTE) {
        return -1;
    }
    
    // Content validation
    if (msg->length > MAX_MESSAGE_LENGHT) {
        return -1;
    }
    
    return 0;
}

// Utility functions
void compact_clients() {
    pthread_mutex_lock(&clients_mutex);
    
    // No actual compaction needed since we use fixed array with active flag
    // Just update client_count
    int active_count = 0;
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(clients[i].active) active_count++;
    }
    client_count = active_count;
    
    pthread_mutex_unlock(&clients_mutex);
}

void clean_up_clients(pthread_t accept_thread) {
    // Signal all clients to disconnect
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(clients[i].active) {
            close(clients[i].client_fd);
            clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    // Cancel accept thread
    pthread_cancel(accept_thread);
}

// Chat management (stub implementations)
int leave_chat(client_t *client, size_t chat_id) {
    // TODO: Implement chat leaving logic
    fprintf(stdout, "Client %d left chat %zu\n", client->client_id, chat_id);
    return 0;
}

char *list_users() {
    pthread_mutex_lock(&clients_mutex);

    // Calculate needed buffer size
    size_t buffer_size = 1024;
    char *user_list = malloc(buffer_size);
    if (!user_list) {
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    
    strcpy(user_list, "Active users:\n");
    
    for(int i = 0; i < MAX_CLIENT; i++) {
        if(clients[i].active) {
            char user_info[256];
            snprintf(user_info, sizeof(user_info), "ID:%d NAME:%s\n", 
                    clients[i].client_id, clients[i].client_name);
            strcat(user_list, user_info);
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return user_list;
}

void free_chats(chat_t *chat) {
    if (chat) {
        free(chat);
    }
}

// Signal handling
void signal_handler(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            printf("\nArrêt du serveur...\n");
            server_running = 0;   
            break;
        default:
            break;
    }
}

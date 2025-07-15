#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <ncurses.h>
#include <unistd.h>
#include <signal.h>

#include <chat.h>
#include <client.h>

// Global variables
static client_t local_clients_list[MAX_CLIENT];
static int local_client_count = 0;
static int client_running = 1;

char* extract_delimited_content(char *buffer, char start_char, char end_char, int *start_pos) {
    char *pos = strchr(buffer + *start_pos, start_char);
    if (!pos) {
        return NULL;
    }
    
    int start_idx = pos - buffer + 1;
    char *end_pos = strchr(pos + 1, end_char);
    if (!end_pos) {
        return NULL;
    }
    
    int len = end_pos - pos - 1;
    if (len <= 0) {
        return NULL;
    }
    
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    strncpy(result, buffer + start_idx, len);
    result[len] = '\0';
    
    *start_pos = end_pos - buffer + 1;
    return result;
}

char* get_command_arg(const char* input) {
    char* space = strchr(input, ' ');
    if (!space) return NULL;
    
    while (*space == ' ') space++;
    
    return (*space != '\0') ? space : NULL;
}

char *get_sender(char *buffer, char *message) {
    char *colon = strchr(buffer, ':');
    if (colon) {
        *colon = '\0';
        strcpy(message, colon + 1);
        return buffer;
    } else {
        strcpy(message, buffer);
        return "Unknown";
    }
}

void update_local_client_list(char *client_name, client_t *local_clients_list) {
    // Extract client ID from name format "UserX"
    if (strncmp(client_name, "User", 4) == 0) {
        int id = atoi(client_name + 4);
        if (id > 0 && id <= MAX_CLIENT) {
            // Find existing client or add new one
            int found = 0;
            for (int i = 0; i < local_client_count; i++) {
                if (local_clients_list[i].client_id == id) {
                    found = 1;
                    break;
                }
            }
            
            if (!found && local_client_count < MAX_CLIENT) {
                local_clients_list[local_client_count].client_id = id;
                strncpy(local_clients_list[local_client_count].client_name, client_name, NAME_LENGHT - 1);
                local_clients_list[local_client_count].client_name[NAME_LENGHT - 1] = '\0';
                local_clients_list[local_client_count].active = 1;
                local_client_count++;
            }
        }
    }
}

void *receive_messages(void *arg) {
    args_t *args = (args_t*)arg;
    client_t *client = args->client;
    int fd = client->client_fd;
    window_t *window = args->window;

    char buffer[MAX_MESSAGE_LENGHT];
    time_t last_beat = time(NULL);
    char message[MAX_MESSAGE_LENGHT];

    while (client_running) {
        ssize_t bytes = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            if (client_running) {
                update_status(window, "Connexion fermée");
                client_running = 0;
            }
            break;
        }
        buffer[bytes] = '\0';

        // Parse message
        char *sender = get_sender(buffer, message);
        int msg_type = get_message_type(message);
        char *content = get_message_content(message);

        if (sender && strcmp(sender, "Unknown") != 0) {
            update_local_client_list(sender, local_clients_list);
        }
        
        if (!content) {
            fprintf(stderr, "Message is empty\n");
            continue;
        }

        switch (msg_type) {
            case MSG_AUTH_ACK:
                update_status(window, "✅ Authentification réussie");
                break;

            case MSG_AUTH_FAIL:
                update_status(window, "❌ Authentification échouée");
                break;

            case MSG_BROADCAST: 
                display_messages(window, sender, content);
                break;
            
            case MSG_USER_JOIN: 
                display_messages(window, "Système", content);
                break;
            
            case MSG_USER_LEAVE:
                display_messages(window, "Système", content);
                break;

            case MSG_CHANNEL_CREATION: 
                display_messages(window, "Système", content);
                break;
            
            case MSG_MSG: {
                int start_pos = 0;
                char *sender_content = extract_delimited_content(content, '%', '%', &start_pos);
                if (sender_content) {
                    char sender_name[64];
                    snprintf(sender_name, sizeof(sender_name), "User%s", sender_content);
                    display_messages(window, sender_name, content + start_pos);
                    free(sender_content);
                }
                break;
            }
            
            case MSG_USER_LIST: 
                update_client_list_from_server(window, content);
                break;
            
            case MSG_PING: 
                send_formatted_message(MSG_HEARTBEAT, "", client);
                break;
            
            case MSG_SERVER_ERROR: 
                update_status(window, content);
                break;
            
            case MSG_SERVER_INFO: 
                display_messages(window, "Serveur", content);
                break;
            
            default:
                display_messages(window, "Inconnu", content);
                break;
        }
        
        if (content) {
            free(content);
        }

        // Send heartbeat every 5 minutes
        if (time(NULL) - last_beat > 300) {
            send_formatted_message(MSG_HEARTBEAT, "", client);
            last_beat = time(NULL);
        }
    }
    return NULL;
}

int get_message_type(char *buffer) {
    int start_pos = 0;
    char *type_str = extract_delimited_content(buffer, '#', '#', &start_pos);
    if (!type_str) return -1;
    
    int type = atoi(type_str);
    free(type_str);
    return type;
}

char* get_message_content(char *buffer) {
    char *first_hash = strchr(buffer, '#');
    if (!first_hash) return NULL;
    
    char *second_hash = strchr(first_hash + 1, '#');
    if (!second_hash) return NULL;
    
    return strdup(second_hash + 1);
}

void update_client_list_from_server(window_t *window, const char *client_data) {
    werase(window->users_win);
    box(window->users_win, 0, 0);
    mvwprintw(window->users_win, 0, 2, "local_Clients_list");
    
    char *data_copy = strdup(client_data);
    if (!data_copy) return;
    
    char *token = strtok(data_copy, ",");
    int line = 1;
    
    while (token && line < getmaxy(window->users_win) - 1) {
        mvwprintw(window->users_win, line++, 1, "Client_%s", token);
        token = strtok(NULL, ",");
    }
    
    wrefresh(window->users_win);
    free(data_copy);
}

client_t *connect_to_server(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return NULL; 
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(fd);
        return NULL;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return NULL;
    }

    client_t *client = calloc(1, sizeof(client_t)); 
    if (!client) {
        close(fd);
        return NULL;
    }
    
    client->client_fd = fd;
    client->connect_time = time(NULL);
    client->last_activity = time(NULL);
    client->active = 1;
    
    // Receive client info as formatted string
    char buffer[256];
    int bytes_received = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        fprintf(stderr, "Failed to receive client details\n");
        free(client);
        close(fd);
        return NULL;
    }
    
    buffer[bytes_received] = '\0';
    // Parse the formatted string to populate client struct
    if (sscanf(buffer, "ID:%d NAME:%127s", &client->client_id, client->client_name) != 2) {
        fprintf(stderr, "Failed to parse client details\n");
        free(client);
        close(fd);
        return NULL;
    }
    
    return client;
}

int send_formatted_message(int msg_type, const char *message, client_t *client) {
    if (!client || client->client_fd < 0 || !message) {
        return -1;
    }
    
    char formatted_msg[MAX_MESSAGE_LENGHT];
    int len = snprintf(formatted_msg, sizeof(formatted_msg), "User%d:#%d#%s", 
                      client->client_id, msg_type, message);
    
    if (len >= MAX_MESSAGE_LENGHT) {
        return -1;
    }
    
    if (send(client->client_fd, formatted_msg, len, 0) < 0) {
        perror("send formatted message");
        return -1;
    }
    
    client->last_activity = time(NULL);
    return 0;
}

client_t *auto_reconnect(const char *ip, int port) {
    int attempts = 0;
    const int max_attempts = 5;
    const int retry_delay = 2;

    while (attempts < max_attempts && client_running) {
        printf("Tentative de reconnexion %d/%d...\n", attempts + 1, max_attempts);
        client_t *client = connect_to_server(ip, port);
        if (client && client->client_fd > 0) {
            printf("Reconnecté !\n");
            return client;
        }
        
        attempts++;
        if (attempts < max_attempts) {
            sleep(retry_delay * attempts);
        }
    }
    
    return NULL;
}

int send_message_to_user(int receiver_id, const char *message, client_t *client) {
    if (!message || !client) {
        return -1;
    }
    
    char formatted_msg[MAX_MESSAGE_LENGHT];
    int len = snprintf(formatted_msg, sizeof(formatted_msg), "%%%d%%%s", receiver_id, message);
    
    if (len >= MAX_MESSAGE_LENGHT) {
        return -1;
    }
    
    return send_formatted_message(MSG_SEND, formatted_msg, client);
}

int is_client_running() {
    return client_running;
}

void set_client_running(int running) {
    client_running = running;
}
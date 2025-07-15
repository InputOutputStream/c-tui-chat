#ifndef CHAT_H
#define CHAT_H

#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ncurses.h>
#include <pthread.h>

#define MAX_CLIENT 250
#define MAX_CHATS 100
#define MAX_CHAT_MESSAGES_NUMBER 1000 
#define TIMEOUT 3600
#define MAX_MESSAGES_PER_MINUTE 1000
#define MAX_MESSAGE_LENGHT 1024
#define DEFAULT_PORT 8008
#define NAME_LENGHT 128

/**
 * Chat handling header file
 */

typedef struct MESSAGES {
    char payload[MAX_MESSAGE_LENGHT];
    size_t length;
    uint16_t sender_user_id;
    uint16_t receiver_user_id;
    time_t timestamp;
} message_t;

typedef struct CLIENT {
    struct sockaddr_in client_addr; 
    unsigned int client_addr_len;
    int client_id;
    char client_name[NAME_LENGHT];
    time_t connect_time;
    time_t last_activity;
    int client_fd;
    int msg_count;
    pthread_t thread; // Client execution thread
    int active;
} client_t;

typedef struct CHAT {
    // Properties of the chat
    uint16_t chat_id;
    message_t messages[MAX_CHAT_MESSAGES_NUMBER];
    size_t messages_count;
    client_t *participants[MAX_CLIENT];
    size_t participants_count;
} chat_t;

typedef struct CHAT_WINDOW_STRUCT {
    WINDOW *msg_win;
    WINDOW *input_win;
    WINDOW *users_win;
    WINDOW *status_win;
    int rows, cols; // Sera defini en fonction du contexte
} window_t;

typedef enum {
    MSG_WIN,
    INPUT_WIN,
    USER_WIN,
    STATUS_WIN,
} window_msg_t;

// Client message types (sent to server)
typedef enum {
    MSG_AUTH_REQ,           // Authentification
    MSG_SEND,               // Envoyer message privé
    MSG_CHANNEL,            // Send message to channel
    MSG_BROADCAST_CLIENTS,  // Message diffusé à tous
    MSG_CREATE_CHANNEL,     // créer un salon
    MSG_JOIN_CHANNEL,       // Rejoindre salon
    MSG_LEAVE_CHANNEL,      // Quitter salon
    MSG_LIST_USERS,         // Lister utilisateurs
    MSG_HEARTBEAT,          // Keep-alive
    MSG_DISCONNECT          // Déconnexion propre
} client_message_type_t;

// Server message types (received from server)
typedef enum {
    MSG_AUTH_ACK,           // Authentification réussie
    MSG_AUTH_FAIL,          // Authentification échouée
    MSG_USER_JOIN,          // Utilisateur rejoint
    MSG_BROADCAST,          // Message diffusé
    MSG_USER_LEAVE,         // Utilisateur part
    MSG_CHANNEL_CREATION,   // Confirmation de la création d'un canal
    MSG_USER_LIST,          // Liste des utilisateurs
    MSG_SERVER_ERROR,       // Erreur serveur
    MSG_PING,               // Ping du serveur
    MSG_SERVER_INFO,        // Info serveur
    MSG_MSG,                // Message privé
} server_message_type_t;

// Variables globales avec mutex pour le contrôle d'accès
extern client_t clients[MAX_CLIENT]; // Liste des clients en cours
extern chat_t chats[MAX_CHATS]; // Liste des chats en cours
extern int client_count; // Nombre de clients connectés
extern pthread_mutex_t clients_mutex; 
extern int server_running; 

#endif // !CHAT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "chat.h"
#include "server.h"

// Global variables
client_t clients[MAX_CLIENT];
chat_t chats[MAX_CHATS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t chats_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_running = 1;

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Démarrage de Low Chat Server...\n");
    
    // Initialize globals
    server_running = 1;
    client_count = 0;
    
    // Initialize mutexes
    if (pthread_mutex_init(&clients_mutex, NULL) != 0) {
        fprintf(stderr, "❌ Échec d'initialisation du mutex clients\n");
        exit(EXIT_FAILURE);
    }
    
    if (pthread_mutex_init(&chats_mutex, NULL) != 0) {
        fprintf(stderr, "❌ Échec d'initialisation du mutex chats\n");
        pthread_mutex_destroy(&clients_mutex);
        exit(EXIT_FAILURE);
    }
    
    // Initialize client and chat arrays
    memset(clients, 0, sizeof(clients));
    memset(chats, 0, sizeof(chats));
    
    if(start_message_server() != 0) {
        fprintf(stderr, "❌ Échec de démarrage du serveur\n");
        pthread_mutex_destroy(&clients_mutex);
        pthread_mutex_destroy(&chats_mutex);
        exit(EXIT_FAILURE);
    }

    printf("✅ Serveur démarré sur le port %d\n", DEFAULT_PORT);

    // Keep server running until signal
    while(server_running) {
        sleep(1);
    }
    
    // Cleanup
    printf("🔄 Nettoyage en cours...\n");
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&chats_mutex);
    
    printf("✅ Low Chat fermé\n");
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <ncurses.h>

#include <chat.h>
#include <server.h>


// Variables globales
client_t clients[MAX_CLIENT];
chat_t chats[MAX_CHATS];
int client_count = 0;
// int chat_count = 0;
int server_running = 1;

// pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_t chats_mutex = PTHREAD_MUTEX_INITIALIZER;

int find_or_create_chat(uint16_t user1_id, uint16_t user2_id);


// Variables pour l'interface
int current_chat_id = -1;  // -1 pour messages généraux, sinon ID du chat
int current_mode = 0;      // 0: général, 1: chat privé, 2: canal

// Fonction pour afficher les messages généraux (broadcast)
void display_general_messages(window_t *window) {
    werase(window->msg_win);
    box(window->msg_win, 0, 0);
    mvwprintw(window->msg_win, 0, 2, " Messages Généraux ");
    
    // Ici vous pourriez avoir un système de messages généraux
    // Pour l'instant, juste afficher un message d'info
    mvwprintw(window->msg_win, 2, 2, "Messages généraux du serveur...");
    
    wrefresh(window->msg_win);
}

// Fonction pour afficher les messages d'un chat spécifique
void display_chat_messages(window_t *window, int chat_id) {
    if (chat_id < 0 || chat_id >= MAX_CHATS) return;
    
    WINDOW *active_win = (current_mode == 1) ? window->pv_chat_win : window->channel_win;
    
    werase(active_win);
    box(active_win, 0, 0);
    
    // Titre
    if (current_mode == 1) {
        mvwprintw(active_win, 0, 2, " Chat Privé ");
    } else {
        mvwprintw(active_win, 0, 2, " Canal ");
    }
    
    //pthread_mutex_lock(&chats_mutex);
    
    // Afficher tous les messages du chat
    for (size_t i = 0; i < chats[chat_id].messages_count; i++) {
        message_t *msg = &chats[chat_id].messages[i];
        
        // Trouver le nom de l'expéditeur
        char sender_name[NAME_LENGHT] = "Inconnu";
        //pthread_mutex_lock(&clients_mutex);
        for (int j = 0; j < client_count; j++) {
            if (clients[j].client_id == msg->sender_user_id) {
                strncpy(sender_name, clients[j].client_name, NAME_LENGHT - 1);
                break;
            }
        }
        //pthread_mutex_unlock(&clients_mutex);
        
        // Formater le timestamp
        struct tm *tm_info = localtime(&msg->timestamp);
        char time_str[16];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
        
        // Afficher le message
        if (has_colors()) wattron(active_win, COLOR_PAIR(1));
        wprintw(active_win, "[%s] %s: %s\n", time_str, sender_name, msg->payload);
        if (has_colors()) wattroff(active_win, COLOR_PAIR(1));
    }
    
    //pthread_mutex_unlock(&chats_mutex);
    
    wrefresh(active_win);
}


// Fonction pour rafraîchir l'interface selon le mode actuel
void refresh_interface(window_t *window) {
    // Effacer toutes les fenêtres
    werase(window->msg_win);
    werase(window->pv_chat_win);
    werase(window->channel_win);
    
    // Dessiner les bordures
    box(window->msg_win, 0, 0);
    box(window->pv_chat_win, 0, 0);
    box(window->channel_win, 0, 0);
    box(window->users_win, 0, 0);
    box(window->input_win, 0, 0);
    
    // Titres
    mvwprintw(window->users_win, 0, 2, " Utilisateurs ");
    mvwprintw(window->input_win, 0, 2, " Saisie ");
    
    // Afficher selon le mode
    if (current_mode == 0) {
        // Mode général
        display_general_messages(window);
    } else if (current_chat_id >= 0) {
        // Mode chat privé ou canal
        display_chat_messages(window, current_chat_id);
    }
    
    wrefresh(window->users_win);
    wrefresh(window->input_win);
}


window_t *init_interface()
{
    initscr();              // Démarre ncurses
    cbreak();               // Pas de buffer ligne
    noecho();               // Ne pas afficher les touches
    keypad(stdscr, TRUE);   // Activer touches spéciales
    curs_set(1);            // Affiche le curseur
    
    if(has_colors())
    {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);    // Messages
        init_pair(2, COLOR_GREEN, COLOR_BLACK);   // Status
        init_pair(3, COLOR_YELLOW, COLOR_BLACK);  // Input msg text
        init_pair(4, COLOR_MAGENTA, COLOR_BLACK); // Channels
        init_pair(5, COLOR_RED, COLOR_BLACK);     // Notifications
    }
    
    window_t *window = calloc(1, sizeof(window_t));
    getmaxyx(stdscr, window->rows, window->cols);
    
    // Layout: Messages 70% | Users 30%
    int msg_width = (window->cols * 7) / 10;
    int users_width = window->cols - msg_width;
    
    // Fenêtre des messages (zone principale)
    window->msg_win = newwin(window->rows - 4, msg_width, 0, 0);
    
    // Fenêtre pour les chats privés (même taille que messages)
    window->pv_chat_win = newwin(window->rows - 4, msg_width, 0, 0);
    
    // Fenêtre pour les canaux (même taille que messages)
    window->channel_win = newwin(window->rows - 4, msg_width, 0, 0);
    
    // Fenêtre des utilisateurs (à droite)
    window->users_win = newwin(window->rows - 4, users_width, 0, msg_width);
    
    // Fenêtre de saisie (en bas)
    window->input_win = newwin(2, window->cols, window->rows - 3, 0);
    
    // Fenêtre de status (tout en bas)
    window->status_win = newwin(1, window->cols, window->rows - 1, 0);
    
    // Configuration des fenêtres
    scrollok(window->msg_win, TRUE);
    scrollok(window->pv_chat_win, TRUE);
    scrollok(window->channel_win, TRUE);
    
    // Dessiner les bordures et titres
    refresh_interface(window);
    
    // Initialiser le statut
    update_status(window, "Serveur démarré - Mode: Messages généraux");
    
    return window;
}


// Fonction pour ajouter un message à un chat (même si l'utilisateur n'est pas dans cette vue)
void add_message_to_chat(int chat_id, uint16_t sender_id, uint16_t receiver_id, const char *message) {
    if (chat_id < 0 || chat_id >= MAX_CHATS) return;
    
    //pthread_mutex_lock(&chats_mutex);
    
    if (chats[chat_id].messages_count < MAX_CHAT_MESSAGES_NUMBER) {
        message_t *msg = &chats[chat_id].messages[chats[chat_id].messages_count];
        
        strncpy(msg->payload, message, MAX_MESSAGE_LENGHT - 1);
        msg->payload[MAX_MESSAGE_LENGHT - 1] = '\0';
        msg->length = strlen(msg->payload);
        msg->sender_user_id = sender_id;
        msg->receiver_user_id = receiver_id;
        msg->timestamp = time(NULL);
        
        chats[chat_id].messages_count++;
    }
    
    //pthread_mutex_unlock(&chats_mutex);
}


// Fonction pour basculer vers un chat privé
void switch_to_private_chat(window_t *window, const char *username) {
    // Trouver l'utilisateur par nom
    int target_user_id = -1;
    // pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].client_name, username) == 0) {
            target_user_id = clients[i].client_id;
            break;
        }
    }
    // pthread_mutex_unlock(&clients_mutex);
    
    if (target_user_id == -1) {
        update_status(window, "Utilisateur non trouvé");
        return;
    }
    
    // Trouver ou créer le chat
    int chat_id = find_or_create_chat(0, target_user_id); // 0 = serveur/admin
    if (chat_id == -1) {
        update_status(window, "Impossible de créer le chat");
        return;
    }
    
    current_mode = 1;
    current_chat_id = chat_id;
    
    refresh_interface(window);
    
    char status[256];
    snprintf(status, sizeof(status), "Mode: Chat privé avec %s - /quit pour revenir", username);
    update_status(window, status);
}

// Fonction pour basculer vers un canal
void switch_to_channel(window_t *window, const char *channel_name) {
    current_mode = 2;
    // current_chat_id devrait être défini selon votre logique de canaux
    
    refresh_interface(window);
    
    char status[256];
    snprintf(status, sizeof(status), "Mode: Canal #%s - /leave pour quitter", channel_name);
    update_status(window, status);
}

// Fonction pour revenir aux messages généraux
void switch_to_general_messages(window_t *window) {
    current_mode = 0;
    current_chat_id = -1;
    
    refresh_interface(window);
    update_status(window, "Mode: Messages généraux");
}



void display_messages(window_t *window, const char *user_name, const char *messages, WINDOW *win_type)
{
    if(win_type == NULL) {
        if (current_mode == 0) win_type = window->msg_win;
        else if (current_mode == 1) win_type = window->pv_chat_win;
        else win_type = window->channel_win;
    }
        
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    
    if(has_colors())
        wattron(win_type, COLOR_PAIR(1));
        
    wprintw(win_type, "[%s] %s: %s\n", time_str, user_name, messages);
    
    if(has_colors())
        wattroff(win_type, COLOR_PAIR(1));
        
    wrefresh(win_type);
}

// void update_client_list(window_t *window)
// {
//     werase(window->users_win);
//     box(window->users_win, 0, 0);
//     mvwprintw(window->users_win, 0, 2, " Utilisateurs (%d) ", client_count);
    
//     pthread_mutex_lock(&clients_mutex);
//     for(int i = 0; i < client_count; i++)
//     {
//         if(clients[i].active)
//         {
//             // Afficher une notification si l'utilisateur a des messages non lus
//             int unread_messages = 0;
//             for (int j = 0; j < chat_count; j++) {
//                 // Logique pour compter les messages non lus (à implémenter)
//             }
            
//             if (unread_messages > 0) {
//                 if (has_colors()) wattron(window->users_win, COLOR_PAIR(5));
//                 mvwprintw(window->users_win, i+1, 1, "• %s (%d)", clients[i].client_name, unread_messages);
//                 if (has_colors()) wattroff(window->users_win, COLOR_PAIR(5));
//             } else {
//                 mvwprintw(window->users_win, i+1, 1, "• %s", clients[i].client_name);
//             }
//         }
//     }
//     pthread_mutex_unlock(&clients_mutex);
    
//     wrefresh(window->users_win);
// }

void update_status(window_t *window, const char *status) 
{
    werase(window->status_win);
    if (has_colors()) 
        wattron(window->status_win, COLOR_PAIR(2));
    mvwprintw(window->status_win, 0, 0, "%s", status);
    if (has_colors()) 
        wattroff(window->status_win, COLOR_PAIR(2));
    wrefresh(window->status_win);
}

// Fonction pour obtenir la fenêtre active
WINDOW* get_active_window(window_t *window, int mode) 
{
    switch(mode) {
        case 0: return window->msg_win;        // Messages généraux
        case 1: return window->pv_chat_win;    // Chat privé
        case 2: return window->channel_win;    // Canal
        default: return window->msg_win;
    }
}

void cleanup_interface(window_t *window) 
{
    if (window) {
        if (window->msg_win) delwin(window->msg_win);
        if (window->pv_chat_win) delwin(window->pv_chat_win);
        if (window->channel_win) delwin(window->channel_win);
        if (window->users_win) delwin(window->users_win);
        if (window->input_win) delwin(window->input_win);
        if (window->status_win) delwin(window->status_win);
        free(window);
    }
    endwin();
}

/**
 * Utilisation:
 * 
 * // Recevoir un message privé (depuis votre code serveur)
 * send_message_to_chat(sender_id, receiver_id, "Hello!", window);
 * 
 * // L'utilisateur bascule vers le chat privé
 * switch_to_private_chat(window, "Alice");
 * 
 * // Tous les messages précédents s'affichent automatiquement
 * 
 * Layout de l'interface :
 * 
 *  ┌─────────────────────────────┬──────────────┐
 *  │ Messages/Chat/Canal         │ Utilisateurs │
 *  │                             │              │
 *  │                             │ • User1      │
 *  │ [12:34:56] User1: Hello!    │ • User2 (2)  │ <- (2) = messages non lus
 *  │ [12:35:01] User2: Hi there! │ • User3      │
 *  │                             │              │
 *  ├─────────────────────────────┴──────────────┤
 *  │ > Votre message ici...                     │
 *  ├────────────────────────────────────────────┤
 *  │ Status: Mode Chat privé avec Alice         │
 *  └────────────────────────────────────────────┘
 */

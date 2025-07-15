#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <ncurses.h>

#include <chat.h>
#include <client.h>

static client_app_t *global_app = NULL;

void signal_handler(int sig) {
    switch (sig)
    {    
        default:
            if (global_app) {
                global_app->client_running = 0;
                set_client_running(0);
            }
            break;
    }
}

int main() {
    const char *str_addr = "127.0.0.1";
    const int port = 8008;
    
    client_app_t *app = calloc(1, sizeof(client_app_t));
    if (!app) {
        fprintf(stderr, "ERROR❌: Échec allocation mémoire\n");
        return EXIT_FAILURE;
    }
    
    app->client_running = 1;
    app->running = 1;
    global_app = app;
    
    // Initialize mutex
    if (pthread_mutex_init(&app->message_mutex, NULL) != 0) {
        fprintf(stderr, "ERROR❌: Échec initialisation mutex\n");
        free(app);
        return EXIT_FAILURE;
    }

    printf("Démarrage de Low Chat...\n");
    
    // Configuration signaux
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialisation interface
    app->window = init_interface();
    if (!app->window) {
        fprintf(stderr, "ERROR❌: Échec du démarrage de l'interface\n");
        pthread_mutex_destroy(&app->message_mutex);
        free(app);
        exit(EXIT_FAILURE);
    }
    
    update_status(app->window, "Connexion au serveur...");
    
    // Connexion au serveur
    app->client = connect_to_server(str_addr, port);
    if (!app->client || app->client->client_fd < 0) {
        update_status(app->window, "❌ Impossible de se connecter au serveur");
        cleanup_interface(app->window);
        pthread_mutex_destroy(&app->message_mutex);
        free(app);
        fprintf(stderr, "ERROR❌: Impossible de se connecter au serveur\n");
        return EXIT_FAILURE;
    }
    
    update_status(app->window, "✅ Connecté au serveur - Tapez vos messages");
    
    // Prepare arguments for receive thread
    args_t thread_args = {
        .window = app->window,
        .client = app->client
    };
    
    // Lancement thread de réception
    if (pthread_create(&app->receive_thread, NULL, receive_messages, &thread_args) != 0) {
        fprintf(stderr, "ERROR❌: Échec création thread de réception\n");
        cleanup_interface(app->window);
        close(app->client->client_fd);
        free(app->client);
        pthread_mutex_destroy(&app->message_mutex);
        free(app);
        return EXIT_FAILURE;
    }
    
    // Boucle principale avec interface ncurses
    char input_buffer[MAX_MESSAGE_LENGHT];
    int ch;
    int input_pos = 0;

    // Commandes list
    const char *commands[] = {
        "/help - Affiche cette aide",
        "/quit - Quitte le chat", 
        "/join <channel_id> - Rejoindre un canal",
        "/chat - Mode chat général",
        "/leave_channel - Quitter le canal actuel",
        "/create_channel <name> - Créer un nouveau canal"
    };
    const int commandes_number = sizeof(commands) / sizeof(commands[0]);

    while (app->client_running && is_client_running()) {
        ch = wgetch(app->window->input_win);
        
        switch (ch) {
            case '\n':
            case '\r':
            case KEY_ENTER:
                if (input_pos > 0) {
                    input_buffer[input_pos] = '\0';
                    
                    // Commandes
                    if (strcmp(input_buffer, "/help") == 0) {
                        display_messages(app->window, "Aide", "Commandes disponibles:");
                        for (int i = 0; i < commandes_number; i++) {
                            display_messages(app->window, "Commandes", commands[i]);
                        }
                    }
                    else if (strcmp(input_buffer, "/quit") == 0) {
                        app->client_running = 0;
                        set_client_running(0);
                    }
                    else if (strncmp(input_buffer, "/join ", 6) == 0) {
                        char* channel_id = get_command_arg(input_buffer);
                        if (channel_id) {
                            char formatted_msg[MAX_MESSAGE_LENGHT];
                            int len = snprintf(formatted_msg, sizeof(formatted_msg), "[%s]", channel_id);
                            
                            if (len < MAX_MESSAGE_LENGHT && send_formatted_message(MSG_JOIN_CHANNEL, formatted_msg, app->client) == 0) {
                                display_messages(app->window, "Système", "Demande de connexion au canal envoyée");
                            } else {
                                update_status(app->window, "❌ Erreur lors de la connexion au canal");
                            }
                        } else {
                            display_messages(app->window, "Erreur", "Usage: /join <channel_id>");
                        }
                    }
                    else if (strncmp(input_buffer, "/create_channel ", 16) == 0) {
                        char* channel_name = get_command_arg(input_buffer);
                        if (channel_name) {
                            if (send_formatted_message(MSG_CREATE_CHANNEL, channel_name, app->client) == 0) {
                                display_messages(app->window, "Système", "Demande de création de canal envoyée");
                            } else {
                                update_status(app->window, "❌ Erreur lors de la création du canal");
                            }
                        } else {
                            display_messages(app->window, "Erreur", "Usage: /create_channel <nom_du_canal>");
                        }
                    }
                    else if (strcmp(input_buffer, "/leave_channel") == 0) {
                        if (send_formatted_message(MSG_LEAVE_CHANNEL, "", app->client) == 0) {
                            display_messages(app->window, "Système", "Demande de sortie du canal envoyée");
                        } else {
                            update_status(app->window, "❌ Erreur lors de la sortie du canal");
                        }
                    }
                    else if (strcmp(input_buffer, "/chat") == 0) {
                        display_messages(app->window, "Info", "Vous êtes en mode chat général");
                    }
                    else {
                        // Message normal
                        display_messages(app->window, "Vous", input_buffer);
                        
                        if (send_formatted_message(MSG_BROADCAST_CLIENTS, input_buffer, app->client) < 0) {
                            update_status(app->window, "❌ Erreur envoi message");
                        }
                    }
                    
                    // Nettoyer la zone d'entrée
                    werase(app->window->input_win);
                    box(app->window->input_win, 0, 0);
                    mvwprintw(app->window->input_win, 0, 2, "Input: ");
                    wrefresh(app->window->input_win);
                    input_pos = 0;
                }
                break;
                
            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (input_pos > 0) {
                    input_pos--;
                    mvwdelch(app->window->input_win, 1, input_pos + 1);
                    wrefresh(app->window->input_win);
                }
                break;
                
            default:
                if (ch >= 32 && ch <= 126 && input_pos < MAX_MESSAGE_LENGHT - 1) {
                    input_buffer[input_pos++] = ch;
                    mvwaddch(app->window->input_win, 1, input_pos, ch);
                    wrefresh(app->window->input_win);
                }
                break;
        }
        
        // Mise à jour périodique de la liste des clients
        static time_t last_update = 0;
        time_t now = time(NULL);
        if (now - last_update > 1) {
            update_client_list(app->window);
            last_update = now;
        }
    }
    
    // Cleanup
    app->client_running = 0;
    set_client_running(0);
    
    if (app->client && app->client->client_fd >= 0) {
        send_formatted_message(MSG_DISCONNECT, "", app->client);
        close(app->client->client_fd);
    }
    
    // Wait for receive thread to finish
    pthread_cancel(app->receive_thread);
    pthread_join(app->receive_thread, NULL);

    cleanup_interface(app->window);
    
    if (app->client) {
        free(app->client);
    }
    
    pthread_mutex_destroy(&app->message_mutex);
    free(app);
    
    printf("✅ Chat fermé proprement\n");
    return 0;
}
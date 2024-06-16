#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>

#define PORT 8889
#define BUFFER_SIZE 1024

void *handle_client(void *client_socket);
void *server_thread(void *arg);
void *client_thread(void *arg);

char client_current_dir[BUFFER_SIZE];

int main() {
    pthread_t server_tid, client_tid;

    if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
        perror("Error al obtener el directorio actual del cliente");
        exit(EXIT_FAILURE);
    }

    // Crear y lanzar hilo del servidor
    if (pthread_create(&server_tid, NULL, server_thread, NULL) != 0) {
        perror("Error al crear el hilo del servidor");
        exit(EXIT_FAILURE);
    }

    // Crear y lanzar hilo del cliente
    if (pthread_create(&client_tid, NULL, client_thread, NULL) != 0) {
        perror("Error al crear el hilo del cliente");
        exit(EXIT_FAILURE);
    }

    // Esperar a que ambos hilos terminen
    pthread_join(server_tid, NULL);
    pthread_join(client_tid, NULL);

    return 0;
}

void *server_thread(void *arg) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;

    // Crear el socket del servidor
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("No se pudo crear el socket del servidor");
        exit(EXIT_FAILURE);
    }

    // Configurar la dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Enlazar el socket a la dirección y puerto
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al enlazar el socket del servidor a la dirección");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes
    if (listen(server_socket, 5) < 0) {
        perror("No se pudo escuchar en el socket del servidor");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Servidor FTP activo en el puerto %d. Esperando conexiones...\n", PORT);

    // Bucle para aceptar conexiones de clientes
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Error al aceptar la conexión del cliente");
            continue;
        }

        printf("Nuevo cliente conectado\n");

        // Crear un hilo para manejar la conexión del cliente
        if (pthread_create(&thread_id, NULL, handle_client, (void *)&client_socket) != 0) {
            perror("Error al crear un hilo para manejar al cliente");
            close(client_socket);
        } else {
            pthread_detach(thread_id);  // Separar el hilo
        }
    }

    close(server_socket);
    return NULL;
}

void *client_thread(void *arg) {
    int client_socket = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];

    while (1) {
        printf("ftp> ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;

        char *cmd = strtok(command, " ");
        char *arg = strtok(NULL, "");

        if (strcmp(cmd, "open") == 0) {
            if (arg == NULL) {
                printf("Uso correcto: open <dirección-ip>\n");
                continue;
            }

            client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket < 0) {
                perror("No se pudo crear el socket del cliente");
                continue;
            }

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            if (inet_pton(AF_INET, arg, &server_addr.sin_addr) <= 0) {
                perror("Dirección IP no válida");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                perror("No se pudo conectar al servidor");
                close(client_socket);
                client_socket = -1;
                continue;
            }

            printf("Conectado al servidor %s en el puerto %d\n", arg, PORT);
        } else if (strcmp(cmd, "close") == 0) {
            if (client_socket != -1) {
                close(client_socket);
                client_socket = -1;
                printf("Conexión cerrada\n");
            } else {
                printf("No hay ninguna conexión activa\n");
            }
        } else if (strcmp(cmd, "quit") == 0) {
            if (client_socket != -1) {
                close(client_socket);
            }
            printf("Saliendo del programa FTP...\n");
            exit(0);
        } else if (strcmp(cmd, "lcd") == 0) {
            if (arg == NULL) {
                printf("Uso correcto: lcd <directorio>\n");
                continue;
            }

            if (chdir(arg) == 0) {
                printf("Directorio local cambiado a: %s\n", arg);
                if (getcwd(client_current_dir, sizeof(client_current_dir)) == NULL) {
                    perror("No se pudo obtener el nuevo directorio actual");
                }
            } else {
                perror("Error al cambiar el directorio local");
            }
        } else if (client_socket == -1) {
            printf("Debe conectarse a un servidor primero usando el comando 'open <dirección-ip>'\n");
        } else {
            if (strcmp(cmd, "put") == 0) {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", client_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    perror("No se pudo abrir el archivo local para enviar");
                    continue;
                }

                fseek(file, 0, SEEK_END);
                long file_size = ftell(file);
                fseek(file, 0, SEEK_SET);
                char *file_content = malloc(file_size);
                fread(file_content, 1, file_size, file);
                fclose(file);

                snprintf(buffer, BUFFER_SIZE, "put %s %ld", arg, file_size);
                send(client_socket, buffer, strlen(buffer), 0);
                send(client_socket, file_content, file_size, 0);
                free(file_content);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Servidor: %s\n", buffer);
                }
            } else if (strcmp(cmd, "get") == 0) {
                snprintf(buffer, BUFFER_SIZE, "get %s", arg);
                send(client_socket, buffer, strlen(buffer), 0);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    long file_size = atol(buffer);

                    char *file_content = malloc(file_size);
                    bytes_received = recv(client_socket, file_content, file_size, 0);
                    if (bytes_received > 0) {
                        printf("Recibiendo archivo: %s\n", arg);

                        char file_path[BUFFER_SIZE];
                        snprintf(file_path, BUFFER_SIZE, "%s/%s", client_current_dir, arg);
                        FILE *file = fopen(file_path, "wb");
                        if (file == NULL) {
                            perror("No se pudo crear el archivo local");
                            free(file_content);
                            continue;
                        }
                        fwrite(file_content, 1, file_size, file);
                        fclose(file);

                        printf("Archivo %s recibido correctamente\n", arg);
                    }
                    free(file_content);
                }
            } else {
                snprintf(buffer, BUFFER_SIZE, "%s %s", cmd, arg ? arg : "");
                send(client_socket, buffer, strlen(buffer), 0);

                int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Servidor: %s\n", buffer);
                }
            }
        }
    }

    return NULL;
}

void *handle_client(void *client_socket) {
    int socket = *(int *)client_socket;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    char server_current_dir[BUFFER_SIZE];

    if (getcwd(server_current_dir, sizeof(server_current_dir)) == NULL) {
        perror("No se pudo obtener el directorio actual del servidor");
        close(socket);
        return NULL;
    }

    while ((bytes_read = recv(socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Comando recibido: %s\n", buffer);

        char *cmd = strtok(buffer, " ");
        char *arg = strtok(NULL, " ");
        char response[BUFFER_SIZE];

        if (strcmp(cmd, "cd") == 0) {
            if (arg == NULL) {
                snprintf(response, BUFFER_SIZE, "¡Ups! No especificaste un directorio.\n");
            } else {
                char new_dir[BUFFER_SIZE];
                snprintf(new_dir, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                if (chdir(new_dir) == 0) {
                    snprintf(response, BUFFER_SIZE, "¡Perfecto! Cambiado al directorio %s\n", arg);
                    strcpy(server_current_dir, new_dir);
                } else {
                    snprintf(response, BUFFER_SIZE, "Error: No se pudo cambiar al directorio %s\n", arg);
                }
            }
        } else if (strcmp(cmd, "ls") == 0) {
            DIR *d;
            struct dirent *dir;
            d = opendir(server_current_dir);
            if (d) {
                response[0] = '\0';
                while ((dir = readdir(d)) != NULL) {
                    strcat(response, dir->d_name);
                    strcat(response, "\n");
                }
                closedir(d);
            } else {
                snprintf(response, BUFFER_SIZE, "Error: No se pudo listar el directorio\n");
            }
        } else if (strcmp(cmd, "pwd") == 0) {
            snprintf(response, BUFFER_SIZE, "Directorio actual: %s\n", server_current_dir);
        } else if (strcmp(cmd, "get") == 0) {
            if (arg == NULL) {
                snprintf(response, BUFFER_SIZE, "¡Ups! No especificaste un archivo para descargar.\n");
            } else {
                char file_path[BUFFER_SIZE];
                snprintf(file_path, BUFFER_SIZE, "%s/%s", server_current_dir, arg);
                FILE *file = fopen(file_path, "rb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "Error: El archivo %s no se encontró\n", arg);
                } else {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    char *file_content = malloc(file_size);
                    fread(file_content, 1, file_size, file);
                    fclose(file);

                    snprintf(response, BUFFER_SIZE, "%ld", file_size);
                    send(socket, response, strlen(response), 0);
                    send(socket, file_content, file_size, 0);
                    free(file_content);
                }
            }
        } else if (strcmp(cmd, "put") == 0) {
            long file_size = atol(arg);
            char *file_content = malloc(file_size);
            bytes_read = recv(socket, file_content, file_size, 0);

            if (bytes_read != file_size) {
                snprintf(response, BUFFER_SIZE, "Error: No se recibió el archivo completo\n");
                send(socket, response, strlen(response), 0);
            } else {
                FILE *file = fopen(buffer, "wb");
                if (file == NULL) {
                    snprintf(response, BUFFER_SIZE, "Error: No se pudo crear el archivo en el servidor\n");
                } else {
                    fwrite(file_content, 1, file_size, file);
                    fclose(file);
                    snprintf(response, BUFFER_SIZE, "Transferencia completada. Archivo %s guardado.\n", buffer);
                }
                free(file_content);
                send(socket, response, strlen(response), 0);
            }
        } else {
            snprintf(response, BUFFER_SIZE, "Error: Comando '%s' no implementado\n", cmd);
        }

        if (strcmp(cmd, "get") != 0 && strcmp(cmd, "put") != 0) {
            send(socket, response, strlen(response), 0);
        }
    }

    close(socket);
    printf("Cliente desconectado\n");
    return NULL;
}

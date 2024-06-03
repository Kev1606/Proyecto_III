#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT 8889
#define BUF_SIZE 1024
#define CMD_SIZE 256

void *handle_connection(void *arg);
void *handle_user_input(void *arg);
void execute_command(int sock, char *command);
void send_file(int sock, char *filename);
void receive_file(int sock, char *filename);

int server_sock, client_sock;
struct sockaddr_in server_addr, client_addr;
socklen_t client_addr_len = sizeof(client_addr);

int main() {
    pthread_t thread_id;

    // Crear socket del servidor
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Configurar dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Vincular el socket con la dirección
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Escuchar conexiones entrantes
    if (listen(server_sock, 5) < 0) {
        perror("listen");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Crear un thread para manejar el input del usuario
    pthread_create(&thread_id, NULL, handle_user_input, NULL);
    pthread_detach(thread_id);

    printf("Servidor FTP esperando conexiones en el puerto %d...\n", PORT);

    while (1) {
        // Aceptar conexiones entrantes
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("accept");
            close(server_sock);
            exit(EXIT_FAILURE);
        }

        printf("Conexión aceptada de %s\n", inet_ntoa(client_addr.sin_addr));

        // Crear un thread para manejar la conexión entrante
        pthread_create(&thread_id, NULL, handle_connection, (void *)&client_sock);
        pthread_detach(thread_id);
    }

    close(server_sock);
    return 0;
}



void *handle_connection(void *arg) {
    int sock = *(int *)arg;
    char buffer[BUF_SIZE];
    int bytes_read;

    while ((bytes_read = recv(sock, buffer, BUF_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Comando recibido: %s\n", buffer);
        execute_command(sock, buffer);
    }

    if (bytes_read == 0) {
        printf("Conexión cerrada por el cliente\n");
    } else if (bytes_read < 0) {
        perror("recv");
    }

    close(sock);
    return NULL;
}


void *handle_user_input(void *arg) {
    char command[CMD_SIZE];
    char server_ip[16];
    int sock;
    struct sockaddr_in remote_addr;

    while (1) {
        printf("bftp> ");
        fgets(command, CMD_SIZE, stdin);
        command[strcspn(command, "\n")] = '\0'; // Remover newline

        if (strncmp(command, "open ", 5) == 0) {
            sscanf(command + 5, "%s", server_ip);

            // Crear socket del cliente
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                perror("socket");
                continue;
            }

            // Configurar dirección remota
            remote_addr.sin_family = AF_INET;
            remote_addr.sin_addr.s_addr = inet_addr(server_ip);
            remote_addr.sin_port = htons(PORT);

            // Conectar al servidor remoto
            if (connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
                perror("connect");
                close(sock);
                continue;
            }

            printf("Conectado a %s\n", server_ip);
        } else if (strcmp(command, "close") == 0) {
            close(sock);
            printf("Conexión cerrada\n");
        } else if (strcmp(command, "quit") == 0) {
            close(sock);
            exit(0);
        } else {
            // Enviar comando al servidor remoto
            if (send(sock, command, strlen(command), 0) < 0) {
                perror("send");
            }
        }
    }
    return NULL;
}


void execute_command(int sock, char *command) {
    if (strncmp(command, "get ", 4) == 0) {
        send_file(sock, command + 4);
    } else if (strncmp(command, "put ", 4) == 0) {
        receive_file(sock, command + 4);
    } else if (strcmp(command, "ls") == 0) {
        DIR *d;
        struct dirent *dir;
        char buffer[BUF_SIZE];

        d = opendir(".");
        if (d) {
            while ((dir = readdir(d)) != NULL) {
                snprintf(buffer, BUF_SIZE, "%s\n", dir->d_name);
                send(sock, buffer, strlen(buffer), 0);
            }
            closedir(d);
        }
    } else if (strncmp(command, "cd ", 3) == 0) {
        chdir(command + 3);
    } else if (strcmp(command, "pwd") == 0) {
        char cwd[BUF_SIZE];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            send(sock, cwd, strlen(cwd), 0);
        }
    }
}

void send_file(int sock, char *filename) {
    int fd;
    char buffer[BUF_SIZE];
    int bytes_read;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    while ((bytes_read = read(fd, buffer, BUF_SIZE)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    close(fd);
}

void receive_file(int sock, char *filename) {
    int fd;
    char buffer[BUF_SIZE];
    int bytes_read;

    fd = open(filename, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror("open");
        return;
    }

    while ((bytes_read = recv(sock, buffer, BUF_SIZE, 0)) > 0) {
        write(fd, buffer, bytes_read);
    }

    close(fd);
}

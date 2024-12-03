#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "stream.h"
#include <ctype.h>
#include <semaphore.h>
#include "error_codes.h"
#define MAX_PATHS 100
#define MAX_CLIENTS 20
#define BUFFER_SIZE 8192
#define HEARTBEATTIME 5
#define PATH_MAX_LENGTH 100
#define TIMEOUT 5
#define MAX_READS 10
int nm_socket;
pthread_mutex_t mutex_client = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_port = PTHREAD_MUTEX_INITIALIZER;
int present_client_port;
typedef struct Client
{
    int client_sock;
    char client_ip[INET_ADDRSTRLEN];
    struct Client *next;
} Client;
Client *client_list = NULL;
typedef struct Hashtable
{
    char *paths[MAX_PATHS];
    int count_paths;
} Hashtable;
Hashtable paths_accessible;
struct client_handler_args
{
    char *ip_ss;
    int port_client;
    int nm_port;
};
struct nm_handler_args
{
    char *ip_ss;
    int port_nm;
};
struct client_handling_args
{
    int *connection_client; // Client socket
    int nm_port;            // Port for NM
    char *ip_ss;
    int port_client; // Storage server IP address
};
typedef struct fileLock
{
    sem_t write_sem;
    sem_t read_sem;
} fileLock;

fileLock FileLock[MAX_PATHS];

int get_hash(const char *s, int table_size)
{
    unsigned long long hash = 0;
    unsigned long long p = 31;
    unsigned long long m = 1e9 + 9;
    unsigned long long p_pow = 1;

    for (int i = 0; s[i] != '\0'; i++)
    {
        char c = tolower(s[i]);
        hash = (hash + (c - ' ' + 1) * p_pow) % m;
        p_pow = (p_pow * p) % m;
    }

    return hash % table_size;
}

void init_semaphores()
{
    for (int i = 0; i < MAX_PATHS; i++)
    {
        sem_init(&FileLock[i].write_sem, 0, 1);
        sem_init(&FileLock[i].read_sem, 0, MAX_READS);
    }
}

int check_sem(sem_t *x, int client_sock)
{
    if (sem_trywait(x) == -1)
    {
        printf("\nConcurrent limit reached! Wait for some time :(\n");
        char message[MAX_PATHS];
        snprintf(message, MAX_PATHS, "Wait for some time :(");
        send(client_sock, message, strlen(message), 0);
        return -1;
    }
    return 1;
}
int checkFileOrFolder(const char *path)
{
    struct stat pathStat;

    if (stat(path, &pathStat) == 0)
    {
        if (S_ISREG(pathStat.st_mode))
        {
            return 1;
        }
        else if (S_ISDIR(pathStat.st_mode))
        {
            return 0;
        }
    }
    else
    {
        perror("stat");
        printf("Invalid path or cannot access it.\n");
    }
}
void initialisation_ht(Hashtable *ht)
{
    for (int i = 0; i < MAX_PATHS; i++)
    {
        ht->paths[i] = NULL;
    }
    ht->count_paths = 0;
}
void addpath_ht(Hashtable *ht, char *newpath)
{
    if (ht->count_paths >= MAX_PATHS)
    {
        printf("Exceeded limit of paths.\n");
    }
    else
    {
        ht->paths[ht->count_paths] = strdup(newpath);
        ht->count_paths++;
    }
}
int valid_path(char *path)
{
    struct stat validpaths;
    return (stat(path, &validpaths) == 0);
}
void input_paths()
{
    char path[1024];
    printf("Enter the accessible paths:\n");
    while (1)
    {
        printf("Path: ");
        fgets(path, sizeof(path), stdin);
        path[strcspn(path, "\n")] = '\0';
        if (strcmp(path, "finish") == 0)
        {
            break;
        }
        if (!valid_path(path))
        {
            printf("Invalid path. Enter correct path.\n");
        }
        else
        {
            addpath_ht(&paths_accessible, path);
        }
    }
}

void *send_heartbeat(void *arg)
{
    int *socket = (int *)arg;
    char heartbeat_msg[] = "HEARTBEAT\n";
    char response[BUFFER_SIZE];

    while (1)
    {
        // printf("Sending heartbeat...\n");

        // Send heartbeat with error checking
        ssize_t sent = send(*socket, heartbeat_msg, strlen(heartbeat_msg), 0);
        if (sent < 0)
        {
            perror("Failed to send heartbeat");
            break;
        }

        // Wait for acknowledgment with timeout
        struct timeval tv;
        tv.tv_sec = 2; // 2 second timeout
        tv.tv_usec = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(*socket, &readfds);

        int ready = select(*socket + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0)
        {
            ssize_t bytes_received = recv(*socket, response, sizeof(response) - 1, 0);
            if (bytes_received > 0)
            {
                response[bytes_received] = '\0';
                // printf("Heartbeat acknowledged: %s\n", response);
            }
            else if (bytes_received == 0)
            {
                printf("Connection closed by naming server\n");
                break;
            }
            else
            {
                perror("Error receiving acknowledgment");
                break;
            }
        }

        sleep(5); // Send heartbeat every 5 seconds
    }

    // If we break from the loop, the connection is dead
    printf("Heartbeat thread exiting\n");
    return NULL;
}

void add_client(int client_sock, const char *client_ip)
{
    Client *new_client = malloc(sizeof(Client));
    if (new_client == NULL)
    {
        perror("Error allocating memory for new client");
        return;
    }

    new_client->client_sock = client_sock;
    strncpy(new_client->client_ip, client_ip, INET_ADDRSTRLEN);
    new_client->next = client_list; // Add at the beginning of the list
    client_list = new_client;

    printf("New client connected: %s\n", client_ip);
}

int path_exists_ht(Hashtable *ht, const char *path)
{
    for (int i = 0; i < ht->count_paths; i++)
    {
        if (ht->paths[i] != NULL && strcmp(ht->paths[i], path) == 0)
        {
            return 1; // Path exists
        }
    }
    return 0; // Path does not exist
}

void send_data_to_nm(char *path, char *file_dir)
{
    char buffer[PATH_MAX_LENGTH + 5];
    char ack[PATH_MAX_LENGTH];
    snprintf(buffer, sizeof(buffer), "%s %s\n", path, file_dir);
    send(nm_socket, buffer, strlen(buffer), 0);

    // Wait for acknowledgment
    int ack_received = recv(nm_socket, ack, sizeof(ack) - 1, 0);
    if (ack_received > 0)
    {
        ack[ack_received] = '\0';
        if (strncmp(ack, "Path Already exists in one of the servers.", 12) == 0)
        {
            printf("It is there\n");
        }
        else
        {
            addpath_ht(&paths_accessible, path);
        }
        printf("Acknowledgment: %s\n", ack);
    }
}

void list_directory_recursive(const char *base_path, const char *current_path, int level)
{
    struct dirent *entry;
    DIR *dir;
    char full_path[PATH_MAX_LENGTH]; // Increased buffer size
    char entry_path[PATH_MAX_LENGTH];

    // Combine base path and current path
    if (strcmp(current_path, ".") == 0)
    {
        // For the root level, just use the base_path
        snprintf(full_path, sizeof(full_path), "%s", base_path);
    }
    else
    {
        if (snprintf(full_path, sizeof(full_path), "%s/%s", base_path, current_path) >= sizeof(full_path))
        {
            fprintf(stderr, "Path too long: %s/%s\n", base_path, current_path);
            return;
        }
    }

    dir = opendir(full_path);

    if (dir == NULL)
    {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        // Skip "." and ".."
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
        {
            continue;
        }

        // Construct the full path for the current entry
        if (snprintf(entry_path, sizeof(entry_path), "%s/%s", full_path, entry->d_name) >= sizeof(entry_path))
        {
            fprintf(stderr, "Path too long: %s/%s\n", full_path, entry->d_name);
            continue;
        }

        // Print the full path
        for (int i = 0; i < level; i++)
            printf("  "); // Indentation
        printf("%s\n", entry_path);
        char buffer[PATH_MAX_LENGTH + 200];
        char ack[PATH_MAX_LENGTH];
        if (path_exists_ht(&paths_accessible, entry_path) == 0)
        {
            // send_data_to_nm(entry_path);
            struct stat path_stat;
            int path_type = 1; // Default to file

            if (stat(entry_path, &path_stat) == 0)
            {
                if (S_ISDIR(path_stat.st_mode))
                {
                    path_type = 2; // Directory
                }
            }
            char is_file_dir[20];
            if (path_type == 1)
            {
                strcpy(is_file_dir, "FILE");
            }
            else if (path_type == 2)
            {
                strcpy(is_file_dir, "DIRECTORY");
            }
            snprintf(buffer, sizeof(buffer), "%s %s\n", entry_path, is_file_dir);
            send(nm_socket, buffer, strlen(buffer), 0);

            int ack_received = recv(nm_socket, ack, sizeof(ack) - 1, 0);
            if (ack_received > 0)
            {
                ack[ack_received] = '\0';
                if (strncmp(ack, "Path Already exists in one of the servers.", 12) == 0)
                {
                    printf("It is there\n");
                }
                else
                {
                    addpath_ht(&paths_accessible, entry_path);
                }
                printf("Acknowledgment: %s\n", ack);
            }

            // addpath_ht(&paths_accessible, entry_path);
        }
        else
        {
            printf("Path already exists %s\n\n", entry_path);
        }

        // Check if the entry is a directory
        struct stat path_stat;
        if (stat(entry_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode))
        {
            // Recurse into the directory
            list_directory_recursive(base_path, entry_path + strlen(base_path) + 1, level + 1);
        }
    }

    closedir(dir);
}

int check_path_type(char *path)
{
    struct stat path_stat;
    if (stat(path, &path_stat) != 0)
    {
        perror("stat");
        return 0; // Error occurred
    }
    int path_type = 0;
    if (S_ISREG(path_stat.st_mode))
    {
        printf("%s is a file.\n", path);
        return 1; // It's a file
    }
    else if (S_ISDIR(path_stat.st_mode))
    {
        printf("%s is a directory.\n", path);
        path_type = 2;
        char buffer[PATH_MAX_LENGTH];
        char ack[PATH_MAX_LENGTH];
        if (path_exists_ht(&paths_accessible, path) == 0)
        {
            // send_data_to_nm(path);
            char *is_dir = "DIRECTORY";
            snprintf(buffer, sizeof(buffer), "%s %s\n", path, is_dir);
            send(nm_socket, buffer, strlen(buffer), 0);

            // Wait for acknowledgment
            int ack_received = recv(nm_socket, ack, sizeof(ack) - 1, 0);
            if (ack_received > 0)
            {
                ack[ack_received] = '\0';
                if (strncmp(ack, "Path Already exists in one of the servers.", 12) == 0)
                {
                    printf("It is there\n");
                    printf("Acknowledgment: %s\n", ack);
                }
                else
                {
                    addpath_ht(&paths_accessible, path);
                    printf("Acknowledgment: %s\n", ack);
                    list_directory_recursive(path, ".", 0);
                }
            }
        }
        else
        {
            printf("Path already exists %s in ss\n\n", path);
        }

        return 2; // It's a directory
    }
    else
    {
        printf("%s is neither a regular file nor a directory.\n", path);
        return 0; // Neither
    }
    return 0;
}

void registration_with_nm(char *ip_nm, int port_nm, char *ip_ss, int port_ss, int port_client)
{
    char buffer[BUFFER_SIZE];
    //   int nm_socket;
    struct sockaddr_in ss;

    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket < 0)
    {
        perror("Error in creating the socket");
        exit(0);
    }

    int opt = 1;
    if (setsockopt(nm_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        close(nm_socket);
        exit(0);
    }

    ss.sin_port = htons(port_nm);
    ss.sin_family = AF_INET;

    if (inet_pton(AF_INET, ip_nm, &ss.sin_addr) <= 0)
    {
        printf("Invalid address.Address is not valid\n");
        exit(0);
    }

    if (connect(nm_socket, (struct sockaddr *)&ss, sizeof(ss)) < 0)
    {
        perror("Error in connecting to the naming server");
        exit(0);
    }

    snprintf(buffer, sizeof(buffer), "%s %d %d", ip_ss, port_ss, port_client);
    send(nm_socket, buffer, strlen(buffer), 0);

    char ack[BUFFER_SIZE];
    char path[1024];
    int check_backup = 0;
    int data_read = recv(nm_socket, ack, sizeof(ack), 0);
    ack[data_read] = '\0';
    printf("%s----\n", ack);
    if (strncmp(ack, "This storage server already exists! :)\n", strlen("This storage server already exists! :)\n")) == 0)
    {
        return;
    }
    while (1)
    {
        if (check_backup == 0)
        {
            printf("Path to store backup files of other ss: ");
        }
        else
        {
            printf("Path: ");
        }
        fgets(path, sizeof(path), stdin);
        path[strcspn(path, "\n")] = '\0';
        int path_add_dir = paths_accessible.count_paths;
        int check_path = check_path_type(path);
        if (strcmp(path, "finish") == 0)
        {
            send(nm_socket, "END_PATHS\n", 10, 0);

            // Final acknowledgment for end of paths
            int final_ack_received = recv(nm_socket, ack, sizeof(ack) - 1, 0);
            if (final_ack_received > 0)
            {
                ack[final_ack_received] = '\0';
                printf("Final acknowledgment: %s\n\n", ack);
            }
            break;
        }
        if (check_path == 0)
        {
            printf("Invalid path. Enter correct path.\n");
            continue;
        }
        else if (check_path == 2)
        { // directory
            check_backup++;
            printf("back up = %d\n", check_backup);
            // EAT FIVE STAR DO NOTHING!!
        }
        else if (path_exists_ht(&paths_accessible, path) == 0)
        {

            if (check_path == 1 && check_backup != 0)
            { // file
                usleep(45000);
                char *is_file = "FILE";
                send_data_to_nm(path, is_file);
            }
            else if (check_path == 1 && check_backup == 0)
            {
                printf("Please enter a valid directory, files are not allowed to store files/directories\n");
            }
            // else if(check_path == 2){           // directory
            //     check_backup++;
            //     printf("back up = %d\n",check_backup);
            //     // EAT FIVE STAR DO NOTHING!!
            // }
        }
        else
        {
            printf("Path already exists %s\n\n", path);
            continue;
        }
    }
}
int delete_directory_recursively(const char *dir_path)
{
    struct dirent *entry;
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        perror("Error opening directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat path_stat;
        if (stat(path, &path_stat) == -1)
        {
            perror("Error getting file status");
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(path_stat.st_mode))
        {
            if (delete_directory_recursively(path) != 0)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            if (remove(path) != 0)
            {
                perror("Error deleting file");
                closedir(dir);
                return -1;
            }
        }
    }
    closedir(dir);
    if (rmdir(dir_path) != 0)
    {
        perror("Error deleting directory");
        return -1;
    }

    return 0;
}

void copy_file(const char *src_path, const char *dest_path)
{
    FILE *src_file = fopen(src_path, "rb");
    if (src_file == NULL)
    {
        perror("Error opening source file");
        return;
    }
    FILE *dest_file = fopen(dest_path, "wb");
    if (dest_file == NULL)
    {
        perror("Error opening destination file");
        fclose(src_file);
        return;
    }
    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, src_file)) > 0)
    {
        fwrite(buffer, 1, bytes, dest_file);
    }
    fclose(src_file);
    fclose(dest_file);
}

void copy_directory(const char *src_dir, const char *dest_dir)
{
    DIR *dir = opendir(src_dir);
    if (dir == NULL)
    {
        perror("Error opening source directory");
        return;
    }
    mkdir(dest_dir, 0755);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char src_path[BUFFER_SIZE];
        char dest_path[BUFFER_SIZE];

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            copy_directory(src_path, dest_path);
        }
        else
        {
            copy_file(src_path, dest_path);
        }
    }

    closedir(dir);
}
void receive_and_write_file(int client_sock, int file_fd)
{
    char receive_buffer[1024];
    ssize_t bytes_received;

    while ((bytes_received = recv(client_sock, receive_buffer, sizeof(receive_buffer), 0)) > 0)
    {
        receive_buffer[bytes_received] = '\0';
        char *end_marker_pos = strstr(receive_buffer, "FILE_TRANSFER_END");

        if (end_marker_pos != NULL)
        {
            size_t data_len = end_marker_pos - receive_buffer;

            if (write(file_fd, receive_buffer, data_len) < 0)
            {
                perror("Error writing final data to file");
                break;
            }

            printf("End marker found. File transfer complete.\n");
            break;
        }
        if (write(file_fd, receive_buffer, bytes_received) < 0)
        {
            perror("Error writing to file");
            break;
        }
    }
    if (bytes_received < 0)
    {
        perror("Receive error");
    }
    else
    {
        printf("File data received and written successfully.\n");
        const char *success_message = "File data received and written successfully.";
        send(client_sock, success_message, strlen(success_message), 0);
    }
    close(file_fd);
}
void send_file(int client_sock, const char *file_path)
{
    char buffer[BUFFER_SIZE];
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0)
    {
        perror("Error opening file");
        return;
    }

    // Notify the client of the file being sent
    snprintf(buffer, sizeof(buffer), "FILE_START:%s\n", file_path);
    send(client_sock, buffer, strlen(buffer), 0);
    char new_buffer[BUFFER_SIZE];
    int data = recv(client_sock, new_buffer, sizeof(new_buffer), 0);

    // Read and send file data
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0)
    {
        send(client_sock, buffer, bytes_read, 0);
        data = recv(client_sock, new_buffer, sizeof(new_buffer), 0);
    }

    // Notify the client that the file transfer is complete
    const char *end_message = "FILE_TRANSFER_END\n";
    send(client_sock, end_message, strlen(end_message), 0);
    data = recv(client_sock, new_buffer, sizeof(new_buffer), 0);
    close(file_fd);
}

void send_directory(int client_sock, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    struct stat entry_stat;
    char full_path[BUFFER_SIZE];

    // Notify the client of the directory being sent
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "DIR_START:%s\n", dir_path);
    send(client_sock, buffer, strlen(buffer), 0);
    char new_buffer[BUFFER_SIZE];
    int data = recv(client_sock, new_buffer, sizeof(new_buffer), 0);

    while ((entry = readdir(dir)) != NULL)
    {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        // Get file or directory stats
        if (stat(full_path, &entry_stat) < 0)
        {
            perror("Error reading file/directory stats");
            continue;
        }

        if (S_ISREG(entry_stat.st_mode))
        {
            // If it's a file, send it
            send_file(client_sock, full_path);
        }
        else if (S_ISDIR(entry_stat.st_mode))
        {
            // If it's a directory, recursively traverse it
            send_directory(client_sock, full_path);
        }
    }

    // Notify the client that the directory transfer is complete
    const char *end_message = "DIR_TRANSFER_END\n";
    send(client_sock, end_message, strlen(end_message), 0);
    data = recv(client_sock, new_buffer, sizeof(new_buffer), 0);
    closedir(dir);
}

void receive_file_system(int sock, const char *dest_path)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    while (1)
    {
        // Receive the directory or file information
        // printf("1\n");
        bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        // printf("ggg%ld\n",bytes_received);
        // printf("%s\n",buffer);
        if (bytes_received <= 0)
        {
            perror("Failed to receive data");
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the buffer

        // Handle the message type (DIR, FILE, or END)
        if (strncmp(buffer, "DIR:", 4) == 0)
        {
            // Create the directory (if it doesn't already exist)
            char dir_path[4096];
            snprintf(dir_path, sizeof(dir_path), "%s", buffer + 4);
            dir_path[sizeof(dir_path) - 1] = '\0';
            printf("%s\n", dir_path);
            struct stat st;
            if (stat(dir_path, &st) != 0)
            {
                if (mkdir(dir_path, 0755) != 0)
                {
                    perror("Failed to create directory");
                }
            }
            snprintf(buffer, sizeof(buffer), "File Created");
            send(sock, buffer, strlen(buffer), 0);
            printf("Created directory: %s\n", dir_path);
        }
        else if (strncmp(buffer, "FILE:", 5) == 0)
        {
            // File transfer begins
            char file_path[4096];
            snprintf(file_path, sizeof(file_path), "%s", buffer + 5);
            printf("%s\n", file_path);
            // Open the file for writing
            int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1)
            {
                perror("Failed to open file");
                continue;
            }
            snprintf(buffer, sizeof(buffer), "File Created");
            send(sock, buffer, strlen(buffer), 0);
            // Receive the file data in chunks
            while (1)
            {
                bytes_received = recv(sock, buffer, sizeof(buffer), 0);
                if (bytes_received <= 0)
                {
                    perror("Failed to receive file data");
                    break;
                }

                if (strncmp(buffer, "FILE_END", 8) == 0)
                {
                    break; // End of file data
                }

                write(fd, buffer, bytes_received);
            }

            close(fd);
            printf("Received file: %s\n", file_path);
            char new_buffer[BUFFER_SIZE];
            snprintf(new_buffer, sizeof(new_buffer), "File Recieved");
            send(sock, new_buffer, strlen(new_buffer), 0);
        }
        else if (strncmp(buffer, "TRANSFER_COMPLETE", 17) == 0)
        {
            // Transfer is complete, break the loop
            printf("File transfer complete.\n");
            // printf("File data received and written successfully.\n");
            const char *success_message = "Folder data received and written successfully.";
            send(sock, success_message, strlen(success_message), 0);
            break;
        }
    }
}
#define ASYNC_WRITE_THRESHOLD 50
struct async_write_data
{
    char *file_path;
    char *data;
    char *ip;
    char *ss_ip;
    int ss_port;
    int port;
};
void *async_write(void *args)
{
    struct async_write_data *write_data = args;
    int nm_sock;
    struct sockaddr_in ss;
    nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0)
    {
        perror("Error in creating the socket");
        exit(0);
    }
    int opt = 1;
    if (setsockopt(nm_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        close(nm_sock);
        exit(0);
    }
    ss.sin_port = htons(write_data->port);
    ss.sin_family = AF_INET;

    if (inet_pton(AF_INET, write_data->ip, &ss.sin_addr) <= 0)
    {
        printf("Invalid address.Address is not valid\n");
        exit(0);
    }

    if (connect(nm_sock, (struct sockaddr *)&ss, sizeof(ss)) < 0)
    {
        perror("Error in connecting to the naming server");
        exit(0);
    }
    char async_buffer[BUFFER_SIZE];
    snprintf(async_buffer, sizeof(async_buffer), "Asynchrnous Write Started");
    send(nm_sock, async_buffer, strlen(async_buffer), 0);
    close(nm_sock);

    FILE *file = fopen(write_data->file_path, "a");
    if (file == NULL)
    {
        perror("Error opening file in async write thread");
        free(write_data->data);
        free(write_data->file_path);
        free(write_data);
        return NULL;
    }

    fwrite(write_data->data, 1, strlen(write_data->data), file);
    fclose(file);
    sleep(20);
    printf("Asynchronous write completed.\n");
    nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_sock < 0)
    {
        perror("Error in creating the socket for reconnection");
        exit(0);
    }

    if (connect(nm_sock, (struct sockaddr *)&ss, sizeof(ss)) < 0)
    {
        perror("Error in reconnecting to the naming server");
        close(nm_sock);
        exit(0);
    }
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "Asynchronous write completed: %s %d %s", write_data->ss_ip, write_data->ss_port, write_data->file_path);
    send(nm_sock, buffer, strlen(buffer), 0);
    printf("buffer_sent:%s\n", buffer);
    close(nm_sock);
    free(write_data->data);
    free(write_data->file_path);
    free(write_data->ss_ip);
    free(write_data);
    return NULL;
}

void *client_handling(void *arg)
{
    struct client_handling_args *args = (struct client_handling_args *)arg;
    int *client_socket = args->connection_client;
    int nm_port = args->nm_port;
    char *ip_ss = args->ip_ss;
    int client_port = args->port_client;
    int client_sock = *client_socket;
    free(arg);
    char buffer[BUFFER_SIZE];
    char data_port[100];
    int data_read;
    data_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (data_read <= 0)
    {
        perror("Error in receving the file request from the client");
        close(client_sock);
        return NULL;
    }
    buffer[data_read] = '\0';
    char store[BUFFER_SIZE];
    strcpy(store, buffer);
    char *command = strtok(buffer, " ");
    printf("%s\n", command);
    char actual_file_path[BUFFER_SIZE];
    strcpy(actual_file_path, store + strlen(command) + 1);
    if (command == NULL || actual_file_path == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_PATH_NOT_FOUND);
        send(client_sock, buffer, strlen(buffer), 0);
        close(client_sock);
        return NULL;
    }
    if (strcmp(command, "READ") == 0)
    {
        printf("Client requested filepath for reading: %s\n", actual_file_path);
        int index = get_hash(actual_file_path, MAX_PATHS);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        if (check_sem(&FileLock[index].read_sem, client_sock) == -1)
            return NULL;
        if (checkFileOrFolder(actual_file_path) == 1)
        {
            FILE *file = fopen(actual_file_path, "r");
            if (file == NULL)
            {
                perror("File not found or file cannot be opened");
                snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_FILE_NOT_FOUND);
                send(client_sock, buffer, strlen(buffer), 0);
                close(client_sock);
                return NULL;
            }
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);
            if (file_size == 0)
            {
                snprintf(buffer, sizeof(buffer), "The file is empty.");
                send(client_sock, buffer, strlen(buffer), 0);
                fclose(file);
                sem_post(&FileLock[index].read_sem);
                return NULL;
            }
            while ((data_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            {
                if (send(client_sock, buffer, data_read, 0) < 0)
                {
                    perror("Error in sending the file.");
                    break;
                }
            }
            fclose(file);
            printf("File sent sucessfully :)\n");
            sem_post(&FileLock[index].read_sem);
            sem_post(&FileLock[index].write_sem);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_NOT_A_FILE);
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
        }
    }
    else if (strcmp(command, "WRITE1") == 0)
    {
        char *hhd = strtok(actual_file_path, " ");
        char *dhd = strtok(NULL, " ");
        char *path = strtok(NULL, " ");
        int index = get_hash(path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        if (checkFileOrFolder(path) == 1)
        {
            FILE *file = fopen(path, "a");
            if (file == NULL)
            {
                perror("Error in opening the file for writing");
                snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_FILE_NOT_FOUND);
                send(client_sock, buffer, strlen(buffer), 0);
                close(client_sock);
                return NULL;
            }
            snprintf(buffer, sizeof(buffer), "Ready to receive the data.\n");
            send(client_sock, buffer, strlen(buffer), 0);
            size_t total_data_size = 0;
            size_t buffer_size = 1024;
            char *data_buffer = malloc(buffer_size);
            if (data_buffer == NULL)
            {
                perror("Error allocating initial memory for data buffer");
                fclose(file);
                close(client_sock);
                return NULL;
            }
            data_buffer[0] = '\0';

            while ((data_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0)) > 0)
            {
                buffer[data_read] = '\0';
                if (strcmp(buffer, "STOP\n") == 0)
                {
                    break;
                }
                if (total_data_size + data_read + 1 > buffer_size)
                {
                    buffer_size = total_data_size + data_read + 1;
                    char *new_buffer = realloc(data_buffer, buffer_size);
                    if (new_buffer == NULL)
                    {
                        perror("Error reallocating memory for data buffer");
                        free(data_buffer);
                        fclose(file);
                        close(client_sock);
                        return NULL;
                    }
                    data_buffer = new_buffer;
                }
                // Append the new data to the buffer
                strcat(data_buffer, buffer);
                total_data_size += data_read;
            }
            if (total_data_size > ASYNC_WRITE_THRESHOLD)
            {
                snprintf(buffer, sizeof(buffer), "Write request accepted. Data will be written asynchronously.\n");
                send(client_sock, buffer, strlen(buffer), 0);

                struct async_write_data *write_data = malloc(sizeof(struct async_write_data));
                if (write_data == NULL)
                {
                    perror("Error allocating memory for async_write_data");
                    free(data_buffer);
                    fclose(file);
                    close(client_sock);
                    return NULL;
                }
                write_data->file_path = strdup(path);
                write_data->data = data_buffer;
                write_data->ip = strdup(ip_ss);
                write_data->port = nm_port;
                write_data->ss_ip = strdup(ip_ss);
                write_data->ss_port = client_port;
                pthread_t write_thread;
                pthread_create(&write_thread, NULL, async_write, write_data);
                pthread_detach(write_thread);
            }
            else
            {
                fwrite(data_buffer, 1, total_data_size, file);
                fclose(file);
                printf("Data requested by client is written to the file successfully :)\n");
                snprintf(buffer, sizeof(buffer), "Data written successfully :)\n");
                send(client_sock, buffer, strlen(buffer), 0);
                free(data_buffer);
            }
            sem_post(&FileLock[index].write_sem);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_NOT_A_FILE);
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
        }
    }
    else if (strcmp(command, "WRITE2") == 0)
    {
        char *hhd = strtok(actual_file_path, " ");
        char *dhd = strtok(NULL, " ");
        char *path = strtok(NULL, " ");
        int index = get_hash(path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        // sem_wait(&FileLock[index].write_sem);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        if (checkFileOrFolder(path) == 1)
        {
            FILE *file = fopen(path, "a");
            if (file == NULL)
            {
                perror("Error in opening the file for writing");
                snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_FILE_NOT_FOUND);
                send(client_sock, buffer, strlen(buffer), 0);
                close(client_sock);
                return NULL;
            }

            snprintf(buffer, sizeof(buffer), "Ready to receive the data.\n");
            send(client_sock, buffer, strlen(buffer), 0);

            while ((data_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0)) > 0)
            {
                buffer[data_read] = '\0';
                if (strcmp(buffer, "STOP\n") == 0)
                {
                    break;
                }
                fwrite(buffer, 1, data_read, file);
            }
            fclose(file);
            printf("Data requeested by client is written to the file sucessfully :)\n");

            snprintf(buffer, sizeof(buffer), "Data written successfully :)\n");
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_NOT_A_FILE);
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
        }
    }
    else if (strcmp(command, "INFO") == 0)
    {
        struct stat file;
        if (stat(actual_file_path, &file) != 0)
        {
            perror("Error in retriving the information of the file");
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_FILE_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
            close(client_sock);
            return NULL;
        }
        snprintf(buffer, sizeof(buffer), "Size: %lld bytes\nPermissions: %o\n", (long long)file.st_size, file.st_mode & 0777);
        send(client_sock, buffer, strlen(buffer), 0);

        printf("File Information has sent to client successfully :)\n");
    }
    else if (strcmp(command, "STREAM") == 0)
    {
        if (send_audio(actual_file_path, client_sock))
            printf("Audio file sent to client successfully:)\n");
        else
            printf("Audio file couldn't be sent :(\n");
    }
    close(client_sock);
    return NULL;
}
void *client_handler(void *arg)
{
    struct client_handler_args *args = (struct client_handler_args *)arg;
    char *ip_ss = args->ip_ss;
    int port_client_init = args->port_client;
    int nm_port = args->nm_port;
    int client_socket;
    struct sockaddr_in client_addr;
    present_client_port = port_client_init;

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0)
    {
        perror("Error in creating the socket for client.");
        exit(0);
    }

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port_client_init);
    client_addr.sin_addr.s_addr = inet_addr(ip_ss);

    int opt = 1;
    if (setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        close(client_socket);
        exit(0);
    }

    if (bind(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Error in binding in socket");
        close(client_socket);
        exit(0);
    }

    if (listen(client_socket, 5) < 0)
    {
        perror("Error in listening the client.");
        close(client_socket);
        exit(0);
    }

    printf("Storage server is ready for clients on port %d\n", port_client_init);
    socklen_t client_addr_len = sizeof(client_addr);

    while (1)
    {
        int *connection_client = malloc(sizeof(int));
        if (connection_client == NULL)
        {
            perror("Error in allocating the memory");
            continue;
        }

        *connection_client = accept(client_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*connection_client < 0)
        {
            perror("Error in connecting to the client");
            free(connection_client);
            continue;
        }

        printf("Accepted client connection using PORT: %d\n", port_client_init);

        // Create a new structure to pass to the thread
        struct client_handling_args *client_thread_args = malloc(sizeof(struct client_handling_args));
        if (client_thread_args == NULL)
        {
            perror("Error in allocating memory for client thread args");
            close(*connection_client);
            free(connection_client);
            continue;
        }

        client_thread_args->connection_client = connection_client;
        client_thread_args->nm_port = nm_port;
        client_thread_args->ip_ss = ip_ss;
        client_thread_args->port_client = port_client_init;

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_handling, client_thread_args) != 0)
        {
            perror("Error in creating the thread");
            close(*connection_client);
            free(connection_client);
            free(client_thread_args); // Don't forget to free allocated memory
        }
        else
        {
            pthread_detach(client_thread);
        }
    }
    close(client_socket);
}
void *nm_handling(void *arg)
{
    int client_sock = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];
    char data_port[100];

    int data_read;
    data_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (data_read <= 0)
    {
        perror("Error in receving the file request from the client");
        close(client_sock);
        return NULL;
    }

    buffer[data_read] = '\0';
    char store[BUFFER_SIZE];
    strcpy(store, buffer);

    char *command = strtok(buffer, " ");
    printf("%s\n", command);
    char actual_file_path[BUFFER_SIZE];
    strcpy(actual_file_path, store + strlen(command) + 1);
    if (command == NULL || actual_file_path == NULL)
    {
        snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_PATH_NOT_FOUND);
        send(client_sock, buffer, strlen(buffer), 0);
        close(client_sock);
        return NULL;
    }
    if (strcmp(command, "CREATE") == 0)
    {
        char *fd_str = strtok(actual_file_path, " ");
        char *path = strtok(NULL, " ");
        char *filename = strtok(NULL, " ");
        int fd = atoi(fd_str);
        if (path && filename)
        {
            char full_path[BUFFER_SIZE];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);
            if (fd == 1)
            {
                int file_fd = open(full_path, O_CREAT | O_WRONLY, 0644);
                if (file_fd == -1)
                {
                    perror("Error creating file");
                    snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_PATH_NOT_FOUND);
                    send(client_sock, buffer, strlen(buffer), 0);
                    return NULL;
                }
                else
                {
                    printf("File created successfully at: %s\n", full_path);
                    close(file_fd);
                    addpath_ht(&paths_accessible, full_path);
                    snprintf(buffer, sizeof(buffer), "File Created Successfully.\n");
                    send(client_sock, buffer, strlen(buffer), 0);
                }
            }
            else
            {
                if (mkdir(full_path, 0755) == -1)
                {
                    perror("Error creating directory");
                    snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_PATH_NOT_FOUND);
                    send(client_sock, buffer, strlen(buffer), 0);
                    return NULL;
                }
                {
                    printf("Folder created successfully at: %s\n", full_path);
                    addpath_ht(&paths_accessible, full_path);
                    snprintf(buffer, sizeof(buffer), "Folder Created Successfully.\n");
                    send(client_sock, buffer, strlen(buffer), 0);
                }
            }
        }
    }
    else if (strcmp(command, "DELETEFILE") == 0)
    {
        int index = get_hash(actual_file_path, MAX_PATHS);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        printf("NM requested filepath for deleting: %s\n", actual_file_path);
        if (remove(actual_file_path) == 0)
        {
            printf("File deleted successfully\n");
            snprintf(buffer, sizeof(buffer), "File deleted successfully.\n");
            send(client_sock, buffer, strlen(buffer), 0);
        }
        else
        {
            perror("Error deleting file");
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_FILE_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
            return NULL;
        }
        sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "DELETEFOLDER") == 0)
    {
        int index = get_hash(actual_file_path, MAX_PATHS);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        printf("NM requested filepath for deleting: %s\n", actual_file_path);
        // if (rmdir(actual_file_path) == 0)
        // {
        //     printf("Folder deleted successfully\n");
        //     snprintf(buffer, sizeof(buffer), "Folder deleted successfully.\n");
        //     send(client_sock, buffer, strlen(buffer), 0);
        // }
        // else
        // {
        //     perror("Error deleting folder");
        //     snprintf(buffer, sizeof(buffer), "Error:Directory Not Deleted.\n");
        //     send(client_sock, buffer, strlen(buffer), 0);
        // }
        if (delete_directory_recursively(actual_file_path) == 0)
        {
            printf("Folder Deleted Successfully\n");
            snprintf(buffer, sizeof(buffer), "Folder Deleted Successfully\n");
            send(client_sock, buffer, strlen(buffer), 0);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_DIR_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
            return NULL;
        }
        sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "COPY1") == 0)
    {
        char *bhh = strtok(actual_file_path, " ");
        char *shhds = strtok(NULL, " ");
        char *src_path = strtok(NULL, " ");
        char *dest_path = strtok(NULL, " ");
        printf("src_path:%s\n", src_path);
        printf("dest_path:%s\n", dest_path);
        int index = get_hash(src_path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        struct stat src_stat, dest_stat;
        if (stat(src_path, &src_stat) != 0)
        {
            perror("Error accessing source path");
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_SRC_FILE_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
            return NULL;
        }

        if (stat(dest_path, &dest_stat) == 0 && S_ISDIR(dest_stat.st_mode))
        {
            if (S_ISREG(src_stat.st_mode))
            {
                char dest_file_path[BUFFER_SIZE];
                snprintf(dest_file_path, sizeof(dest_file_path), "%s/%s", dest_path, strrchr(src_path, '/') + 1);
                copy_file(src_path, dest_file_path);
                printf("File copied successfully.\n");
            }
            else if (S_ISDIR(src_stat.st_mode))
            {
                char dest_dir_path[BUFFER_SIZE];
                snprintf(dest_dir_path, sizeof(dest_dir_path), "%s/%s", dest_path, strrchr(src_path, '/') + 1);
                copy_directory(src_path, dest_dir_path);
                printf("Directory copied successfully.\n");
            }
        }
        printf("Data Copied Successfully\n");
        snprintf(buffer, sizeof(buffer), "Data Copied successfully.\n");
        send(client_sock, buffer, strlen(buffer), 0);
        sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "COPY2") == 0)
    {
        char *bhh = strtok(actual_file_path, " ");
        char *hfhh = strtok(NULL, " ");
        char *src_path = strtok(NULL, " ");
        char *dest_path = strtok(NULL, " ");
        printf("src_path:%s\n", src_path);
        printf("dest_path:%s\n", dest_path);
        int index = get_hash(src_path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Data from Src File\n");
        send(client_sock, buffer, strlen(buffer), 0);
        int file_fd = open(src_path, O_RDONLY);
        if (file_fd < 0)
        {
            perror("File open error");
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_SRC_FILE_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
            return NULL;
        }
        char new_buffer[1024];
        ssize_t bytes_read;
        while ((bytes_read = read(file_fd, new_buffer, sizeof(new_buffer))) > 0)
        {
            send(client_sock, new_buffer, bytes_read, 0);
        }
        close(file_fd);
        char *end_message = "FILE_TRANSFER_END";
        send(client_sock, end_message, strlen(end_message), 0);
        sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "DEST") == 0)
    {
        // printf("HUhhhh:)\n");
        // printf("%s\n",actual_file_path);
        char *bhh = strtok(actual_file_path, " ");
        char *hfhh = strtok(NULL, " ");
        char *xbshs = strtok(NULL, " ");
        char *src_path = strtok(NULL, " ");
        char *dest_path = strtok(NULL, " ");
        printf("src_path:%s\n", src_path);
        printf("dest_path:%s\n", dest_path);
        int index = get_hash(dest_path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        char *src_name = strrchr(src_path, '/');
        if (src_name)
            src_name++;
        else
            src_name = src_path;
        char new_path[BUFFER_SIZE];
        snprintf(new_path, sizeof(new_path), "%s/%s", dest_path, src_name);
        int file_fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd < 0)
        {
            perror("Error creating file");
            snprintf(buffer, sizeof(buffer), "Error Code %d", ERR_DEST_DIR_NOT_FOUND);
            send(client_sock, buffer, strlen(buffer), 0);
             sem_post(&FileLock[index].write_sem);
            return NULL;
        }
        printf("File created successfully at: %s\n", new_path);
        snprintf(buffer, sizeof(buffer), "Ready to Receive data");
        send(client_sock, buffer, strlen(buffer), 0);
        receive_and_write_file(client_sock, file_fd);
         sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "COPY3") == 0)
    {
        char *bhh = strtok(actual_file_path, " ");
        char *hfhh = strtok(NULL, " ");
        char *src_path = strtok(NULL, " ");
        char *dest_path = strtok(NULL, " ");
        printf("src_path:%s\n", src_path);
        printf("dest_path:%s\n", dest_path);
        char buffer[BUFFER_SIZE];
        int index = get_hash(src_path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        snprintf(buffer, sizeof(buffer), "Data from Src Folder\n");
        send(client_sock, buffer, strlen(buffer), 0);
        struct stat path_stat;
        if (stat(src_path, &path_stat) < 0)
        {
            perror("Error getting path info");
            snprintf(buffer, sizeof(buffer), "Error:Error getting folder info.\n");
            send(client_sock, buffer, strlen(buffer), 0);
            sem_post(&FileLock[index].write_sem);
            exit(1);
        }
        send_directory(client_sock, src_path);
        sem_post(&FileLock[index].write_sem);
    }
    else if (strcmp(command, "DEST1") == 0)
    {
        char *bhh = strtok(actual_file_path, " ");
        char *ndhdjj = strtok(NULL, " ");
        char *hfhh = strtok(NULL, " ");
        // char* fff=strtok(NULL," ");
        char *src_path = strtok(NULL, " ");
        char *dest_path = strtok(NULL, " ");
        int index = get_hash(dest_path, MAX_PATHS);
        printf("%d\n", index);
        int value;
        sem_getvalue(&FileLock[index].write_sem, &value);
        printf("%d\n", value);
        if (check_sem(&FileLock[index].write_sem, client_sock) == -1)
            return NULL;
        printf("src_path:%s\n", src_path);
        printf("dest_path:%s\n", dest_path);
        snprintf(buffer, sizeof(buffer), "Ready to Receive data");
        send(client_sock, buffer, strlen(buffer), 0);
        receive_file_system(client_sock, dest_path);
        sem_post(&FileLock[index].write_sem);
    }
    close(client_sock);
    return NULL;
}
void *nm_handler(void *arg)
{
    struct nm_handler_args *args = (struct nm_handler_args *)arg;
    char *ip_ss = args->ip_ss;
    int port_nm_init = args->port_nm;
    int nm_socket;
    struct sockaddr_in nm_addr;
    int present_nm_port = port_nm_init;
    printf("ip:%s\n", ip_ss);
    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket < 0)
    {
        perror("Error in creating the socket for nm.");
        exit(0);
    }
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(port_nm_init);
    nm_addr.sin_addr.s_addr = inet_addr(ip_ss);
    int opt = 1;
    if (setsockopt(nm_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        printf("nmp:%d\n", port_nm_init);
        close(nm_socket);
        exit(0);
    }
    if (bind(nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0)
    {
        perror("Error in binding in socket");
        close(nm_socket);
        exit(0);
    }
    if (listen(nm_socket, 5) < 0)
    {
        perror("Error in listening the client.");
        close(nm_socket);
        exit(0);
    }
    printf("Storage server is ready for nm on port %d\n", port_nm_init);
    socklen_t nm_addr_len = sizeof(nm_addr);
    int current_port = port_nm_init;
    while (1)
    {
        int *connection_nm = malloc(sizeof(int));
        if (connection_nm == NULL)
        {
            perror("Error in allocating the memory");
            continue;
        }
        printf("port:%d\n", port_nm_init);
        *connection_nm = accept(nm_socket, (struct sockaddr *)&nm_addr, &nm_addr_len);
        if (*connection_nm < 0)
        {
            perror("Error in connecting to the client");
            free(connection_nm);
            continue;
        }
        char buffer[1024];
        printf("Accepted nm connection using PORT: %d\n", current_port);
        pthread_t nm_thread;
        if (pthread_create(&nm_thread, NULL, nm_handling, connection_nm) != 0)
        {
            perror("Error in creating the thread");
            close(*connection_nm);
            free(connection_nm);
        }
        else
        {
            pthread_detach(nm_thread);
        }
    }
    close(nm_socket);
}
int main(int argc, char *argv[])
{
    if (argc <= 5)
    {
        printf("Error in command line input.\n");
        exit(0);
    }
    char *ip_nm = argv[1];
    int port_nm = atoi(argv[2]);
    char *ip_ss = argv[3];
    int port_ss = atoi(argv[4]);
    int port_client = atoi(argv[5]);
    init_semaphores();
    initialisation_ht(&paths_accessible);
    registration_with_nm(ip_nm, port_nm, ip_ss, port_ss, port_client);
    struct client_handler_args *client_args = malloc(sizeof(struct client_handler_args));
    if (client_args == NULL)
    {
        perror("Error allocating memory for client handler args");
        exit(1);
    }
    client_args->ip_ss = ip_ss;
    client_args->port_client = port_client;
    client_args->nm_port = port_nm;
    struct nm_handler_args *nm_args = malloc(sizeof(struct nm_handler_args));
    if (nm_args == NULL)
    {
        perror("Error allocating memory for nm handler args");
        exit(1);
    }
    nm_args->ip_ss = ip_ss;
    nm_args->port_nm = port_ss;
    pthread_t client_requests;
    pthread_t nm_requests;
    pthread_t heartbeat_thread;
    if (pthread_create(&heartbeat_thread, NULL, send_heartbeat, &nm_socket) != 0)
    {
        perror("Error creating heartbeat thread");
        close(nm_socket);
        exit(0);
    }
    if (pthread_create(&client_requests, NULL, (void *)&client_handler, client_args) != 0)
    {
        perror("Thread Creation Error For client Requests");
        exit(1);
    }
    if (pthread_create(&nm_requests, NULL, (void *)&nm_handler, nm_args) != 0)
    {
        perror("Thread Creation Error For nm requests");
        exit(1);
    }
    if (pthread_join(client_requests, NULL) != 0)
    {
        perror("pthread join client requests");
        exit(EXIT_FAILURE);
    }
    if (pthread_join(nm_requests, NULL) != 0)
    {
        perror("pthread join nm requests");
        exit(EXIT_FAILURE);
    }
    printf("Naming Server Disconnected\n");
    pthread_join(heartbeat_thread, NULL);
    return 0;
}
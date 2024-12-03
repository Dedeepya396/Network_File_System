#include "naming_server.h"

int get_server_index(char path[])
{
    for (int i = 0; i < server_count; i++)
    {
        printf("Server_count: %d\n", storageServers[i].count_paths);
        for (int j = 0; j < storageServers[i].count_paths; j++)
        {
            if (strcmp(path, storageServers[i].Accessible_Paths[j]) == 0)
            {
                return i;
            }
        }
    }
    return 0;
}

int get_server_index_by_ip(char *ip_here, int port_here)
{
    for (int i = 0; i < server_count; i++)
    {
        if (strcmp(storageServers[i].ip, ip_here) == 0 && storageServers[i].client_port == port_here)
        {
            return i;
        }
    }
    return -1;
}
void delete_from_cache(char path[])
{
    for (int i = 0; i < LRU_size; i++)
    {
        if (strcmp(LRU_cache[i].path, path) == 0)
        {
            strcpy(LRU_cache[i].path, "");
            strcpy(LRU_cache[i].ss_ip, "");
            LRU_cache[i].ss_port = 0;
            LRU_size--;
            printf("Deleting from cache as well! \n");
            snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Deleting path from cache as well!\n");

            return;
        }
    }
    printf("Not found in cache to delete!\n");
}
void receive_data(int server_sock, FileSystem *fs)
{
    char buffer[BUFFER_SIZE];
    char current_path[BUFFER_SIZE] = "";
    char *current_data = NULL;
    size_t current_data_size = 0;

    while (1)
    {
        ssize_t bytes_received = recv(server_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            // Connection closed or error
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received data
        char new_buffer[BUFFER_SIZE];
        snprintf(new_buffer, sizeof(new_buffer), "Received");
        send(server_sock, new_buffer, sizeof(new_buffer), 0);
        if (strncmp(buffer, "FILE_START:", 11) == 0)
        {
            // A new file is being sent
            if (current_data)
            {
                // Save the previous file data
                FileData *file = &fs->files[fs->count++];
                strncpy(file->path, current_path, BUFFER_SIZE);
                file->data = current_data;
                file->size = current_data_size;

                current_data = NULL;
                current_data_size = 0;
            }

            // Extract file path
            sscanf(buffer + 11, "%s", current_path);
        }
        else if (strcmp(buffer, "FILE_TRANSFER_END\n") == 0)
        {
            // File transfer completed
            if (current_data)
            {
                FileData *file = &fs->files[fs->count++];
                strncpy(file->path, current_path, BUFFER_SIZE);
                file->data = current_data;
                file->size = current_data_size;

                current_data = NULL;
                current_data_size = 0;
            }
        }
        else if (strncmp(buffer, "DIR_START:", 10) == 0)
        {
            // A new directory is being sent
            char dir_path[BUFFER_SIZE];
            sscanf(buffer + 10, "%s", dir_path);

            // Add directory to the file system
            FileData *dir = &fs->files[fs->count++];
            strncpy(dir->path, dir_path, BUFFER_SIZE);
            dir->data = NULL; // Directories have no data
            dir->size = 0;
        }
        else if (strcmp(buffer, "DIR_TRANSFER_END\n") == 0)
        {
            // Directory transfer completed
            // No additional handling needed
        }
        else
        {
            // Normal file data
            current_data = realloc(current_data, current_data_size + bytes_received);
            memcpy(current_data + current_data_size, buffer, bytes_received);
            current_data_size += bytes_received;
        }
    }
    if (current_data)
    {
        FileData *file = &fs->files[fs->count++];
        strncpy(file->path, current_path, BUFFER_SIZE);
        file->data = current_data;
        file->size = current_data_size;
    }
}
void send_file_system(int sock, FileSystem *fs, const char *src_path, const char *dest_path)
{
    char buffer[BUFFER_SIZE];
    size_t src_path_len = strlen(src_path);
    const char *src_base_name = strrchr(src_path, '/');
    if (src_base_name)
        src_base_name++;
    else
        src_base_name = src_path;
    printf("%d\n", fs->count);
    for (int i = 0; i < fs->count; i++)
    {
        printf("i:%d\n", i);
        FileData *entry = &fs->files[i];
        const char *relative_path = entry->path + src_path_len;
        if (*relative_path == '/')
            relative_path++;
        char dest_full_path[4096];
        if (relative_path[0] != '\0')
        {
            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s/%s", dest_path, src_base_name, relative_path);
        }
        else
        {
            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s", dest_path, src_base_name);
        }
        printf("%s\n", dest_full_path);

        if (entry->data == NULL)
        {
            char ack[BUFFER_SIZE];
            snprintf(buffer, sizeof(buffer), "DIR:%s", dest_full_path);
            printf("DIR: %s\n", dest_full_path);
            send(sock, buffer, strlen(buffer), 0);
            int data_read = recv(sock, ack, sizeof(ack), 0);
        }
    }
    for (int i = 0; i < fs->count; i++)
    {
        FileData *entry = &fs->files[i];
        const char *relative_path = entry->path + src_path_len;
        if (*relative_path == '/')
            relative_path++;
        char dest_full_path[4096];
        snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s/%s", dest_path, src_base_name, relative_path);

        if (entry->data != NULL)
        {
            snprintf(buffer, sizeof(buffer), "FILE:%s", dest_full_path);
            char ack[BUFFER_SIZE];

            printf("%s\n", buffer);
            send(sock, buffer, strlen(buffer), 0);
            printf("sent\n");
            int data_read = recv(sock, ack, sizeof(ack), 0);
            size_t sent = 0;
            while (sent < entry->size)
            {
                size_t chunk_size = (entry->size - sent) > BUFFER_SIZE ? BUFFER_SIZE : (entry->size - sent);
                memcpy(buffer, entry->data + sent, chunk_size);
                send(sock, buffer, chunk_size, 0);
                sent += chunk_size;
            }
            snprintf(buffer, sizeof(buffer), "FILE_END");
            send(sock, buffer, strlen(buffer), 0);
            int data_x = recv(sock, ack, sizeof(ack), 0);
            printf("Receie%s\n", ack);
        }
    }
    snprintf(buffer, sizeof(buffer), "TRANSFER_COMPLETE\n");
    send(sock, buffer, strlen(buffer), 0);
}
void print_file_system(const FileSystem *fs)
{
    printf("FileSystem contains %d file(s):\n", fs->count);

    for (int i = 0; i < fs->count; i++)
    {
        const FileData *file = &fs->files[i];
        printf("File %d:\n", i + 1);
        printf("  Path: %s\n", file->path);
        printf("  Size: %zu bytes\n", file->size);

        // Print file data, assuming it's a string. Adjust if it's binary data.
        if (file->data != NULL)
        {
            printf("  Data: %.*s\n", (int)file->size, file->data); // Limit printing to file size
        }
        else
        {
            printf("  Data: (null)\n");
        }
    }
}
void send_to_SS(const char *ss_ip, int port, char request[], int client_socket)
{
    char response[BUFFER_SIZE + 5];
    printf("came\n");
    int sock;
    struct sockaddr_in server_addr;
    int bytes_received;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    printf("%d\n", sock);
    if (sock < 0)
    {
        perror("Socket creation failed\n");
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ss_ip);
    server_addr.sin_port = htons(port - 1);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to the SS failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to SS at %s:%d\n", ss_ip, port);
    if (send(sock, request, strlen(request), 0) < 0)
    {
        perror("Failed to send message to SS");
    }
    else
    {
        printf("Request sent to SS: %s\n\n", request);
        snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Request sent to SS\n");
    }
    char buffer[BUFFER_SIZE];
    if (strncmp(request, "STREAM", 6) != 0)
    {
        int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (data_read > 0)
        {
            buffer[data_read] = '\0';
            printf("FROM SS: %s\n", buffer);
            snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: From SS %s\n", buffer);
            if (strcmp(buffer, "Wait for some time :(") == 0)
            {
                send(client_socket, buffer, strlen(buffer), 0);
            }
            else if (strncmp(buffer, "Error", 5) == 0)
            {
                send(client_socket, buffer, strlen(buffer), 0);
            }
            else
            {
                printf("1\n");
                snprintf(response, BUFFER_SIZE + 5, "ACK %s", buffer);
                printf("ackkkk%s\n", response);
                if (strncmp(buffer, "File Created Successfully.", 26) == 0)
                {

                    char *command = strtok(request, " ");
                    char *fd_str = strtok(NULL, " ");
                    char *path = strtok(NULL, " ");
                    char *filename = strtok(NULL, " ");
                    int fd = atoi(fd_str);
                    char full_path[BUFFER_SIZE];
                    snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(path);
                    printf("%d\n", index);
                    insertPathToTrie(full_path, index, storageServers[index].count_paths);
                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], full_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("Added path to SS: %s\n", full_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Added path to SS %s\n", full_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "Folder Created Successfully.", 28) == 0)
                {
                    char *command = strtok(request, " ");
                    char *fd_str = strtok(NULL, " ");
                    char *path = strtok(NULL, " ");
                    char *filename = strtok(NULL, " ");
                    int fd = atoi(fd_str);
                    char full_path[BUFFER_SIZE];
                    snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(path);
                    printf("%d\n", index);
                    insertPathToTrie(full_path, index, storageServers[index].count_paths);
                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], full_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("Added path to SS: %s\n", full_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Added path to SS %s\n", full_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "File deleted successfully.", 26) == 0)
                {
                    char *command = strtok(request, " ");
                    char *path = strtok(NULL, " ");
                    for (int i = 0; i < server_count; i++)
                    {
                        for (int j = 0; j < storageServers[i].count_paths; j++)
                        {
                            if (strcmp(storageServers[i].Accessible_Paths[j], path) == 0)
                            {
                                for (int k = j; k < storageServers[i].count_paths - 1; k++)
                                {
                                    strcpy(storageServers[i].Accessible_Paths[k], storageServers[i].Accessible_Paths[k + 1]);
                                }
                                strcpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths - 1], "");
                                if (removePath(root, path, 0))
                                    printf("Path %s removed from trie successfully.\n", path);
                                else
                                    printf("Path '%s' not found or couldn't be removed.\n", path);
                                delete_from_cache(path);
                                storageServers[i].count_paths--;
                                printf("Path '%s' removed from storage server %s:%d\n", path, storageServers[i].ip, storageServers[i].client_port);
                                break;
                            }
                        }
                    }
                }
                if (strncmp(buffer, "Folder Deleted Successfully", 27) == 0)
                {
                    char *command = strtok(request, " ");
                    char *path = strtok(NULL, " ");
                    for (int i = 0; i < server_count; i++)
                    {
                        int j = 0;
                        while (j < storageServers[i].count_paths)
                        {
                            if (strncmp(storageServers[i].Accessible_Paths[j], path, strlen(path)) == 0)
                            {
                                (removePath(root, storageServers[i].Accessible_Paths[j], 0));
                                delete_from_cache(storageServers[i].Accessible_Paths[j]);
                                printf("Removing Path: %s\n", storageServers[i].Accessible_Paths[j]);
                                for (int k = j; k < storageServers[i].count_paths - 1; k++)
                                {
                                    strcpy(storageServers[i].Accessible_Paths[k], storageServers[i].Accessible_Paths[k + 1]);
                                }
                                strcpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths - 1], "");
                                storageServers[i].count_paths--;
                                continue;
                            }
                            j++;
                        }
                    }
                }
                if (strncmp(buffer, "Data Copied successfully.", 25) == 0)
                {
                    char *command = strtok(request, " ");
                    char *copy_type = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");

                    char *src_name = strrchr(src_path, '/');
                    if (src_name)
                        src_name++;
                    else
                        src_name = src_path;

                    char new_path[BUFFER_SIZE];
                    snprintf(new_path, sizeof(new_path), "%s/%s", dest_path, src_name);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(dest_path);
                    printf("%d\n", index);
                    insertPathToTrie(new_path, index, storageServers[index].count_paths);

                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], new_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("New path added to SS: %s\n", new_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: New path added to SS %s \n", new_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "Data from Src File", 18) == 0)
                {
                    printf("1\n");
                    char store[BUFFER_SIZE];
                    strcpy(store, request);
                    char *bhh = strtok(request, " ");
                    char *hhh = strtok(NULL, " ");
                    char *ndhbhd = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");
                    char *dest_ip = strtok(NULL, " ");
                    int dest_port = atoi(strtok(NULL, " "));
                    char new_request[BUFFER_SIZE + 6];
                    snprintf(new_request, sizeof(new_request), "DEST %s", store);
                    char new_buffer[BUFFER_SIZE];
                    char *file_data = malloc(1);
                    size_t file_data_size = 0;
                    if (file_data == NULL)
                    {
                        perror("malloc");
                        return;
                    }
                    file_data[0] = '\0';
                    ssize_t bytes_received;
                    while ((bytes_received = recv(sock, new_buffer, sizeof(new_buffer) - 1, 0)) > 0)
                    {
                        printf("2\n");
                        new_buffer[bytes_received] = '\0';
                        printf("%s\n", new_buffer);
                        if (strstr(new_buffer, "FILE_TRANSFER_END") != NULL)
                        {
                            char *end_pos = strstr(new_buffer, "FILE_TRANSFER_END");
                            *end_pos = '\0';
                            file_data_size += strlen(new_buffer);
                            file_data = realloc(file_data, file_data_size + 1);
                            if (file_data == NULL)
                            {
                                perror("realloc");
                                free(file_data);
                                return;
                            }
                            strcat(file_data, new_buffer);
                            break;
                        }
                        file_data_size += bytes_received;
                        file_data = realloc(file_data, file_data_size + 1);
                        if (file_data == NULL)
                        {
                            perror("realloc");
                            free(file_data);
                            return;
                        }
                        strncat(file_data, new_buffer, bytes_received);
                    }
                    // if (file_data != NULL)
                    // {
                    //     printf("File data received:\n%s\n", file_data);
                    // }
                    /*--------------------------------------------------------------NEED TO ADD TO LOG-------------------------*/
                    int sock;
                    struct sockaddr_in server_addr;
                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    printf("%d\n", sock);
                    if (sock < 0)
                    {
                        perror("Socket creation failed\n");
                        exit(EXIT_FAILURE);
                    }
                    memset(&server_addr, 0, sizeof(server_addr));
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_addr.s_addr = inet_addr(dest_ip);
                    server_addr.sin_port = htons(dest_port - 1);
                    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                    {
                        perror("Connection to the SS failed");
                        close(sock);
                        exit(EXIT_FAILURE);
                    }
                    printf("Connected to SS at %s:%d\n", dest_ip, dest_port - 1);
                    if (send(sock, new_request, strlen(new_request), 0) < 0)
                    {
                        perror("Failed to send message to SS");
                    }
                    else
                    {
                        printf("Request sent to SS: %s\n\n", new_request);
                    }
                    int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (strncmp(buffer, "Ready to Receive data", 21) == 0)
                    {
                        size_t total_bytes_sent = 0;
                        size_t file_data_len = file_data_size; // Ensure this is the correct length (in bytes) of the file data
                        ssize_t bytes_sent;

                        // Send `file_data` in chunks until all data is sent
                        while (total_bytes_sent < file_data_len)
                        {
                            bytes_sent = send(sock, file_data + total_bytes_sent, file_data_len - total_bytes_sent, 0);

                            if (bytes_sent < 0)
                            {
                                perror("Send error");
                                break; // Exit on error
                            }

                            total_bytes_sent += bytes_sent;
                        }

                        if (total_bytes_sent == file_data_len)
                        {
                            printf("File data sent successfully.\n");
                        }
                        else
                        {
                            printf("Data sending incomplete.\n");
                        }
                        free(file_data);
                        const char *end_message = "FILE_TRANSFER_END";
                        send(sock, end_message, strlen(end_message), 0);

                        int ddd = recv(sock, buffer, sizeof(buffer) - 1, 0);
                        snprintf(response, BUFFER_SIZE + 5, "ACK %s", buffer);
                        char *src_name = strrchr(src_path, '/');
                        if (src_name)
                            src_name++;
                        else
                            src_name = src_path;

                        char new_path[BUFFER_SIZE];
                        snprintf(new_path, sizeof(new_path), "%s/%s", dest_path, src_name);
                        if (!root)
                            root = createNode();
                        int index = get_server_index(dest_path);
                        printf("%d\n", index);
                        insertPathToTrie(new_path, index, storageServers[index].count_paths);
                        for (int i = 0; i < server_count; i++)
                        {
                            if (strcmp(dest_ip, storageServers[i].ip) == 0 && dest_port == storageServers[i].client_port)
                            {
                                if (storageServers[i].count_paths < MAX_PATHS)
                                {
                                    strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], new_path, MAX_LEN);
                                    storageServers[i].count_paths++;
                                    printf("New path added to SS: %s\n", new_path);
                                }
                                else
                                {
                                    printf("Max paths reached for SS\n");
                                }
                                break;
                            }
                        }
                    }
                }
                if (strncmp(buffer, "Data from Src Folder", 20) == 0)
                {
                    char store[BUFFER_SIZE];
                    strcpy(store, request);
                    char *bhh = strtok(request, " ");
                    char *sdjsdhdud = strtok(NULL, " ");
                    char *hhh = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");
                    char *dest_ip = strtok(NULL, " ");
                    int dest_port = atoi(strtok(NULL, " "));
                    char new_request[BUFFER_SIZE + 6];
                    snprintf(new_request, sizeof(new_request), "DEST1 %s", store);
                    FileSystem fs = {.count = 0};
                    receive_data(sock, &fs);
                    print_file_system(&fs);
                    int sock;
                    struct sockaddr_in server_addr;
                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    printf("%d\n", sock);
                    if (sock < 0)
                    {
                        perror("Socket creation failed\n");
                        exit(EXIT_FAILURE);
                    }
                    memset(&server_addr, 0, sizeof(server_addr));
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_addr.s_addr = inet_addr(dest_ip);
                    server_addr.sin_port = htons(dest_port - 1);
                    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                    {
                        perror("Connection to the SS failed");
                        close(sock);
                        exit(EXIT_FAILURE);
                    }
                    printf("Connected to SS at %s:%d\n", dest_ip, dest_port - 1);
                    if (send(sock, new_request, strlen(new_request), 0) < 0)
                    {
                        perror("Failed to send message to SS");
                    }
                    else
                    {
                        printf("Request sent to SS: %s\n\n", new_request);
                    }

                    int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (strncmp(buffer, "Ready to Receive data", 21) == 0)
                    {
                        send_file_system(sock, &fs, src_path, dest_path);
                    }
                    if (!root)
                    {
                        root = createNode(); // Ensure Trie root is initialized
                    }
                    size_t src_path_len = strlen(src_path);
                    const char *src_base_name = strrchr(src_path, '/');
                    if (src_base_name)
                        src_base_name++;
                    else
                        src_base_name = src_path;
                    printf("fssscount:-%d\n", fs.count);
                    for (int i = 0; i < fs.count; i++)
                    {
                        FileData entry = fs.files[i];
                        const char *relative_path = entry.path + src_path_len;
                        if (*relative_path == '/')
                            relative_path++;
                        char dest_full_path[4096];
                        if (relative_path[0] != '\0')
                        {
                            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s/%s", dest_path, src_base_name, relative_path);
                        }
                        else
                        {
                            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s", dest_path, src_base_name);
                        }
                        printf("%s\n", dest_full_path);
                        int index = get_server_index(dest_path); // Map file to storage server index
                        printf("Server index for path %s: %d\n", dest_path, index);
                        insertPathToTrie(dest_full_path, index, storageServers[index].count_paths);
                        for (int j = 0; j < server_count; j++)
                        {
                            if (strcmp(dest_ip, storageServers[j].ip) == 0 && dest_port == storageServers[j].client_port)
                            {
                                if (storageServers[j].count_paths < MAX_PATHS)
                                {
                                    strncpy(storageServers[j].Accessible_Paths[storageServers[j].count_paths], dest_full_path, MAX_LEN);
                                    storageServers[j].count_paths++;
                                    printf("New path added to Storage Server %d: %s\n", j, dest_full_path);
                                }
                                else
                                {
                                    printf("Max paths reached for Storage Server %d\n", j);
                                }
                                break;
                            }
                        }
                    }
                    for (int i = 0; i < fs.count; i++)
                    {
                        free(fs.files[i].data);
                    }
                }
                send(client_socket, response, strlen(response), 0);
            }
        }
        else
        {
            printf("No response from storage server\n");
        }
    }
}
void send_to_SS_dup(const char *ss_ip, int port, char request[], int client_socket)
{
    char response[BUFFER_SIZE + 5];
    int sock;
    struct sockaddr_in server_addr;
    int bytes_received;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    printf("%d\n", sock);
    if (sock < 0)
    {
        perror("Socket creation failed\n");
        exit(EXIT_FAILURE);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ss_ip);
    server_addr.sin_port = htons(port - 1);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to the SS failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Connected to SS at %s:%d\n", ss_ip, port);
    if (send(sock, request, strlen(request), 0) < 0)
    {
        perror("Failed to send message to SS");
    }
    else
    {
        printf("Request sent to SS: %s\n\n", request);
        snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Request sent to SS\n");
    }
    char buffer[BUFFER_SIZE];
    if (strncmp(request, "STREAM", 6) != 0)
    {
        int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (data_read > 0)
        {
            buffer[data_read] = '\0';
            printf("FROM SS: %s\n", buffer);
            snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: From SS %s\n", buffer);
            {
                if (strncmp(buffer, "File Created Successfully.", 26) == 0)
                {

                    char *command = strtok(request, " ");
                    char *fd_str = strtok(NULL, " ");
                    char *path = strtok(NULL, " ");
                    char *filename = strtok(NULL, " ");
                    int fd = atoi(fd_str);
                    char full_path[BUFFER_SIZE];
                    snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(path);
                    printf("%d\n", index);
                    insertPathToTrie(full_path, index, storageServers[index].count_paths);
                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], full_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("Added path to SS: %s\n", full_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Added path to SS %s\n", full_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "Folder Created Successfully.", 28) == 0)
                {
                    char *command = strtok(request, " ");
                    char *fd_str = strtok(NULL, " ");
                    char *path = strtok(NULL, " ");
                    char *filename = strtok(NULL, " ");
                    int fd = atoi(fd_str);
                    char full_path[BUFFER_SIZE];
                    snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(path);
                    printf("%d\n", index);
                    insertPathToTrie(full_path, index, storageServers[index].count_paths);
                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], full_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("Added path to SS: %s\n", full_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Added path to SS %s\n", full_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "File deleted successfully.", 26) == 0)
                {
                    char *command = strtok(request, " ");
                    char *path = strtok(NULL, " ");
                    for (int i = 0; i < server_count; i++)
                    {
                        for (int j = 0; j < storageServers[i].count_paths; j++)
                        {
                            if (strcmp(storageServers[i].Accessible_Paths[j], path) == 0)
                            {
                                for (int k = j; k < storageServers[i].count_paths - 1; k++)
                                {
                                    strcpy(storageServers[i].Accessible_Paths[k], storageServers[i].Accessible_Paths[k + 1]);
                                }
                                strcpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths - 1], "");
                                if (removePath(root, path, 0))
                                    printf("Path %s removed from trie successfully.\n", path);
                                else
                                    printf("Path '%s' not found or couldn't be removed.\n", path);
                                delete_from_cache(path);
                                storageServers[i].count_paths--;
                                printf("Path '%s' removed from storage server %s:%d\n", path, storageServers[i].ip, storageServers[i].client_port);
                                break;
                            }
                        }
                    }
                }
                if (strncmp(buffer, "Folder Deleted Successfully", 27) == 0)
                {
                    char *command = strtok(request, " ");
                    char *path = strtok(NULL, " ");
                    for (int i = 0; i < server_count; i++)
                    {
                        int j = 0;
                        while (j < storageServers[i].count_paths)
                        {
                            if (strncmp(storageServers[i].Accessible_Paths[j], path, strlen(path)) == 0)
                            {
                                (removePath(root, storageServers[i].Accessible_Paths[j], 0));
                                delete_from_cache(storageServers[i].Accessible_Paths[j]);
                                printf("Removing Path: %s\n", storageServers[i].Accessible_Paths[j]);
                                for (int k = j; k < storageServers[i].count_paths - 1; k++)
                                {
                                    strcpy(storageServers[i].Accessible_Paths[k], storageServers[i].Accessible_Paths[k + 1]);
                                }
                                strcpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths - 1], "");
                                storageServers[i].count_paths--;
                                continue;
                            }
                            j++;
                        }
                    }
                }
                if (strncmp(buffer, "Data Copied successfully.", 25) == 0)
                {
                    char *command = strtok(request, " ");
                    char *copy_type = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");

                    char *src_name = strrchr(src_path, '/');
                    if (src_name)
                        src_name++;
                    else
                        src_name = src_path;

                    char new_path[BUFFER_SIZE];
                    snprintf(new_path, sizeof(new_path), "%s/%s", dest_path, src_name);
                    if (!root)
                        root = createNode();
                    int index = get_server_index(dest_path);
                    printf("%d\n", index);
                    insertPathToTrie(new_path, index, storageServers[index].count_paths);

                    for (int i = 0; i < server_count; i++)
                    {
                        if (strcmp(ss_ip, storageServers[i].ip) == 0 && port == storageServers[i].client_port)
                        {
                            if (storageServers[i].count_paths < MAX_PATHS)
                            {
                                strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], new_path, MAX_LEN);
                                storageServers[i].count_paths++;
                                printf("New path added to SS: %s\n", new_path);
                                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: New path added to SS %s \n", new_path);
                            }
                            else
                            {
                                printf("Max paths reached for SS\n");
                            }
                            break;
                        }
                    }
                }
                if (strncmp(buffer, "Data from Src File", 18) == 0)
                {
                    printf("1\n");
                    char store[BUFFER_SIZE];
                    strcpy(store, request);
                    char *bhh = strtok(request, " ");
                    char *hhh = strtok(NULL, " ");
                    char *ndhbhd = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");
                    char *dest_ip = strtok(NULL, " ");
                    int dest_port = atoi(strtok(NULL, " "));
                    char new_request[BUFFER_SIZE + 6];
                    snprintf(new_request, sizeof(new_request), "DEST %s", store);
                    char new_buffer[BUFFER_SIZE];
                    char *file_data = malloc(1);
                    size_t file_data_size = 0;
                    if (file_data == NULL)
                    {
                        perror("malloc");
                        return;
                    }
                    file_data[0] = '\0';
                    ssize_t bytes_received;
                    while ((bytes_received = recv(sock, new_buffer, sizeof(new_buffer) - 1, 0)) > 0)
                    {
                        printf("2\n");
                        new_buffer[bytes_received] = '\0';
                        printf("%s\n", new_buffer);
                        if (strstr(new_buffer, "FILE_TRANSFER_END") != NULL)
                        {
                            char *end_pos = strstr(new_buffer, "FILE_TRANSFER_END");
                            *end_pos = '\0';
                            file_data_size += strlen(new_buffer);
                            file_data = realloc(file_data, file_data_size + 1);
                            if (file_data == NULL)
                            {
                                perror("realloc");
                                free(file_data);
                                return;
                            }
                            strcat(file_data, new_buffer);
                            break;
                        }
                        file_data_size += bytes_received;
                        file_data = realloc(file_data, file_data_size + 1);
                        if (file_data == NULL)
                        {
                            perror("realloc");
                            free(file_data);
                            return;
                        }
                        strncat(file_data, new_buffer, bytes_received);
                    }
                    // if (file_data != NULL)
                    // {
                    //     printf("File data received:\n%s\n", file_data);
                    // }
                    /*--------------------------------------------------------------NEED TO ADD TO LOG-------------------------*/
                    int sock;
                    struct sockaddr_in server_addr;
                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    printf("%d\n", sock);
                    if (sock < 0)
                    {
                        perror("Socket creation failed\n");
                        exit(EXIT_FAILURE);
                    }
                    memset(&server_addr, 0, sizeof(server_addr));
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_addr.s_addr = inet_addr(dest_ip);
                    server_addr.sin_port = htons(dest_port - 1);
                    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                    {
                        perror("Connection to the SS failed");
                        close(sock);
                        exit(EXIT_FAILURE);
                    }
                    printf("Connected to SS at %s:%d\n", dest_ip, dest_port - 1);
                    if (send(sock, new_request, strlen(new_request), 0) < 0)
                    {
                        perror("Failed to send message to SS");
                    }
                    else
                    {
                        printf("Request sent to SS: %s\n\n", new_request);
                    }
                    int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (strncmp(buffer, "Ready to Receive data", 21) == 0)
                    {
                        size_t total_bytes_sent = 0;
                        size_t file_data_len = file_data_size; // Ensure this is the correct length (in bytes) of the file data
                        ssize_t bytes_sent;

                        // Send `file_data` in chunks until all data is sent
                        while (total_bytes_sent < file_data_len)
                        {
                            bytes_sent = send(sock, file_data + total_bytes_sent, file_data_len - total_bytes_sent, 0);

                            if (bytes_sent < 0)
                            {
                                perror("Send error");
                                break; // Exit on error
                            }

                            total_bytes_sent += bytes_sent;
                        }

                        if (total_bytes_sent == file_data_len)
                        {
                            printf("File data sent successfully.\n");
                        }
                        else
                        {
                            printf("Data sending incomplete.\n");
                        }
                        free(file_data);
                        const char *end_message = "FILE_TRANSFER_END";
                        send(sock, end_message, strlen(end_message), 0);

                        int ddd = recv(sock, buffer, sizeof(buffer) - 1, 0);
                        snprintf(response, BUFFER_SIZE + 5, "ACK %s", buffer);
                        char *src_name = strrchr(src_path, '/');
                        if (src_name)
                            src_name++;
                        else
                            src_name = src_path;

                        char new_path[BUFFER_SIZE];
                        snprintf(new_path, sizeof(new_path), "%s/%s", dest_path, src_name);
                        if (!root)
                            root = createNode();
                        int index = get_server_index(dest_path);
                        printf("%d\n", index);
                        insertPathToTrie(new_path, index, storageServers[index].count_paths);
                        for (int i = 0; i < server_count; i++)
                        {
                            if (strcmp(dest_ip, storageServers[i].ip) == 0 && dest_port == storageServers[i].client_port)
                            {
                                if (storageServers[i].count_paths < MAX_PATHS)
                                {
                                    strncpy(storageServers[i].Accessible_Paths[storageServers[i].count_paths], new_path, MAX_LEN);
                                    storageServers[i].count_paths++;
                                    printf("New path added to SS: %s\n", new_path);
                                }
                                else
                                {
                                    printf("Max paths reached for SS\n");
                                }
                                break;
                            }
                        }
                    }
                }
                if (strncmp(buffer, "Data from Src Folder", 20) == 0)
                {
                    char store[BUFFER_SIZE];
                    strcpy(store, request);
                    char *bhh = strtok(request, " ");
                    char *sdjsdhdud = strtok(NULL, " ");
                    char *hhh = strtok(NULL, " ");
                    char *src_path = strtok(NULL, " ");
                    char *dest_path = strtok(NULL, " ");
                    char *dest_ip = strtok(NULL, " ");
                    int dest_port = atoi(strtok(NULL, " "));
                    char new_request[BUFFER_SIZE + 6];
                    snprintf(new_request, sizeof(new_request), "DEST1 %s", store);
                    FileSystem fs = {.count = 0};
                    receive_data(sock, &fs);
                    print_file_system(&fs);
                    int sock;
                    struct sockaddr_in server_addr;
                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    printf("%d\n", sock);
                    if (sock < 0)
                    {
                        perror("Socket creation failed\n");
                        exit(EXIT_FAILURE);
                    }
                    memset(&server_addr, 0, sizeof(server_addr));
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_addr.s_addr = inet_addr(dest_ip);
                    server_addr.sin_port = htons(dest_port - 1);
                    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
                    {
                        perror("Connection to the SS failed");
                        close(sock);
                        exit(EXIT_FAILURE);
                    }
                    printf("Connected to SS at %s:%d\n", dest_ip, dest_port - 1);
                    if (send(sock, new_request, strlen(new_request), 0) < 0)
                    {
                        perror("Failed to send message to SS");
                    }
                    else
                    {
                        printf("Request sent to SS: %s\n\n", new_request);
                    }

                    int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (strncmp(buffer, "Ready to Receive data", 21) == 0)
                    {
                        send_file_system(sock, &fs, src_path, dest_path);
                    }
                    if (!root)
                    {
                        root = createNode(); // Ensure Trie root is initialized
                    }
                    size_t src_path_len = strlen(src_path);
                    const char *src_base_name = strrchr(src_path, '/');
                    if (src_base_name)
                        src_base_name++;
                    else
                        src_base_name = src_path;
                    printf("fssscount:-%d\n", fs.count);
                    for (int i = 0; i < fs.count; i++)
                    {
                        FileData entry = fs.files[i];
                        const char *relative_path = entry.path + src_path_len;
                        if (*relative_path == '/')
                            relative_path++;
                        char dest_full_path[4096];
                        if (relative_path[0] != '\0')
                        {
                            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s/%s", dest_path, src_base_name, relative_path);
                        }
                        else
                        {
                            snprintf(dest_full_path, sizeof(dest_full_path), "%s/%s", dest_path, src_base_name);
                        }
                        printf("%s\n", dest_full_path);
                        int index = get_server_index(dest_path); // Map file to storage server index
                        printf("Server index for path %s: %d\n", dest_path, index);
                        insertPathToTrie(dest_full_path, index, storageServers[index].count_paths);
                        for (int j = 0; j < server_count; j++)
                        {
                            if (strcmp(dest_ip, storageServers[j].ip) == 0 && dest_port == storageServers[j].client_port)
                            {
                                if (storageServers[j].count_paths < MAX_PATHS)
                                {
                                    strncpy(storageServers[j].Accessible_Paths[storageServers[j].count_paths], dest_full_path, MAX_LEN);
                                    storageServers[j].count_paths++;
                                    printf("New path added to Storage Server %d: %s\n", j, dest_full_path);
                                }
                                else
                                {
                                    printf("Max paths reached for Storage Server %d\n", j);
                                }
                                break;
                            }
                        }
                    }
                    for (int i = 0; i < fs.count; i++)
                    {
                        free(fs.files[i].data);
                    }
                }
            }
        }
        else
        {
            printf("No response from storage server\n");
        }
    }
}
void handle_list(int client_socket)
{
    char buffer[server_count * MAX_PATHS * MAX_LEN];
    int offset = 0;
    for (int i = 0; i < server_count; i++)
    {
        for (int j = 0; j < storageServers[i].count_paths; j++)
        {
            int len = snprintf(buffer + offset, sizeof(buffer) - offset, "%s\n", storageServers[i].Accessible_Paths[j]);
            if (len < 0 || len >= sizeof(buffer) - offset)
            {
                perror("Buffer overflow");
                return;
            }
            offset += len;
        }
    }
    if (send(client_socket, buffer, offset, 0) < 0)
    {
        perror("Failed to send paths to client");
        return;
    }
    printf("Paths sent to client successfully.\n\n");
    snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: LIST paths sent to client successfully!\n");
}
extern int global_flag;
extern int async_write_active;
extern int error_flag;
extern int path_async_number;
extern pthread_mutex_t flag_mutex;
void *async_message_handler(void *arg)
{
    int client_socket = *(int *)arg;

    while (1)
    {
        pthread_mutex_lock(&flag_mutex);
        
        if (global_flag != -1)
        {
            printf("globalflag%d\n",global_flag);
            char flag_buffer[BUFFER_SIZE];
            snprintf(flag_buffer, sizeof(flag_buffer), "Asynchronous Write Completed:)");
            send(client_socket, flag_buffer, strlen(flag_buffer), 0);
            char async_path[BUFFER_SIZE];
            int index = global_flag;
            strcpy(async_path, storageServers[global_flag].Accessible_Paths[path_async_number]);
            char request_backup[2 * BUFFER_SIZE+100];
            char dest_path_backup[1000];
            int fd = 1;
            // int index_dest = get_server_index_by_ip(storageServers[index].back_1_ip);
            if (storageServers[index].no_backup_ss > 0)
            {
                // int index = get_server_index_by_ip(ss_ip,port);
                if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                {
                    // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                    // strcat(src_path_backup,src_path);
                    strcpy(dest_path_backup, storageServers[storageServers[index].first_bk_ss].backup);

                    // strcat(dest_path_backup,path);
                    last_slash(dest_path_backup, async_path);
                    printf("DEST PATH BACKUP %s\n", dest_path_backup);
                    snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d",fd, async_path,
                             dest_path_backup, storageServers[index].backup_1_ip, storageServers[index].backup_clientport_1);
                    send_to_SS_dup(storageServers[index].ip, storageServers[index].client_port, request_backup, client_socket);
                }
                else
                {
                    printf("Backup storage server 1 is not active\n");
                }
                if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                {
                    // memset(src_path_backup,sizeof(src_path_backup),0);
                    memset(dest_path_backup, sizeof(dest_path_backup), 0);
                    memset(request_backup, sizeof(request_backup), 0);
                    // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                    // strcat(src_path_backup,src_path);
                    strcpy(dest_path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                    // strcat(dest_path_backup,dest_path);
                    last_slash(dest_path_backup, async_path);
                    snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d", fd, async_path, dest_path_backup,
                             storageServers[index].backup_2_ip, storageServers[index].backup_clientport_2);
                    send_to_SS_dup(storageServers[index].ip, storageServers[index].client_port, request_backup, client_socket);
                }
                else
                {
                    printf("Only one storage server available to backup data or Backup server is inactive\n");
                }
            }
            else
            {
                printf("No storage servers are there to store the backup data\n");
            }

            global_flag = -1;
        }
        pthread_mutex_unlock(&flag_mutex);

        usleep(100000);
    }

    return NULL;
}
void *async_error_message_handler(void *arg)
{
    int client_socket = *(int *)arg;

    while (1)
    {
        pthread_mutex_lock(&flag_mutex);
        if (error_flag == 1)
        {
            char flag_buffer[BUFFER_SIZE];
            snprintf(flag_buffer, sizeof(flag_buffer), "Asynchronous Write Stopped as SS went offline:(\n");
            send(client_socket, flag_buffer, strlen(flag_buffer), 0);
            error_flag = 0;
        }
        pthread_mutex_unlock(&flag_mutex);

        usleep(100000);
    }

    return NULL;
}
void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);
    pthread_t async_thread;
    pthread_mutex_init(&flag_mutex, NULL);
    if (pthread_create(&async_thread, NULL, async_message_handler, &client_socket) != 0)
    {
        perror("Failed to create asynchronous message handler thread");
        close(client_socket);
    }
    pthread_t async_err_thread;
    pthread_mutex_init(&flag_mutex, NULL);
    if (pthread_create(&async_err_thread, NULL, async_error_message_handler, &client_socket) != 0)
    {
        perror("Failed to create asynchronous message handler thread");
        close(client_socket);
    }
    char request[200];
    char response[MAX_LEN];
    char path_backup[300];

    printf("Client connected\n\n");
    snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: New client connected to NM!\n");

    while (1)
    {
        int flag_from_backup = 0;
        memset(request, 0, sizeof(request));
        memset(path_backup, 0, sizeof(path_backup));
        memset(response, 0, sizeof(response));

        int n = recv(client_socket, request, 200, 0);
        if (n <= 0)
        {
            printf("REQUEST: %s\n", request);
            if (n == 0)
                printf("Client disconnected\n");
            else
                perror("Error in receiving data from client");
            break;
        }
        if (strncmp(request, "READ", 4) == 0 || strncmp(request, "INFO", 4) == 0 || strcmp(request, "LIST") == 0 || strncmp(request, "STREAM", 6) == 0)
        {
            printf("Got a request from the client\n");
            snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Received a request from client %s\n", request);

            printf("%s\n\n", request);
            char path[MAX_LEN];
            if (strncmp(request, "READ", 4) == 0)
            {
                strcpy(path, request + 5);
            }
            else if (strncmp(request, "INFO", 4) == 0)
            {
                strcpy(path, request + 5);
            }
            else if (strcmp(request, "LIST") == 0)
            {
                handle_list(client_socket);
                continue;
            }
            else if (strncmp(request, "STREAM", 6) == 0)
            {
                strcpy(path, request + 7);
            }
            printf("%s\n", path);

            // if (searchInCache(path, response) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response);
            //     send(client_socket, response, strlen(response), 0);
            //     continue;
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(path, response);
            // }
            int cache = searchInCache(path, response);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                printf("SS_DETAILS %s\n\n", response);
                if (storageServers[cache].ss_online != 1)
                {
                    if (strncmp(request, "READ", 4) != 0)
                    {
                        printf("Cannot perform the given operation as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                    else if (strncmp(request, "READ", 4) == 0)
                    {
                        // char path_backup[BUFFER_SIZE];
                        printf("QER: %d\n", storageServers[cache].no_backup_ss);
                        if (storageServers[cache].no_backup_ss > 0)
                        {
                            if (storageServers[storageServers[cache].first_bk_ss].ss_online == 1)
                            {
                                // send this detalis by concatinating
                                strcpy(path_backup, storageServers[storageServers[cache].first_bk_ss].backup);
                                strcat(path_backup, path);
                                memset(response, sizeof(response), 0);
                                snprintf(response, sizeof(response), "%s %d", storageServers[cache].backup_1_ip, storageServers[cache].backup_clientport_1);
                                strcat(response, " ");
                                strcat(response, path_backup);
                                flag_from_backup = 1;
                            }
                            else if (storageServers[cache].no_backup_ss == 2 && storageServers[storageServers[cache].second_bk_ss].ss_online == 1)
                            {
                                // send this details
                                strcpy(path_backup, storageServers[storageServers[cache].second_bk_ss].backup);
                                strcat(path_backup, path);
                                memset(response, sizeof(response), 0);
                                snprintf(response, sizeof(response), "%s %d", storageServers[cache].backup_2_ip, storageServers[cache].backup_clientport_2);
                                strcat(response, " ");
                                strcat(response, path_backup);
                                flag_from_backup = 1;
                            }
                            else
                            {
                                // one or two inactive
                                printf("Cannot perform the given operation as backup ss is/are offline :(\n");
                                send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                                continue;
                            }
                        }
                        else
                        {
                            // no backup servers
                            printf("Cannot perform the given operation as ss is offline  and has no backup servers:(\n");
                            send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                            continue;
                        }
                    }
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(path, response);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        if (strncmp(request, "READ", 4) != 0)
                        {
                            printf("Cannot perform the given operation as ss is offline :(\n");
                            send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                            continue;
                        }
                        else if (strncmp(request, "READ", 4) == 0)
                        {
                            printf("NOOOOOOOOO:%d\n", storageServers[cache].no_backup_ss);
                            if (storageServers[cache].no_backup_ss > 0)
                            {
                                if (storageServers[storageServers[cache].first_bk_ss].ss_online == 1)
                                {
                                    // send this detalis by concatinating
                                    strcpy(path_backup, storageServers[storageServers[cache].first_bk_ss].backup);
                                    strcat(path_backup, path);
                                    memset(response, sizeof(response), 0);
                                    snprintf(response, sizeof(response), "%s %d", storageServers[cache].backup_1_ip, storageServers[cache].backup_clientport_1);
                                    strcat(response, " ");
                                    strcat(response, path_backup);
                                    flag_from_backup = 1;
                                }
                                else if (storageServers[cache].no_backup_ss == 2 && storageServers[storageServers[cache].second_bk_ss].ss_online == 1)
                                {
                                    // send this details
                                    strcpy(path_backup, storageServers[storageServers[cache].second_bk_ss].backup);
                                    strcat(path_backup, path);
                                    memset(response, sizeof(response), 0);
                                    snprintf(response, sizeof(response), "%s %d", storageServers[cache].backup_2_ip, storageServers[cache].backup_clientport_2);
                                    strcat(response, " ");
                                    strcat(response, path_backup);
                                    flag_from_backup = 1;
                                }
                                else
                                {
                                    // one or two inactive
                                    printf("Cannot perform the given operation as backup ss is/are offline :(\n");
                                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                                    continue;
                                }
                            }
                            else
                            {
                                // no backup servers
                                printf("Cannot perform the given operation as ss is offline  and has no backup servers:(\n");
                                send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                                continue;
                            }
                        }
                    }
                }
            }
            if (flag_from_backup != 1 && strcmp(response, "Not found") != 0)
            {
                strcat(response, " ");
                strcat(response, path);
            }
            printf("SS_DETAILS %s\n\n", response);
            send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(request, "LOG") == 0)
        {
            for (int i = 0; i < log_size; i++)
            {
                printf("%s\n", log_ack[i]);
            }
        }
        else if (strncmp(request, "WRITE", 5) == 0)
        {
            char buffer[BUFFER_SIZE];
            printf("Got a request from the client\n");
            printf("%s\n\n", request);
            char path[MAX_LEN];
            int fd;
            int in_ss;
            int ret = sscanf(request, "WRITE %d %s", &fd, path);
            // strcpy(path, request + 6);
            printf("%s\n", path);
            int cache = searchInCache(path, response);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                // printf("SS_DETAILS %s\n\n", response);
                if (storageServers[cache].ss_online != 1)
                {
                    printf("Cannot write as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);
                    continue;
                }
                strcat(response, " ");
                strcat(response, path);
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                in_ss = get_ss_details(path, response);
                if (in_ss != -1)
                {
                    strcat(response, " ");
                    strcat(response, path);
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot write as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                }
            }

            printf("SS_DETAILS %s\n\n", response);
            send(client_socket, response, strlen(response), 0);
            char write_buffer[BUFFER_SIZE];
            if (fd == 2)
            {
                int data_write = recv(client_socket, write_buffer, sizeof(write_buffer), 0);
                if (strncmp(write_buffer, "Data written successfully :)", 28) == 0)
                {
                    char ss_ip[INET_ADDRSTRLEN];
                    int port, index;
                    sscanf(response, "%s %d", ss_ip, &port);
                    if (cache != -1 || in_ss != -1)
                    {
                        if (cache != -1)
                        {
                            index = cache;
                        }
                        else if (in_ss != -1)
                        {
                            index = in_ss;
                        }
                    }
                    char request_backup[2 * BUFFER_SIZE];
                    char dest_path_backup[BUFFER_SIZE + 100];
                    // int index_dest = get_server_index_by_ip(storageServers[index].back_1_ip);
                    if (storageServers[index].no_backup_ss > 0)
                    {
                        // int index = get_server_index_by_ip(ss_ip,port);
                        if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                        {
                            // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                            // strcat(src_path_backup,src_path);
                            strcpy(dest_path_backup, storageServers[storageServers[index].first_bk_ss].backup);

                            // strcat(dest_path_backup,path);
                            last_slash(dest_path_backup, path);
                            printf("DEST PATH BACKUP %s\n", dest_path_backup);
                            snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d", fd, path,
                                     dest_path_backup, storageServers[index].backup_1_ip, storageServers[index].backup_clientport_1);
                            send_to_SS_dup(ss_ip, port, request_backup, client_socket);
                        }
                        else
                        {
                            printf("Backup storage server 1 is not active\n");
                        }
                        if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                        {
                            // memset(src_path_backup,sizeof(src_path_backup),0);
                            memset(dest_path_backup, sizeof(dest_path_backup), 0);
                            memset(request_backup, sizeof(request_backup), 0);
                            // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                            // strcat(src_path_backup,src_path);
                            strcpy(dest_path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                            // strcat(dest_path_backup,dest_path);
                            last_slash(dest_path_backup, path);
                            snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d", fd, path, dest_path_backup,
                                     storageServers[index].backup_2_ip, storageServers[index].backup_clientport_2);
                            send_to_SS_dup(ss_ip, port, request_backup, client_socket);
                        }
                        else
                        {
                            printf("Only one storage server available to backup data or Backup server is inactive\n");
                        }
                    }
                    else
                    {
                        printf("No storage servers are there to store the backup data\n");
                    }
                }
            }
        }
        else if (strncmp(request, "CREATE", 6) == 0)
        {
            printf("Got a request from the client\n");
            printf("%s\n\n", request);
            char path[MAX_LEN];
            int fd;
            char filename[MAX_LEN];
            int ret = sscanf(request, "CREATE %d %s %s", &fd, path, filename);
            printf("%s\n", path);
            // if (searchInCache(path, response) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response);
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(path, response);
            // }
            int cache = searchInCache(path, response);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                // printf("gdgdg%s\n",response);
                //  printf("SS_DETAILS %s\n\n", response);
                // printf("cache:%d\n",cache);
                if (storageServers[cache].ss_online == 0)
                {
                    printf("Cannot create as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                    continue;
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(path, response);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot create as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                }
            }

            printf("SS_DETAILS %s\n\n", response);
            if (strcmp(response, "Not found") == 0)
            {
                printf("Path not found Try again:(\n\n");
                send(client_socket,response,strlen(response),0);
                continue;
            }
            else
            {
                char ss_ip[INET_ADDRSTRLEN];
                int port;
                sscanf(response, "%s %d", ss_ip, &port);
                send_to_SS(ss_ip, port, request, client_socket);
                int index = get_server_index_by_ip(ss_ip, port);
                char request_backup[2 * BUFFER_SIZE];
                char path_backup[BUFFER_SIZE + 100];
                if (storageServers[index].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                    {
                        strcpy(path_backup, storageServers[storageServers[index].first_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "CREATE %d %s %s", fd, path_backup, filename);
                        send_to_SS_dup(storageServers[index].backup_1_ip, storageServers[index].backup_clientport_1, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                    {
                        memset(path_backup, sizeof(path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        strcpy(path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "CREATE %d %s %s", fd, path_backup, filename);
                        send_to_SS_dup(storageServers[index].backup_2_ip, storageServers[index].backup_clientport_2, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
        }
        else if (strncmp(request, "DELETEFILE", 10) == 0)
        {
            printf("Got a request from the client\n");
            printf("%s\n\n", request);
            char path[MAX_LEN];
            strcpy(path, request + 11);
            printf("%s\n", path);
            // if (searchInCache(path, response) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response);
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(path, response);
            // }
            int cache = searchInCache(path, response);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                // printf("SS_DETAILS %s\n\n", response);
                if (storageServers[cache].ss_online != 1)
                {
                    printf("Cannot delete as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                    continue;
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(path, response);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot delete as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                }
            }

            printf("SS_DETAILS %s\n\n", response);
            if (strcmp(response, "Not found") == 0)
            {
                printf("Path not found Try again:(\n\n");
                send(client_socket,response,strlen(response),0);
                continue;
            }
            else
            {
                char ss_ip[INET_ADDRSTRLEN];
                int port;
                sscanf(response, "%s %d", ss_ip, &port);
                send_to_SS(ss_ip, port, request, client_socket);
                int index = get_server_index_by_ip(ss_ip, port);
                char request_backup[2 * BUFFER_SIZE];
                char path_backup[BUFFER_SIZE + 100];
                if (storageServers[index].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                    {
                        strcpy(path_backup, storageServers[storageServers[index].first_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "DELETEFILE %s", path_backup);
                        send_to_SS_dup(storageServers[index].backup_1_ip, storageServers[index].backup_clientport_1, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                    {
                        memset(path_backup, sizeof(path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        strcpy(path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "DELETEFILE %s", path_backup);
                        send_to_SS_dup(storageServers[index].backup_2_ip, storageServers[index].backup_clientport_2, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
        }
        else if (strncmp(request, "DELETEFOLDER", 12) == 0)
        {
            printf("Got a request from the client\n");
            printf("%s\n\n", request);
            char path[MAX_LEN];
            strcpy(path, request + 13);
            printf("%s\n", path);
            // if (searchInCache(path, response) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response);
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(path, response);
            // }
            int cache = searchInCache(path, response);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                // printf("SS_DETAILS %s\n\n", response);
                if (storageServers[cache].ss_online != 1)
                {
                    printf("Cannot delete as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                    continue;
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(path, response);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot delete as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                }
            }

            printf("SS_DETAILS %s\n\n", response);
            if (strcmp(response, "Not found") == 0)
            {
                printf("Path not found Try again:(\n\n");
                send(client_socket,response,strlen(response),0);
                continue;
            }
            else
            {
                char ss_ip[INET_ADDRSTRLEN];
                int port;
                sscanf(response, "%s %d", ss_ip, &port);
                send_to_SS(ss_ip, port, request, client_socket);
                int index = get_server_index_by_ip(ss_ip, port);
                char request_backup[2 * BUFFER_SIZE];
                char path_backup[BUFFER_SIZE + 100];
                if (storageServers[index].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                    {
                        strcpy(path_backup, storageServers[storageServers[index].first_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "DELETEFOLDER %s", path_backup);
                        send_to_SS_dup(storageServers[index].backup_1_ip, storageServers[index].backup_clientport_1, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                    {
                        memset(path_backup, sizeof(path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        strcpy(path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                        strcat(path_backup, path);
                        snprintf(request_backup, sizeof(request_backup), "DELETEFOLDER %s", path_backup);
                        send_to_SS_dup(storageServers[index].backup_2_ip, storageServers[index].backup_clientport_2, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
        }
        else if (strncmp(request, "COPY", 4) == 0)
        {
            char store[MAX_LEN];
            strcpy(store, request);
            printf("%s\n", request);
            char response1[MAX_LEN];
            char response2[MAX_LEN];
            char *command = strtok(request, " ");
            int fd = atoi(strtok(NULL, " "));
            char *src_path = strtok(NULL, " ");
            char *dest_path = strtok(NULL, " ");

            printf("src_path:%s\n", src_path);
            printf("dest_path:%s\n", dest_path);
            // if (searchInCache(src_path, response1) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response1);
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(src_path, response1);
            // }
            int cache = searchInCache(src_path, response1);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                // printf("SS_DETAILS %s\n\n", response1);
                if (storageServers[cache].ss_online != 1)
                {
                    printf("Cannot copy as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                    continue;
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(src_path, response1);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot copy as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                        continue;
                    }
                }
            }

            if (strcmp(response1, "Not found") == 0)
            {
                printf("Source Path not found Try again:(\n\n");
                send(client_socket,response1,strlen(response1),0);
                continue;
            }
            // if (searchInCache(dest_path, response2) == 1)
            // {
            //     printf("\nFound in cache! Sending details to client!\n");
            //     printf("SS_DETAILS %s\n\n", response2);
            // }
            // else
            // {
            //     printf("\nDid not find in cache, searching in memory\n");
            //     get_ss_details(dest_path, response2);
            // }
            cache = searchInCache(dest_path, response2);
            if (cache != -1)
            {
                printf("\nFound in cache! Sending details to client!\n");
                printf("SS_DETAILS %s\n\n", response2);
                if (storageServers[cache].ss_online != 1)
                {
                    printf("Cannot copy as ss is offline :(\n");
                    send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);

                    continue;
                }
            }
            else
            {
                printf("\nDid not find in cache, searching in memory\n");
                /// dedeepya
                int in_ss = get_ss_details(dest_path, response2);
                if (in_ss != -1)
                {
                    if (storageServers[in_ss].ss_online != 1)
                    {
                        printf("Cannot copy as ss is offline :(\n");
                        send(client_socket, "SS is offline cannot perform operation:(\n", strlen("SS is offline cannot perform operation:(\n"), 0);
                        continue;
                    }
                }
            }

            if (strcmp(response2, "Not found") == 0)
            {
                printf("Destination Path not found Try again:(\n\n");
                send(client_socket,response2,strlen(response2),0);
                continue;
            }
            char src_ip[INET_ADDRSTRLEN];
            int src_port;
            char dest_ip[INET_ADDRSTRLEN];
            int dest_port;
            sscanf(response1, "%s %d", src_ip, &src_port);
            sscanf(response2, "%s %d", dest_ip, &dest_port);
            char buffer1[BUFFER_SIZE];
            snprintf(buffer1, sizeof(buffer1), "COPY1 %s", store);
            char buffer2[BUFFER_SIZE];
            snprintf(buffer2, sizeof(buffer2), "COPY2 %s %s %d", store, dest_ip, dest_port);
            char buffer3[BUFFER_SIZE];
            snprintf(buffer3, sizeof(buffer3), "COPY3 %s %s %d", store, dest_ip, dest_port);
            if (strcmp(response1, response2) == 0)
            {
                send_to_SS(src_ip, src_port, buffer1, client_socket);
                int index = get_server_index_by_ip(src_ip, src_port);
                char request_backup[2 * BUFFER_SIZE];
                char src_path_backup[BUFFER_SIZE + 100];
                char dest_path_backup[BUFFER_SIZE + 100];
                if (storageServers[index].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index].first_bk_ss].ss_online == 1)
                    {
                        // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index].first_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY1 COPY %d %s %s", fd, src_path, dest_path_backup);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index].no_backup_ss == 2 && storageServers[storageServers[index].second_bk_ss].ss_online == 1)
                    {
                        // memset(src_path_backup,sizeof(src_path_backup),0);
                        memset(dest_path_backup, sizeof(dest_path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index].second_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY1 COPY %d %s %s", fd, src_path, dest_path_backup);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
            else if (strcmp(response1, response2) != 0 && fd == 1)
            {
                send_to_SS(src_ip, src_port, buffer2, client_socket);
                int index = get_server_index_by_ip(src_ip, src_port);
                int index_dest = get_server_index_by_ip(dest_ip, dest_port);
                printf("dup index: %d\n", index);
                // printf("dup dests: %s %d",)
                char request_backup[2 * BUFFER_SIZE];
                char src_path_backup[BUFFER_SIZE + 100];
                char dest_path_backup[BUFFER_SIZE + 100];
                if (storageServers[index_dest].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index_dest].first_bk_ss].ss_online == 1)
                    {
                        // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index_dest].first_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d", fd, src_path,
                                 dest_path_backup, storageServers[index_dest].backup_1_ip, storageServers[index_dest].backup_clientport_1);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index_dest].no_backup_ss == 2 && storageServers[storageServers[index_dest].second_bk_ss].ss_online == 1)
                    {
                        // memset(src_path_backup,sizeof(src_path_backup),0);
                        memset(dest_path_backup, sizeof(dest_path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index_dest].second_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY2 COPY %d %s %s %s %d", fd, src_path, dest_path_backup,
                                 storageServers[index_dest].backup_2_ip, storageServers[index_dest].backup_clientport_2);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
            else
            {
                send_to_SS(src_ip, src_port, buffer3, client_socket);
                int index = get_server_index_by_ip(src_ip, src_port);
                int index_dest = get_server_index_by_ip(dest_ip, dest_port);
                char request_backup[2 * BUFFER_SIZE];
                char src_path_backup[BUFFER_SIZE + 100];
                char dest_path_backup[BUFFER_SIZE + 100];
                if (storageServers[index_dest].no_backup_ss > 0)
                {
                    // int index = get_server_index_by_ip(ss_ip,port);
                    if (storageServers[storageServers[index_dest].first_bk_ss].ss_online == 1)
                    {
                        // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index_dest].first_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY3 COPY %d %s %s %s %d", fd, src_path, dest_path_backup, storageServers[index_dest].backup_1_ip, storageServers[index_dest].backup_clientport_1);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Backup storage server 1 is not active\n");
                    }
                    if (storageServers[index_dest].no_backup_ss == 2 && storageServers[storageServers[index_dest].second_bk_ss].ss_online == 1)
                    {
                        // memset(src_path_backup,sizeof(src_path_backup),0);
                        memset(dest_path_backup, sizeof(dest_path_backup), 0);
                        memset(request_backup, sizeof(request_backup), 0);
                        // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                        // strcat(src_path_backup,src_path);
                        strcpy(dest_path_backup, storageServers[storageServers[index_dest].second_bk_ss].backup);
                        strcat(dest_path_backup, dest_path);
                        snprintf(request_backup, sizeof(request_backup), "COPY3 COPY %d %s %s %s %d", fd, src_path, dest_path_backup, storageServers[index_dest].backup_2_ip, storageServers[index_dest].backup_clientport_2);
                        send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                    }
                    else
                    {
                        printf("Only one storage server available to backup data or Backup server is inactive\n");
                    }
                }
                else
                {
                    printf("No storage servers are there to store the backup data\n");
                }
            }
        }
    }
    // snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Response from SS %s\n", response);
    snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: Client disconnected!\n");

    close(client_socket);
    return NULL;
}

int main()
{
    initializeNM();
    pthread_t timeout_thread;
    if (pthread_create(&timeout_thread, NULL, check_timeouts_thread, NULL) != 0)
    {
        perror("Failed to create timeout thread");
        exit(1);
    }
    char ip[INET_ADDRSTRLEN];
    int port;
    char request[100];
    char response[MAX_LEN];
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        printf("Connecting to client...\n");

        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(nm_client_socket, (struct sockaddr *)&client_addr, &client_len);

        if (*client_socket < 0)
        {
            perror("Failed to accept client connection");
            free(client_socket);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_socket) != 0)
        {
            perror("Failed to create thread");
            free(client_socket);
            continue;
        }

        pthread_detach(tid);
    }
    return 0;
}
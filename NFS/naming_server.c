#include "naming_server.h"
int nm_client_socket;
int nm_ss_socket;
int server_count = 0;
ServerInfo storageServers[MAX_SS];
extern int global_flag;
extern int async_write_active;
extern int error_flag;
extern int path_async_number;
extern pthread_mutex_t flag_mutex;
int LRU_size = 0;
ServerInfo storageServers[MAX_SS];
CacheNode LRU_cache[MAX_CACHE_LEN];
TrieNode *root = NULL;
char log_ack[MAX_LOGS][MAX_PATH_LENGTH];
int log_size = 0;
void getIPAddress(char *ipBuffer)
{
    struct ifaddrs *ifaddr, *ifa;
    int family;

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Iterate over the interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;
        family = ifa->ifa_addr->sa_family;

        // Only consider IPv4 interfaces
        if (family == AF_INET)
        {
            // Exclude localhost addresses
            if (strcmp(ifa->ifa_name, "lo") != 0)
            {
                inet_ntop(family, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ipBuffer, INET_ADDRSTRLEN);
                break; // Stop after finding the first non-localhost IPv4 address
            }
        }
    }

    freeifaddrs(ifaddr);
}

int isValidPathFormat(const char *path)
{
    if (!path || path[0] != '/')
        return 0;
    if (strstr(path, ".."))
        return 0; // Prevent directory traversal
    return 1;
}

TrieNode *createNode()
{
    TrieNode *node = (TrieNode *)malloc(sizeof(TrieNode));
    node->is_end_of_path = 0;
    node->server_index = -1;
    node->path_index = -1;
    for (int i = 0; i < 256; i++)
    {
        node->children[i] = NULL;
    }
    return node;
}

void insertPathToTrie(const char *path, int server_index, int path_index)
{
    printf("TRIE:%s\n", path);
    TrieNode *node = root;
    for (int i = 0; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!node->children[index])
        {
            node->children[index] = createNode();
        }
        node = node->children[index];
    }
    node->is_end_of_path = 1;
    node->server_index = server_index;
    node->path_index = path_index;
}

void AfterHit(int a)
{
    char path[MAX_LENGTH_PATH];
    char ip[INET_ADDRSTRLEN];
    int port;
    strcpy(path, LRU_cache[a].path);
    strcpy(ip, LRU_cache[a].ss_ip);
    port = LRU_cache[a].ss_port;
    for (int i = a; i < LRU_size - 1; i++)
    {
        strcpy(LRU_cache[i].path, LRU_cache[i + 1].path);
        strcpy(LRU_cache[i].ss_ip, LRU_cache[i + 1].ss_ip);
        LRU_cache[i].ss_port = LRU_cache[i + 1].ss_port;
    }
    strcpy(LRU_cache[LRU_size - 1].path, path);
    strcpy(LRU_cache[LRU_size - 1].ss_ip, ip);
    LRU_cache[LRU_size - 1].ss_port = port;
}

void addPathCache(char path[], char ip[], int port)
{
    if (LRU_size == MAX_CACHE_LEN)
    {
        for (int i = 1; i < MAX_CACHE_LEN; i++)
        {
            strcpy(LRU_cache[i - 1].path, LRU_cache[i].path);
            strcpy(LRU_cache[i - 1].ss_ip, LRU_cache[i].ss_ip);
            LRU_cache[i - 1].ss_port = LRU_cache[i].ss_port;
        }
        strcpy(LRU_cache[LRU_size - 1].path, path);
        strcpy(LRU_cache[LRU_size - 1].ss_ip, ip);
        LRU_cache[LRU_size - 1].ss_port = port;
        return;
    }
    else
    {
        strcpy(LRU_cache[LRU_size].path, path);
        strcpy(LRU_cache[LRU_size].ss_ip, ip);
        LRU_cache[LRU_size].ss_port = port;
        LRU_size++;
    }
}

// int searchInCache(char path[], char result[])
// {
//     int flag = -1;
//     int a = 0;
//     for (int i = 0; i < LRU_size; i++)
//     {
//         printf("%s\n", LRU_cache[i].path);
//         if (strcmp(path, LRU_cache[i].path) == 0)
//         {
//             snprintf(result, MAX_LEN, "%s %d", LRU_cache[i].ss_ip, LRU_cache[i].ss_port);
//             a = i;
//             flag = i;
//             break;
//         }
//     }
//     if (flag != -1)
//         AfterHit(a);
//     if (flag == -1)
//         strcpy(result, "Not found");
//     return flag;
// }
int searchInCache(char path[], char result[])
{
    int flag = -1;
    int a = 0;
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port = -1;
    for (int i = 0; i < LRU_size; i++)
    {
        printf("%s\n", LRU_cache[i].path);
        if (strcmp(path, LRU_cache[i].path) == 0)
        {
            snprintf(result, MAX_LEN, "%s %d", LRU_cache[i].ss_ip, LRU_cache[i].ss_port);
            a = i;
            flag = 1;
            strcpy(ss_ip, LRU_cache[i].ss_ip);
            ss_port = LRU_cache[i].ss_port;
            break;
        }
    }
    if (flag)
        AfterHit(a);
    if (ss_port != -1)
    {
        for (int i = 0; i < server_count; i++)
        {
            if (strcmp(storageServers[i].ip, ss_ip) == 0 && ss_port == storageServers[i].client_port)
            {
                return i;
            }
        }
    }
    if (flag == -1)
        strcpy(result, "Not found");
    return flag;
}
int get_ss_details(char path[], char result[])
{
    TrieNode *node = root;
    printf("I came\n");
    for (int i = 0; path[i]; i++)
    {
        unsigned char index = (unsigned char)path[i];
        if (!node)
        {
            strcpy(result, "Not found");
            return -1;
        }
        if (!node->children[index])
        {
            strcpy(result, "Not found");
            return -1;
        }
        node = node->children[index];
    }
    if (node && node->is_end_of_path)
    {
        int server_index = node->server_index;
        snprintf(result, MAX_LEN, "%s %d",
                 storageServers[server_index].ip,
                 storageServers[server_index].client_port);
        addPathCache(path, storageServers[server_index].ip, storageServers[server_index].client_port);
        return server_index;
    }
    strcpy(result, "Not found");
    return -1;
}
bool removePath(TrieNode *node, const char *path, int depth)
{
    if (!node)
        return false;

    if (path[depth] == '\0')
    {
        if (!node->is_end_of_path)
            return false;

        node->is_end_of_path = 0;

        for (int i = 0; i < 256; i++)
        {
            if (node->children[i])
                return false;
        }

        return true;
    }

    unsigned char index = (unsigned char)path[depth];
    if (!node->children[index])
        return false;

    if (removePath(node->children[index], path, depth + 1))
    {
        free(node->children[index]);
        node->children[index] = NULL;

        if (!node->is_end_of_path)
        {
            for (int i = 0; i < 256; i++)
            {
                if (node->children[i])
                    return false;
            }
            return true;
        }
    }

    return false;
}

void *check_timeouts_thread(void *arg)
{
    printf("Timeout check thread started\n");
    char buffer[BUFFER_SIZE];

    while (1)
    {
        for (int i = 0; i < server_count; i++)
        {
            if (!storageServers[i].ss_online)
                continue; // Skip offline servers

            int socket_fd = storageServers[i].socket_fd; // Get the server-specific socket

            // Non-blocking receive for heartbeat
            ssize_t bytes_received = recv(socket_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
            if (bytes_received > 0)
            {
                buffer[bytes_received] = '\0'; // Null terminate the received data
                // printf("Received heartbeat from server: %s\n", storageServers[i].ip);
                storageServers[i].last_heartbeat = time(NULL); // Update last heartbeat timestamp

                // Send acknowledgment to server
                snprintf(buffer, sizeof(buffer), "ACK\n");
                if (send(socket_fd, buffer, strlen(buffer), 0) < 0)
                {
                    perror("Error sending acknowledgment");
                }
                else
                {
                    // printf("Acknowledgment sent to server: %s\n", storageServers[i].ip);
                }
            }
            else if (bytes_received == 0)
            {
                // Connection closed by the server
                printf("Connection closed by storage server: %s\n", storageServers[i].ip);
                printf("flag:%d\n", async_write_active);
                if (async_write_active == 1)
                {
                    printf("Asyncronous Write Stopped Due to SS going offline");
                    error_flag = 1;
                }
                printf("Going to offline case 1\n");
                storageServers[i].ss_online = 0;
                close(socket_fd); // Clean up the socket
            }
            else if (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                // Error receiving data, other than non-blocking errors
                perror("Error receiving heartbeat");
                printf("flag:%d\n", async_write_active);
                if (async_write_active == 1)
                {
                    printf("Asyncronous Write Stopped Due to SS going offline");
                    error_flag = 1;
                }
                printf("Going to offline case 2\n");
                storageServers[i].ss_online = 0;
            }

            // Check for timeout based on the last heartbeat time
            if (time(NULL) - storageServers[i].last_heartbeat > HEARTBEATTIME)
            {
                printf("Storage server %s timed out\n", storageServers[i].ip);
                printf("Going to offline case 3\n");
                storageServers[i].ss_online = 0;
                close(socket_fd); // Clean up the socket for the timed-out server
            }
        }

        sleep(5); // Wait 5 seconds before the next check
    }
    return NULL;
}
void backup_ss_details()
{
    // while()
    printf("Given backup details\n");
    int count = 0;
    int index = server_count;
    printf("Server count = %d index = %d\n", server_count, index);
    int first_bk;
    int second_bk;
    int i = 1;
    while (count < 2 && (index - i) >= 0)
    {
        if (storageServers[index - i].ss_online)
        {
            if (count == 0)
            {
                second_bk = index - i;
                count++;
            }
            else if (count == 1)
            {
                first_bk = index - i;
                count++;
            }
        }
        i++;
    }
    printf("FIRST %d SECOND %d\n", first_bk, second_bk);
    if (count == 1)
    {
        printf("ONLY ONE STORAGE SERVER AVAILABLE\n");
        storageServers[index].no_backup_ss = 1;
        strcpy(storageServers[index].backup_1_ip, storageServers[second_bk].ip);
        storageServers[index].backup_1_port = storageServers[second_bk].nm_port;
        storageServers[index].backup_clientport_1 = storageServers[second_bk].client_port;
        storageServers[index].first_bk_ss = second_bk;
    }
    else if (count == 0)
    {
        printf("NO BACKUP STORAGE SERVERS AVAILABLE\n");
        storageServers[index].no_backup_ss = 1;
    }
    else if (count == 2)
    {
        storageServers[index].no_backup_ss = 2;
        strcpy(storageServers[index].backup_1_ip, storageServers[first_bk].ip);
        storageServers[index].backup_1_port = storageServers[first_bk].nm_port;
        storageServers[index].first_bk_ss = first_bk;
        storageServers[index].backup_clientport_1 = storageServers[first_bk].client_port;
        strcpy(storageServers[index].backup_2_ip, storageServers[second_bk].ip);
        storageServers[index].backup_2_port = storageServers[second_bk].nm_port;
        storageServers[index].backup_clientport_2 = storageServers[second_bk].client_port;
        storageServers[index].second_bk_ss = second_bk;
        printf("%s %d %d\n\n%s %d %d\n\n", storageServers[index].backup_1_ip,
               storageServers[index].backup_1_port, storageServers[index].backup_clientport_1,
               storageServers[index].backup_2_ip, storageServers[index].backup_2_port,
               storageServers[index].backup_clientport_2);
    }
    printf("hhhh%d\n", storageServers[index].no_backup_ss);
    return;
}

int path_exists_in_servers(const char *path)
{
    for (int i = 0; i < server_count; i++)
    {
        for (int j = 0; j < storageServers[i].count_paths; j++)
        {
            if (strcmp(path, storageServers[i].Accessible_Paths[j]) == 0)
            {
                return 1; // Path exists in a storage server
            }
        }
    }
    return 0; // Path does not exist in any storage server
}
void remove_formality(char *path, char *remaining_path, char *starting_path)
{
    int counter = 0;
    int i = 0;
    for (i = 0; i < strlen(path); i++)
    {
        if (path[i] == '/')
        {
            counter++;
        }
        if (counter == 3)
        {
            break;
        }
    }
    // char remainng_path[BUFFER_SIZE];
    strncpy(starting_path, &path[0], i);
    strncpy(remaining_path, &path[i], strlen(path));
    remaining_path[strlen(path)] = '\0'; //  working
    printf("STARTING PATH  %s  REMAINING PATH %s\n", starting_path, remaining_path);
    // return remainng_path;
}

void last_slash(char *dest_path_backup, char *path)
{
    char *last_slash = strrchr(path, '/');

    if (last_slash != NULL)
    {
        // Calculate the length excluding the last '/'
        size_t length = last_slash - path; // Exclude the '/'
        char temp[1024];
        strncpy(temp, path, length);
        temp[length] = '\0'; // Null-terminate the string

        // Concatenate the substring to dest_path_backup
        strcat(dest_path_backup, temp);

        // Output the result
        printf("Concatenated Path: %s\n", dest_path_backup);
    }
    else
    {
        printf("No '/' found in the path.\n");
    }
}

void call_copy(char *src_path, char *dest_path, int client_socket, char *store, int flag)
{
    char response1[BUFFER_SIZE];
    char response2[BUFFER_SIZE];
    printf("Src path = %s Dest path = %s\n", src_path, dest_path);
    get_ss_details(src_path, response1);
    if (strcmp(response1, "Not found") == 0)
    {
        printf("Source Path not found Try again:(\n\n");
    }
    get_ss_details(dest_path, response2);
    if (strcmp(response2, "Not found") == 0)
    {
        printf("Destination Path not found Try again:(\n\n");
    }
    char src_ip[INET_ADDRSTRLEN];
    int src_port;
    char dest_ip[INET_ADDRSTRLEN];
    int dest_port;
    sscanf(response1, "%s %d", src_ip, &src_port);
    sscanf(response2, "%s %d", dest_ip, &dest_port);
    struct stat path_stat;
    char buffer1[BUFFER_SIZE];
    snprintf(buffer1, sizeof(buffer1), "COPY1 %s", store);
    char buffer2[BUFFER_SIZE];
    snprintf(buffer2, sizeof(buffer2), "COPY2 %s %s %d", store, dest_ip, dest_port);
    char buffer3[BUFFER_SIZE];
    snprintf(buffer3, sizeof(buffer3), "COPY3 %s %s %d", store, dest_ip, dest_port);
    if (strcmp(response1, response2) == 0)
    {
        send_to_SS_dup(src_ip, src_port, buffer1, client_socket);
    }
    else if (strcmp(response1, response2) != 0 && (flag == 1))
    {
        send_to_SS_dup(src_ip, src_port, buffer2, client_socket);
    }
    else
    {
        send_to_SS_dup(src_ip, src_port, buffer3, client_socket);
    }
}

void add_to_backup()
{
    char request[BUFFER_SIZE];
    strcpy(request, "CREATE ");
    int ser_index = server_count - 1;
    printf("SER_INDEX = %d\n", ser_index);
    char remaining_path[BUFFER_SIZE];
    char starting_path[BUFFER_SIZE];
    char folder_add[BUFFER_SIZE];
    char path_to_send[BUFFER_SIZE];
    char port_send[BUFFER_SIZE];
    char dest_path[BUFFER_SIZE];
    char request_copy[BUFFER_SIZE];
    char file_dir_str_create[BUFFER_SIZE];
    char file_dir_str[BUFFER_SIZE];
    // int j = 0;
    char already_exists[BUFFER_SIZE];

    if (storageServers[ser_index].no_backup_ss > 0)
    {
        for (int j = 0; j < storageServers[ser_index].count_paths; j++)
        {
            // remove_formality(storageServers[ser_index].Accessible_Paths[0], remaining_path, starting_path);
            // printf("\n\nACCESSIBLE PATHS TILL NOW\n\n");

            // for(int k = 0; k < storageServers[ser_index-2].count_paths ; k++){
            //     printf("%s\n",storageServers[ser_index-2].Accessible_Paths[k]);
            // }
            // printf("\n\n");
            memset(already_exists, 0, sizeof(already_exists));
            strcpy(already_exists, storageServers[storageServers[ser_index].first_bk_ss].backup);
            strcat(already_exists, storageServers[ser_index].Accessible_Paths[j]);
            if (path_exists_in_servers(already_exists))
            {
                printf("Already the path exists in backup\n");
                continue;
            }
            else
            {
                printf("Path not exists\n");
            }
            memset(request, 0, sizeof(request));
            memset(dest_path, 0, sizeof(dest_path));
            memset(request_copy, 0, sizeof(request_copy));
            memset(file_dir_str_create, 0, sizeof(file_dir_str_create));
            memset(file_dir_str, 0, sizeof(file_dir_str));
            strcpy(request, "CREATE ");
            strcpy(remaining_path, storageServers[ser_index].Accessible_Paths[j]);

            // sprintf(file_dir_str_create," %d",storageServers[ser_index].file_dir[j]);
            // sprintf(file_dir_str_create," 2");

            strcat(request, "2 ");
            printf("File or Folder---%s-----\n", file_dir_str_create);
            strcat(request, " "); // folder should be created

            strcat(request, storageServers[storageServers[ser_index].first_bk_ss].backup);

            int index = 1;

            int i = 0;
            strcat(dest_path, storageServers[storageServers[ser_index].first_bk_ss].backup);
            for (i = 1; i < strlen(remaining_path); i++)
            {
                memset(path_to_send, 0, sizeof(path_to_send));
                memset(folder_add, 0, sizeof(folder_add));
                if (remaining_path[i] == '/')
                {

                    int length = i - index;
                    strncpy(folder_add, &remaining_path[index], length);
                    folder_add[length] = '\0'; // Ensure null-termination

                    // printf("Folder_add = %s\n", folder_add);

                    strcpy(path_to_send, request);
                    strcat(path_to_send, " ");
                    strcat(path_to_send, folder_add);

                    printf("FINAL REQUEST %s\n", path_to_send);

                    strcat(request, "/");
                    strcat(request, folder_add);

                    strcat(dest_path, "/");
                    strcat(dest_path, folder_add);

                    index = i + 1;
                    printf("DETAILS GOING %s %d %s %d", storageServers[ser_index].backup_1_ip, storageServers[ser_index].backup_clientport_1,
                           path_to_send, storageServers[ser_index].backup_clientport_1);
                    if (path_exists_in_servers(dest_path))
                    {
                        printf("EXISTING %s\n\n", dest_path);
                    }
                    else if (!path_exists_in_servers(dest_path))
                    {
                        send_to_SS_dup(storageServers[ser_index].backup_1_ip, storageServers[ser_index].backup_clientport_1,
                                       path_to_send, storageServers[ser_index].backup_clientport_1);
                    }
                }
            }
            int length = i - index;
            strncpy(folder_add, &remaining_path[index], length);
            folder_add[length] = '\0';
            printf("DEST path %s Folder_add/File = %s\n", dest_path, folder_add);
            // char request_copy[BUFFER_SIZE];
            strcpy(request_copy, "COPY");
            strcat(request_copy, " ");
            // char file_dir_str[BUFFER_SIZE];
            // int number = storageServers[0].file_dir;
            sprintf(file_dir_str, "%d", storageServers[ser_index].file_dir[j]);

            // strcat(request_copy,number);
            strcat(request_copy, file_dir_str);
            printf("File or Folder---%s--COPY---\n", file_dir_str);
            strcat(request_copy, " ");
            strcat(request_copy, storageServers[ser_index].Accessible_Paths[j]);
            strcat(request_copy, " ");
            strcat(request_copy, dest_path);
            int flag = storageServers[ser_index].file_dir[j];
            call_copy(storageServers[ser_index].Accessible_Paths[j], dest_path, storageServers[ser_index].backup_clientport_1, request_copy, flag);
            printf("Successfully Stored data in 1st Backup Storage Server\n");
        }

        // BACKUP IN SECOND SS

        if (storageServers[ser_index].no_backup_ss == 2)
        {
            for (int j = 0; j < storageServers[ser_index].count_paths; j++)
            {

                memset(already_exists, 0, sizeof(already_exists));
                strcpy(already_exists, storageServers[storageServers[ser_index].second_bk_ss].backup);
                strcat(already_exists, storageServers[ser_index].Accessible_Paths[j]);
                if (path_exists_in_servers(already_exists))
                {
                    printf("Already the path exists in backup\n");
                    continue;
                }
                else
                {
                    printf("Path not exists\n");
                }
                memset(request, 0, sizeof(request));
                memset(dest_path, 0, sizeof(dest_path));
                memset(request_copy, 0, sizeof(request_copy));
                memset(file_dir_str_create, 0, sizeof(file_dir_str_create));
                memset(file_dir_str, 0, sizeof(file_dir_str));
                strcpy(request, "CREATE ");
                strcpy(remaining_path, storageServers[ser_index].Accessible_Paths[j]);

                // sprintf(file_dir_str_create," %d",storageServers[ser_index].file_dir[j]);
                // sprintf(file_dir_str_create," 2");

                strcat(request, "2 ");
                printf("File or Folder---%s-----\n", file_dir_str_create);
                strcat(request, " "); // folder should be created

                strcat(request, storageServers[storageServers[ser_index].second_bk_ss].backup);

                int index = 1;

                int i = 0;
                strcat(dest_path, storageServers[storageServers[ser_index].second_bk_ss].backup);
                for (i = 1; i < strlen(remaining_path); i++)
                {
                    memset(path_to_send, 0, sizeof(path_to_send));
                    memset(folder_add, 0, sizeof(folder_add));
                    if (remaining_path[i] == '/')
                    {

                        int length = i - index;
                        strncpy(folder_add, &remaining_path[index], length);
                        folder_add[length] = '\0'; // Ensure null-termination

                        // printf("Folder_add = %s\n", folder_add);

                        strcpy(path_to_send, request);
                        strcat(path_to_send, " ");
                        strcat(path_to_send, folder_add);

                        printf("FINAL REQUEST %s\n", path_to_send);

                        strcat(request, "/");
                        strcat(request, folder_add);

                        strcat(dest_path, "/");
                        strcat(dest_path, folder_add);

                        index = i + 1;
                        printf("DETAILS GOING %s %d %s %d\n", storageServers[ser_index].backup_2_ip, storageServers[ser_index].backup_clientport_2,
                               path_to_send, storageServers[ser_index].backup_clientport_2);
                        if (!path_exists_in_servers(dest_path))
                        {
                            printf("Path not exists\n");
                            send_to_SS_dup(storageServers[ser_index].backup_2_ip, storageServers[ser_index].backup_clientport_2,
                                           path_to_send, storageServers[ser_index].backup_clientport_2);
                        }
                    }
                }
                int length = i - index;
                strncpy(folder_add, &remaining_path[index], length);
                folder_add[length] = '\0';
                printf("DEST path %s Folder_add/File = %s\n", dest_path, folder_add);
                // char request_copy[BUFFER_SIZE];
                strcpy(request_copy, "COPY");
                strcat(request_copy, " ");
                // char file_dir_str[BUFFER_SIZE];
                // int number = storageServers[0].file_dir;
                sprintf(file_dir_str, "%d", storageServers[ser_index].file_dir[j]);

                // strcat(request_copy,number);
                strcat(request_copy, file_dir_str);
                printf("File or Folder---%s--COPY---\n", file_dir_str);
                strcat(request_copy, " ");
                strcat(request_copy, storageServers[ser_index].Accessible_Paths[j]);
                strcat(request_copy, " ");
                strcat(request_copy, dest_path);
                int flag = storageServers[ser_index].file_dir[j];
                call_copy(storageServers[ser_index].Accessible_Paths[j], dest_path, storageServers[ser_index].backup_clientport_2, request_copy, flag);
                printf("Successfully Stored data in 2nd Backup Storage Server\n");
            }
        }
        else if (storageServers[ser_index].no_backup_ss == 1)
        {
            // printf("Successfully Stored data in 2 Backup Storage Servers\n");
            printf("Only One Backup Server Available\n");
        }
    }
    else
    {
        printf("No Storage Servers Available to store backup data\n");
    }
}

void backup_for_inactive(char *src_ip, int src_port, char *dest_ip, int dest_port, int ser_index, int is_first_second)
{
    char request[BUFFER_SIZE];
    strcpy(request, "CREATE ");
    // int ser_index = server_count - 1;
    printf("SER_INDEX = %d\n", ser_index);
    char remaining_path[BUFFER_SIZE];
    char starting_path[BUFFER_SIZE];
    char folder_add[BUFFER_SIZE];
    char path_to_send[BUFFER_SIZE];
    char port_send[BUFFER_SIZE];
    char dest_path[BUFFER_SIZE];
    char request_copy[BUFFER_SIZE];
    char file_dir_str_create[BUFFER_SIZE];
    char file_dir_str[BUFFER_SIZE];
    // int j = 0;
    char already_exists[BUFFER_SIZE];

    if (storageServers[ser_index].no_backup_ss > 0)
    {
        if (is_first_second == 1)
        {
            for (int j = 0; j < storageServers[ser_index].count_paths; j++)
            {
                // remove_formality(storageServers[ser_index].Accessible_Paths[0], remaining_path, starting_path);
                // printf("\n\nACCESSIBLE PATHS TILL NOW\n\n");

                // for(int k = 0; k < storageServers[ser_index-2].count_paths ; k++){
                //     printf("%s\n",storageServers[ser_index-2].Accessible_Paths[k]);
                // }
                // printf("\n\n");
                memset(already_exists, 0, sizeof(already_exists));
                strcpy(already_exists, storageServers[storageServers[ser_index].first_bk_ss].backup);
                strcat(already_exists, storageServers[ser_index].Accessible_Paths[j]);
                // if (path_exists_in_servers(already_exists))
                // {
                //     printf("Already the path exists in backup\n");
                //     continue;
                // }
                // else
                // {
                //     printf("Path not exists\n");
                // }
                memset(request, 0, sizeof(request));
                memset(dest_path, 0, sizeof(dest_path));
                memset(request_copy, 0, sizeof(request_copy));
                memset(file_dir_str_create, 0, sizeof(file_dir_str_create));
                memset(file_dir_str, 0, sizeof(file_dir_str));
                strcpy(request, "CREATE ");
                strcpy(remaining_path, storageServers[ser_index].Accessible_Paths[j]);

                // sprintf(file_dir_str_create," %d",storageServers[ser_index].file_dir[j]);
                // sprintf(file_dir_str_create," 2");

                strcat(request, "2 ");
                printf("File or Folder---%s-----\n", file_dir_str_create);
                strcat(request, " "); // folder should be created

                strcat(request, storageServers[storageServers[ser_index].first_bk_ss].backup);

                int index = 1;

                int i = 0;
                strcat(dest_path, storageServers[storageServers[ser_index].first_bk_ss].backup);
                for (i = 1; i < strlen(remaining_path); i++)
                {
                    memset(path_to_send, 0, sizeof(path_to_send));
                    memset(folder_add, 0, sizeof(folder_add));
                    if (remaining_path[i] == '/')
                    {

                        int length = i - index;
                        strncpy(folder_add, &remaining_path[index], length);
                        folder_add[length] = '\0'; // Ensure null-termination

                        // printf("Folder_add = %s\n", folder_add);

                        strcpy(path_to_send, request);
                        strcat(path_to_send, " ");
                        strcat(path_to_send, folder_add);

                        printf("FINAL REQUEST %s\n", path_to_send);

                        strcat(request, "/");
                        strcat(request, folder_add);

                        strcat(dest_path, "/");
                        strcat(dest_path, folder_add);

                        index = i + 1;
                        printf("DETAILS GOING %s %d %s %d", storageServers[ser_index].backup_1_ip, storageServers[ser_index].backup_clientport_1,
                               path_to_send, storageServers[ser_index].backup_clientport_1);
                        // if (path_exists_in_servers(dest_path))
                        // {
                        //     printf("EXISTING %s\n\n", dest_path);
                        // }
                        // else if (!path_exists_in_servers(dest_path))
                        // {
                            send_to_SS_dup(storageServers[ser_index].backup_1_ip, storageServers[ser_index].backup_clientport_1,
                                           path_to_send, storageServers[ser_index].backup_clientport_1);
                        // }
                    }
                }
                int length = i - index;
                strncpy(folder_add, &remaining_path[index], length);
                folder_add[length] = '\0';
                printf("DEST path %s Folder_add/File = %s\n", dest_path, folder_add);
                // char request_copy[BUFFER_SIZE];
                strcpy(request_copy, "COPY");
                strcat(request_copy, " ");
                // char file_dir_str[BUFFER_SIZE];
                // int number = storageServers[0].file_dir;
                sprintf(file_dir_str, "%d", storageServers[ser_index].file_dir[j]);

                // strcat(request_copy,number);
                strcat(request_copy, file_dir_str);
                printf("File or Folder---%s--COPY---\n", file_dir_str);
                strcat(request_copy, " ");
                strcat(request_copy, storageServers[ser_index].Accessible_Paths[j]);
                strcat(request_copy, " ");
                strcat(request_copy, dest_path);
                int flag = storageServers[ser_index].file_dir[j];
                call_copy(storageServers[ser_index].Accessible_Paths[j], dest_path, storageServers[ser_index].backup_clientport_1, request_copy, flag);
                printf("Successfully Stored data in 1st Backup Storage Server\n");
            }
        }
        // BACKUP IN SECOND SS

        if (storageServers[ser_index].no_backup_ss == 2)
        {
            if (is_first_second == 2)
            {
                for (int j = 0; j < storageServers[ser_index].count_paths; j++)
                {

                    memset(already_exists, 0, sizeof(already_exists));
                    strcpy(already_exists, storageServers[storageServers[ser_index].second_bk_ss].backup);
                    strcat(already_exists, storageServers[ser_index].Accessible_Paths[j]);
                    // if (path_exists_in_servers(already_exists))
                    // {
                    //     printf("Already the path exists in backup\n");
                    //     continue;
                    // }
                    // else
                    // {
                    //     printf("Path not exists\n");
                    // }
                    memset(request, 0, sizeof(request));
                    memset(dest_path, 0, sizeof(dest_path));
                    memset(request_copy, 0, sizeof(request_copy));
                    memset(file_dir_str_create, 0, sizeof(file_dir_str_create));
                    memset(file_dir_str, 0, sizeof(file_dir_str));
                    strcpy(request, "CREATE ");
                    strcpy(remaining_path, storageServers[ser_index].Accessible_Paths[j]);

                    // sprintf(file_dir_str_create," %d",storageServers[ser_index].file_dir[j]);
                    // sprintf(file_dir_str_create," 2");

                    strcat(request, "2 ");
                    printf("File or Folder---%s-----\n", file_dir_str_create);
                    strcat(request, " "); // folder should be created

                    strcat(request, storageServers[storageServers[ser_index].second_bk_ss].backup);

                    int index = 1;

                    int i = 0;
                    strcat(dest_path, storageServers[storageServers[ser_index].second_bk_ss].backup);
                    for (i = 1; i < strlen(remaining_path); i++)
                    {
                        memset(path_to_send, 0, sizeof(path_to_send));
                        memset(folder_add, 0, sizeof(folder_add));
                        if (remaining_path[i] == '/')
                        {

                            int length = i - index;
                            strncpy(folder_add, &remaining_path[index], length);
                            folder_add[length] = '\0'; // Ensure null-termination

                            // printf("Folder_add = %s\n", folder_add);

                            strcpy(path_to_send, request);
                            strcat(path_to_send, " ");
                            strcat(path_to_send, folder_add);

                            printf("FINAL REQUEST %s\n", path_to_send);

                            strcat(request, "/");
                            strcat(request, folder_add);

                            strcat(dest_path, "/");
                            strcat(dest_path, folder_add);

                            index = i + 1;
                            printf("DETAILS GOING %s %d %s %d\n", storageServers[ser_index].backup_2_ip, storageServers[ser_index].backup_clientport_2,
                                   path_to_send, storageServers[ser_index].backup_clientport_2);
                            // if (!path_exists_in_servers(dest_path))
                            // {
                                // printf("Path not exists\n");
                                send_to_SS_dup(storageServers[ser_index].backup_2_ip, storageServers[ser_index].backup_clientport_2,
                                               path_to_send, storageServers[ser_index].backup_clientport_2);
                            // }
                        }
                    }
                    int length = i - index;
                    strncpy(folder_add, &remaining_path[index], length);
                    folder_add[length] = '\0';
                    printf("DEST path %s Folder_add/File = %s\n", dest_path, folder_add);
                    // char request_copy[BUFFER_SIZE];
                    strcpy(request_copy, "COPY");
                    strcat(request_copy, " ");
                    // char file_dir_str[BUFFER_SIZE];
                    // int number = storageServers[0].file_dir;
                    sprintf(file_dir_str, "%d", storageServers[ser_index].file_dir[j]);

                    // strcat(request_copy,number);
                    strcat(request_copy, file_dir_str);
                    printf("File or Folder---%s--COPY---\n", file_dir_str);
                    strcat(request_copy, " ");
                    strcat(request_copy, storageServers[ser_index].Accessible_Paths[j]);
                    strcat(request_copy, " ");
                    strcat(request_copy, dest_path);
                    int flag = storageServers[ser_index].file_dir[j];
                    call_copy(storageServers[ser_index].Accessible_Paths[j], dest_path, storageServers[ser_index].backup_clientport_2, request_copy, flag);
                    printf("Successfully Stored data in 2nd Backup Storage Server\n");
                }
            }
        }
        else if (storageServers[ser_index].no_backup_ss == 1)
        {
            // printf("Successfully Stored data in 2 Backup Storage Servers\n");
            printf("Only One Backup Server Available\n");
        }
    }
    else
    {
        printf("No Storage Servers Available to store backup data\n");
    }
}

int check_for_ss(char ip[], int nm_port, int client_port)
{
    for (int i = 0; i < server_count; i++)
    {
        if (strcmp(storageServers[i].ip, ip) == 0 && storageServers[i].nm_port == nm_port && storageServers[i].client_port == client_port)
            return 1;
    }
    return 0;
}

int ss_came_from_inactive(char *ip, int port, char response[], int i)
{
    int flag = 1;
    printf("Arrived %s %d\n",ip,port);
    // for(int i = 0; i<server_count ; i++){
    if (storageServers[i].no_backup_ss > 0)
    {
        printf("Entered\n");
        printf("%s %d %s %d %d\n",ip,port,storageServers[i].backup_1_ip,storageServers[i].backup_1_port,storageServers[i].backup_clientport_1);
        if (strcmp(storageServers[i].backup_1_ip, ip) == 0 && (storageServers[i].backup_1_port == port))
        {
            int first = 1;
            snprintf(response, BUFFER_SIZE+30, "%s %d %d", storageServers[i].backup_1_ip, storageServers[i].backup_1_port, first);
            flag = 1;
            printf("Found as backu for %s %d\n",storageServers[i].backup_1_ip,storageServers[i].backup_clientport_1);
            return 1;
        }
        if (storageServers[i].no_backup_ss == 2)
        {
            printf("%s %d %s %d %d\n",ip,port,storageServers[i].backup_2_ip,storageServers[i].backup_2_port,storageServers[i].backup_clientport_2);
        
            if (strcmp(storageServers[i].backup_1_ip, ip) == 0 && (storageServers[i].backup_2_port == port))
            {
                int second = 2;
                snprintf(response, BUFFER_SIZE+30, "%s %d %d", storageServers[i].backup_2_ip, storageServers[i].backup_2_port, second);
                printf("Found as backu for %s %d\n",storageServers[i].backup_1_ip,storageServers[i].backup_clientport_1);
            
                return 1;
            }
        }
    }
    return 0;
    // }
}
void *SS_reg(void *arg)
{
    printf("Waiting for storage servers...\n");
    struct sockaddr_in ss_address = *(struct sockaddr_in *)arg;
    socklen_t ss_addrlen = sizeof(ss_address);

    while (1)
    {
        int new_socket = accept(nm_ss_socket, (struct sockaddr *)&ss_address, &ss_addrlen);
        if (new_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        char buffer[1024];
        int data_read = recv(new_socket, buffer, sizeof(buffer) - 1, 0);
        if (data_read > 0)
        {
            buffer[data_read] = '\0';
            printf("buffeeeeeer:%s\n", buffer);
            if (strncmp(buffer, "Asynchronous write completed", 28) != 0 && strncmp(buffer, "Asynchrnous Write Started", 25) != 0)
            {
                char ip[INET_ADDRSTRLEN];
                int nm_port;
                int client_port;
                sscanf(buffer, "%s %d %d", ip, &nm_port, &client_port);
                printf("%s %d %d\n", ip, nm_port, client_port);

                if (check_for_ss(ip, nm_port, client_port) == 1)
                {
                    printf("This storage server already exists! :)\n");
                    send(new_socket, "This storage server already exists! :)\n", strlen("This storage server already exists! :)\n"), 0);
                    // for (int i = 2; i < server_count; i++)
                    // {
                    //     printf("Started checking for backup\n");
                    //     char response[BUFFER_SIZE+10];
                    //     int is_bakup = ss_came_from_inactive(ip, nm_port, response, i);
                    //     if (is_bakup)
                    //     {
                    //         printf("Is someones backup\n");
                    //         char src_ip[INET_ADDRSTRLEN];
                    //         int src_port;
                    //         int is_first_second;
                    //         sscanf(response, "%s %d %d", src_ip, &src_port, &is_first_second);
                    //         sleep(2);
                    //         backup_for_inactive(src_ip, src_port, ip, client_port, i, is_first_second);
                            //             int index = get_server_index_by_ip(src_ip, src_port);
                            //             int index_dest = get_server_index_by_ip(ip, client_port);
                            //             char request_backup[2 * BUFFER_SIZE];
                            //             char src_path_backup[BUFFER_SIZE + 100];
                            //             char dest_path_backup[BUFFER_SIZE + 100];
                            //             if (storageServers[index_dest].no_backup_ss > 0)
                            //             {
                            //                 // int index = get_server_index_by_ip(ss_ip,port);
                            //                 if (storageServers[storageServers[index_dest].first_bk_ss].ss_online == 1)
                            //                 {
                            //                     // strcpy(src_path_backup,storageServers[storageServers[index].first_bk_ss].backup);
                            //                     // strcat(src_path_backup,src_path);
                            //                     strcpy(dest_path_backup, storageServers[storageServers[index_dest].first_bk_ss].backup);
                            //                     strcat(dest_path_backup, dest_path);
                            //                     snprintf(request_backup, sizeof(request_backup), "COPY3 COPY %d %s %s %s %d", fd, src_path, dest_path_backup, storageServers[index_dest].backup_1_ip, storageServers[index_dest].backup_clientport_1);
                            //                     send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                            //                 }
                            //                 else
                            //                 {
                            //                     printf("Backup storage server 1 is not active\n");
                            //                 }
                            //                 if (storageServers[index_dest].no_backup_ss == 2 && storageServers[storageServers[index_dest].second_bk_ss].ss_online == 1)
                            //                 {
                            //                     // memset(src_path_backup,sizeof(src_path_backup),0);
                            //                     memset(dest_path_backup, sizeof(dest_path_backup), 0);
                            //                     memset(request_backup, sizeof(request_backup), 0);
                            //                     // strcpy(src_path_backup,storageServers[storageServers[index].second_bk_ss].backup);
                            //                     // strcat(src_path_backup,src_path);
                            //                     strcpy(dest_path_backup, storageServers[storageServers[index_dest].second_bk_ss].backup);
                            //                     strcat(dest_path_backup, dest_path);
                            //                     snprintf(request_backup, sizeof(request_backup), "COPY3 COPY %d %s %s %s %d", fd, src_path, dest_path_backup, storageServers[index_dest].backup_2_ip, storageServers[index_dest].backup_clientport_2);
                            //                     send_to_SS_dup(src_ip, src_port, request_backup, client_socket);
                            //                 }
                            //                 else
                            //                 {
                            //                     printf("Only one storage server available to backup data or Backup server is inactive\n");
                            //                 }
                            //             }
                            //             else
                            //             {
                            //                 printf("No storage servers are there to store the backup data\n");
                            //             }
                            //         }
                            //     }
                        // }
                    // }
                    continue;
                }
                printf("Yayy its a new ss!!!\nStorage server registered at %s\n", buffer);
                snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: New storage server registered at %s\n", buffer);
                send(new_socket, "Hello new storager server!! :)\n", strlen("Hello new storager server!! :)\n"), 0);
                strcpy(storageServers[server_count].ip, ip);
                storageServers[server_count].nm_port = nm_port;
                storageServers[server_count].client_port = client_port;
                storageServers[server_count].ss_online = 1;
                storageServers[server_count].last_heartbeat = time(NULL);
                storageServers[server_count].is_active = 1;
                storageServers[server_count].socket_fd = new_socket;
                if (server_count >= 2)
                {
                    backup_ss_details();
                }
                printf("Accessible paths:\n");
                int j = 0;
                int backup_path = 0;
                while ((data_read = recv(new_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
                {
                    char *path_parse = NULL;
                    char *type_content = NULL;
                    buffer[data_read] = '\0';
                    buffer[strcspn(buffer, "\n")] = '\0';

                    if (strncmp(buffer, "END_PATHS", 9) == 0)
                    {
                        printf("%s\n", buffer);
                        send(new_socket, "All paths received.\n", 21, 0);
                        break;
                    }

                    path_parse = strtok(buffer, " ");
                    type_content = strtok(NULL, " ");

                    if (isValidPathFormat(path_parse) &&
                        storageServers[server_count].count_paths < MAX_PATHS && !path_exists_in_servers(path_parse))
                    {
                        if (backup_path == 0)
                        {
                            if (strcmp(type_content, "FILE") == 0)
                            {

                                storageServers[server_count].file_dir[storageServers[server_count].count_paths] = 1;
                            }
                            else if (strcmp(type_content, "DIRECTORY") == 0)
                            {

                                storageServers[server_count].file_dir[storageServers[server_count].count_paths] = 2;
                            }

                            strcpy(storageServers[server_count].backup, path_parse);
                            strcpy(storageServers[server_count].Accessible_Paths[storageServers[server_count].count_paths], path_parse);
                            storageServers[server_count].count_paths++;
                            backup_path++;
                            // continue;
                        }

                        else if (strcmp(type_content, "FILE") == 0)
                        {

                            storageServers[server_count].file_dir[storageServers[server_count].count_paths] = 1;
                            strcpy(storageServers[server_count].Accessible_Paths[storageServers[server_count].count_paths], path_parse);
                            storageServers[server_count].count_paths++;
                            storageServers[server_count].is_backup[server_count] = 0;
                        }
                        else if (strcmp(type_content, "DIRECTORY") == 0)
                        {

                            storageServers[server_count].file_dir[storageServers[server_count].count_paths] = 2;
                            strcpy(storageServers[server_count].Accessible_Paths[storageServers[server_count].count_paths], path_parse);
                            storageServers[server_count].count_paths++;
                            storageServers[server_count].is_backup[server_count] = 0;
                        }
                        // strcpy(storageServers[server_count].Accessible_Paths[storageServers[server_count].count_paths], path_parse);
                        // storageServers[server_count].count_paths++;
                        // storageServers[server_count].is_backup[server_count] = 0; // wrong index
                        // storageServers[server_count].is_backup[server_count] = 0;
                        printf("Registered path: %s\n", path_parse);
                        // if(server_count >= 2 ){          // add data to 2 ss
                        //     strcpy(storageServers[server_count-1].Accessible_Paths[storageServers[server_count-1].count_paths], buffer);
                        //     storageServers[server_count-1].count_paths++;
                        //     storageServers[server_count-1].is_backup[server_count-1] = 1;
                        //     strcpy(storageServers[server_count-2].Accessible_Paths[storageServers[server_count-2].count_paths], buffer);
                        //     storageServers[server_count-2].count_paths++;
                        //     storageServers[server_count-2].is_backup[server_count-2] = 2;
                        // }
                        // Send acknowledgment for valid path
                        if (!root)
                            root = createNode();
                        insertPathToTrie(path_parse, server_count, storageServers[server_count].count_paths);
                        send(new_socket, "Path valid.\n", 12, 0);
                    }
                    else if (path_exists_in_servers(path_parse))
                    {
                        send(new_socket, "Path Already exists in one of the servers.\n", 44, 0);
                    }
                    else
                    {
                        printf("Skipping invalid path: %s\n", path_parse);
                        send(new_socket, "Path invalid.\n", 14, 0);
                    }
                }
                storageServers[server_count].last_heartbeat = time(NULL);
                server_count++;
                sleep(2);
                if (server_count >= 3)
                {
                    add_to_backup();
                }
            }
            else if (strncmp(buffer, "Asynchrnous Write Started", 25) == 0)
            {
                printf("Asynchronous Write Started\n");
                async_write_active = 1;
            }
            else
            {
                printf("Asynchronous Write Completed:)\n");
                int port;
                char ip[INET_ADDRSTRLEN];
                char path_async[BUFFER_SIZE];
                sscanf(buffer, "Asynchronous write completed: %s %d %s", ip, &port, path_async);
                int index = get_server_index_by_ip(ip, port);
                for (int i = 0; i < storageServers[index].count_paths; i++)
                {
                    if (strcmp(path_async, storageServers[index].Accessible_Paths[i]) == 0)
                    {
                        path_async_number = i;
                    }
                }
                global_flag = index;
                async_write_active = 0;
            }
        }
        // close(new_socket);
    }
    return NULL;
}
void initializeNM()
{
    char dynamicIP[INET_ADDRSTRLEN];
    getIPAddress(dynamicIP);
    int new_socket;
    struct sockaddr_in c_address;
    socklen_t c_addrlen = sizeof(c_address);
    if ((nm_client_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(nm_client_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        close(nm_client_socket);
        exit(0);
    }

    c_address.sin_family = AF_INET;
    c_address.sin_addr.s_addr = INADDR_ANY;
    c_address.sin_port = htons(PORT);

    if (bind(nm_client_socket, (struct sockaddr *)&c_address, sizeof(c_address)) < 0)
    {
        perror("Client socket Bind failed");
        exit(1);
    }

    printf("Naming Server initialized with IP: %s, Port: %d for clients \n", dynamicIP, PORT);
    snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: NM initialized with IP: %s, Port: %d for clients\n", dynamicIP, PORT);

    if (listen(nm_client_socket, MAX_SS) < 0)
    {
        perror("Listen failed");
        exit(1);
    }
    /*---------------------------------------------------------------------------------------------------*/
    char dynamicIP_SS[INET_ADDRSTRLEN];
    getIPAddress(dynamicIP_SS);
    struct sockaddr_in s_address;
    socklen_t s_addrlen = sizeof(s_address);
    if ((nm_ss_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(1);
    }

    if (setsockopt(nm_ss_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error in reusing the port");
        close(nm_client_socket);
        exit(0);
    }

    s_address.sin_family = AF_INET;
    s_address.sin_addr.s_addr = INADDR_ANY;
    s_address.sin_port = htons(PORT_SS);

    if (bind(nm_ss_socket, (struct sockaddr *)&s_address, sizeof(s_address)) < 0)
    {
        perror("Bind failed");
        exit(1);
    }

    printf("Naming Server initialized with IP: %s, Port: %d for storage servers\n\n", dynamicIP_SS, PORT_SS);
    snprintf(log_ack[log_size++], MAX_PATH_LENGTH, "NM: NM initialized with IP: %s, Port: %d for SS\n", dynamicIP, PORT_SS);

    if (listen(nm_ss_socket, MAX_SS) < 0)
    {
        perror("Listen failed");
        exit(1);
    }
    pthread_t ss;
    if (pthread_create(&ss, NULL, SS_reg, &s_address) != 0)
    {
        perror("Failed to create thread\n");
    }
}

// int path_exists_in_servers(const char *path)
// {
//     for (int i = 0; i < server_count; i++)
//     {
//         for (int j = 0; j < storageServers[i].count_paths; j++)
//         {
//             if (strcmp(path, storageServers[i].Accessible_Paths[j]) == 0)
//             {
//                 return 1; // Path exists in a storage server
//             }
//         }
//     }
//     return 0; // Path does not exist in any storage server
// }

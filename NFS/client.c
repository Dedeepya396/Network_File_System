#include "client.h"
int nm_client_port;
const char *nm_client_ip;
int sockfd;
int connect_to_nm()
{
    struct sockaddr_in server_address;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        perror("Failed to create socket");
        return -1;
    }
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(nm_client_port);
    if (inet_pton(AF_INET, nm_client_ip, &server_address.sin_addr) <= 0)
    {
        perror("Invalid IP address format");
        return -1;
    }
    printf("Connecting to naming server.....\n");
    if (connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        perror("Connection to Naming Server failed");
        close(sockfd);
        return -1;
    }
    printf("Connected to Naming Server at %s:%d\n", nm_client_ip, nm_client_port);
    return 0;
}

void send_to_SS(const char *ss_ip, int port, char request[])
{
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
    server_addr.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection to the SS failed");
        // close(sock);
        // exit(EXIT_FAILURE);
    }
    printf("Connected to SS at %s:%d\n", ss_ip, port);
    if (strncmp(request, "WRITE", 5) == 0)
    {
        char path[MAX_LEN];
        int fd;
        int ret = sscanf(request, "WRITE %d %s", &fd, path);
        if (fd == 1)
        {
            char new_request[BUFFER_SIZE + 7];
            snprintf(new_request, sizeof(new_request), "WRITE1 %s", request);
            if (send(sock, new_request, strlen(new_request), 0) < 0)
            {
                perror("Failed to send message to SS");
            }
            else
            {
                printf("Request sent to SS: %s\n\n", new_request);
            }
        }
        else
        {
            char new_request[BUFFER_SIZE + 7]; // Ensure BUFFER_SIZE is large enough
            snprintf(new_request, sizeof(new_request), "WRITE2 %s", request);
            if (send(sock, new_request, strlen(new_request), 0) < 0)
            {
                perror("Failed to send message to SS");
            }
            else
            {
                printf("Request sent to SS: %s\n\n", new_request);
            }
        }
    }
    else
    {
        if (send(sock, request, strlen(request), 0) < 0)
        {
            perror("Failed to send message to SS");
        }
        else
        {
            printf("Request sent to SS: %s\n\n", request);
        }
    }
    char buffer[BUFFER_SIZE];
    if (strncmp(request, "STREAM", 6) != 0)
    {
        int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (data_read > 0)
        {
            buffer[data_read] = '\0';
            printf("%s\n", buffer);
            if (strcmp(buffer, "Wait for some time :(") == 0)
            {
                return;
            }
            else if(strncmp(buffer,"Unable",6)==0)
            {
                return;
            }
            else if (strncmp(buffer, "Error", 5) == 0)
            {
                int error_code;
                if (sscanf(buffer, "Error Code %d", &error_code) == 1)
                {
                    switch (error_code)
                        {
                        case ERR_FILE_NOT_FOUND:
                            printf("Error: File not found on the server.\n");
                            break;
                        case ERR_DIR_NOT_FOUND:
                            printf("Error: Folder not found on the server.\n");
                            break;
                        case ERR_PATH_NOT_FOUND:
                            printf("Error: Path not found on the server.\n");
                            break;
                        case ERR_SRC_FILE_NOT_FOUND:
                            printf("Error:Source File not found on the server.\n");
                            break;
                        case ERR_SRC_DIR_NOT_FOUND:
                            printf("Error:Source Folder not found on the server.\n");
                            break;
                        case ERR_DEST_DIR_NOT_FOUND:
                            printf("Error:Destination Folder not found on the server.\n");
                            break;
                        case ERR_NOT_A_FILE:
                            printf("Error:Its not a File.\n");
                            break;
                        default:
                            printf("Error: Unknown error code received (%d).\n", error_code);
                            break;
                        }
                }
            }
            // else if (strncmp(request, "WRITE", 5) != 0)
            // {
            //     data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
            //     if (data_read > 0)
            //     {
            //         buffer[data_read] = '\0';
            //         printf("FROM SS: %s\n", buffer);
            //     }
            //     else
            //     {
            //         printf("No response from storage server\n");
            //     }
            // }
        }
        else
        {
            printf("No response from storage server\n");
        }
    }
    if (strncmp(request, "WRITE", 5) == 0)
    {
        write_data(sock);
        int data_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (data_read > 0)
        {
            buffer[data_read] = '\0';
            printf("FROM SS: %s\n", buffer);
            send(sockfd,buffer,strlen(buffer),0);
        }
        else
        {
            printf("No response from storage server\n");
        }
    }
    if (strncmp(request, "STREAM", 6) == 0)
    {
        char path[MAX_LEN];
        if (recv_audio(sock) != -1)
            printf("Audio recv and played successfully :)\n");
    }
}

void send_to_nm(char request[])
{
    printf("From Naming Server\n\n");
    if (send(sockfd, request, strlen(request), 0) < 0)
    {
        perror("Send error\n");
        return;
    }
    else
    {
        if (strncmp(request, "LIST", 4) != 0 && strcmp(request, "LOG") != 0)
        {
            printf("File request sent to nm\n\n");

            char response[MAX_LEN];
            int bytes_received = recv(sockfd, response, MAX_LEN - 1, 0);
            response[bytes_received] = '\0';
            if (strncmp(response, "Asynchronous Write Completed:)", 30) == 0)
            {
                printf("Asynchronous Write Completed:)\n");
                bytes_received = recv(sockfd, response, MAX_LEN - 1, 0);
            }
            if (strncmp(response, "Asynchronous Write Stopped as SS went offline:(", 47) == 0)
            {
                printf("Asynchronous Write Stopped as SS went offline:(\n");
                bytes_received = recv(sockfd, response, MAX_LEN - 1, 0);
            }
            if (bytes_received > 0)
            {
                response[bytes_received] = '\0';
                printf("Response from naming server: %s\n", response);
                if (strcmp(response, "SS is offline cannot perform operation:(\n") == 0)
                    return;
                if (strncmp(response, "ACK", 3) == 0)
                {
                    printf("Acknowledgement Received\n");
                }
                else if (strcmp(response,"Wait for some time :(" ) == 0)
                {
                    return;
                }
                else if (strncmp(response, "Error", 5) == 0)
                {
                    int error_code;
                    if (sscanf(response, "Error Code %d", &error_code) == 1) // Extract the error code
                    {
                        switch (error_code)
                        {
                        case ERR_FILE_NOT_FOUND:
                            printf("Error: File not found on the server.\n");
                            break;
                        case ERR_DIR_NOT_FOUND:
                            printf("Error: Folder not found on the server.\n");
                            break;
                        case ERR_PATH_NOT_FOUND:
                            printf("Error: Path not found on the server.\n");
                            break;
                        case ERR_SRC_FILE_NOT_FOUND:
                            printf("Error:Source File not found on the server.\n");
                            break;
                        case ERR_SRC_DIR_NOT_FOUND:
                            printf("Error:Source Folder not found on the server.\n");
                            break;
                        case ERR_DEST_DIR_NOT_FOUND:
                            printf("Error:Destination Folder not found on the server.\n");
                            break;
                        case ERR_NOT_A_FILE:
                            printf("Error:Its not a File.\n");
                            break;
                        default:
                            printf("Error: Unknown error code received (%d).\n", error_code);
                            break;
                        }
                    }
                }
                else if (strcmp(response, "Not found") != 0)
                {
                    char ss_ip[INET_ADDRSTRLEN];
                    int port;
                    char path_from_nm[MAX_LEN+5];
                    sscanf(response, "%s %d %s", ss_ip, &port,path_from_nm);
                    printf("Received storage server ip and port:%s %d\n\n", ss_ip, port);
                    if(strncmp(request,"READ",4) == 0) 
                    {
                        for(int i=0;i<strlen(request);i++) {
                            request[i] = 0;
                        }
                        snprintf(request,BUFFER_SIZE+1010,"READ %s",path_from_nm);
                        printf("paths %s\n",path_from_nm);
                        printf("Sent to SS function %s\n",request);
                    }
                    send_to_SS(ss_ip, port, request);
                }
                else
                {
                    printf("Path not found Try again:(\n\n");
                }
            }
            else
            {
                printf("Failed to receive response or connection closed\n");
            }
        }
    }
}
void handle_read()
{
    printf("Enter the path of req file: ");
    char path[MAX_LEN];
    fgets(path, MAX_LEN, stdin);
    path[strcspn(path, "\n")] = '\0';
    char request[MAX_LEN + 5];
    snprintf(request, MAX_LEN + 5, "READ %s", path);
    send_to_nm(request);
}
void handle_write()
{
    printf("Enter 1 to Asynchronous Data Writing and 2 for Synchronous Data Writing:");
    int fd;
    scanf("%d", &fd);
    while (getchar() != '\n')
        ;
    if (fd == 1 || fd == 2)
    {
        printf("Enter the path of req file: ");
        char path[MAX_LEN];
        fgets(path, MAX_LEN, stdin);
        path[strcspn(path, "\n")] = '\0';
        char request[MAX_LEN + 10];
        snprintf(request, 2 * MAX_LEN + 10, "WRITE %d %s", fd, path);
        send_to_nm(request);
    }
    else
    {
        printf("Please enter a valid number\n");
    }
}
void handle_info()
{
    printf("Enter the path of req file: ");
    char path[MAX_LEN];
    fgets(path, MAX_LEN, stdin);
    path[strcspn(path, "\n")] = '\0';
    char request[MAX_LEN + 5];
    snprintf(request, MAX_LEN + 5, "INFO %s", path);
    send_to_nm(request);
}
void handle_list()
{
    char request[5];
    snprintf(request, 5, "LIST");
    send_to_nm(request);
    char buffer[BUFFER_SIZE];

    int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (strncmp(buffer, "Asynchronous Write Completed:)", 30) == 0)
    {
        printf("Asynchronous Write Completed:)\n");
        bytes_received = recv(sockfd, buffer, MAX_LEN - 1, 0);
    }
    if (strncmp(buffer, "Asynchronous Write Stopped as SS went offline:(", 47) == 0)
    {
        printf("Asynchronous Write Stopped as SS went offline:(\n");
        bytes_received = recv(sockfd, buffer, MAX_LEN - 1, 0);
    }
    if (bytes_received < 0)
    {
        perror("Error receiving data from server");
        return;
    }
    buffer[bytes_received] = '\0';
    char *path = strtok(buffer, "\n");
    while (path != NULL)
    {
        printf("Received path: %s\n", path);
        path = strtok(NULL, "\n");
    }
}
void handle_audio()
{
    printf("Enter path to mp3 file: ");
    char path[MAX_LEN];
    fgets(path, MAX_LEN, stdin);
    path[strcspn(path, "\n")] = '\0';
    if(strstr(path,".mp3")==NULL)
    {
        printf("Error Code: %d",ERR_NOT_AUDIO_FILE);
        printf("Not AN audio file\n");
    }
    char request[MAX_LEN + 8];
    snprintf(request, MAX_LEN + 8, "STREAM %s", path);
    send_to_nm(request);
}
void handle_create()
{
    printf("Enter 1 to Create a File and 2 to Create a Folder: ");
    int fd;
    scanf("%d", &fd);
    while (getchar() != '\n')
        ;
    if (fd == 1)
    {
        printf("Enter the path where you want to Create a File: ");
        char path[MAX_LEN];
        fgets(path, MAX_LEN, stdin);
        path[strcspn(path, "\n")] = '\0';
        printf("Enter the Name of the File you want to create: ");
        char filename[MAX_LEN];
        fgets(filename, MAX_LEN, stdin);
        filename[strcspn(filename, "\n")] = '\0';
        char request[(2 * MAX_LEN) + 11];
        snprintf(request, (2 * MAX_LEN) + 11, "CREATE %d %s %s", fd, path, filename);
        send_to_nm(request);
    }
    else if (fd == 2)
    {
        printf("Enter the path where you want to Create a Folder: ");
        char path[MAX_LEN];
        fgets(path, MAX_LEN, stdin);
        path[strcspn(path, "\n")] = '\0';
        printf("Enter the Name of the Folder you want to Create: ");
        char filename[MAX_LEN];
        fgets(filename, MAX_LEN, stdin);
        filename[strcspn(filename, "\n")] = '\0';
        char request[(2 * MAX_LEN) + 11];
        snprintf(request, (2 * MAX_LEN) + 11, "CREATE %d %s %s", fd, path, filename);
        send_to_nm(request);
    }
    else
    {
        printf("Please Enter Valid Input(1 for File 2 for Folder)\n");
    }
}
void handle_delete()
{
    printf("Enter 1 to Delete a file and 2 to Delete a directory: ");
    int fd;
    scanf("%d", &fd);
    while (getchar() != '\n')
        ;
    if (fd == 1)
    {
        printf("Enter the path of the file you waant to Delete: ");
        char path[MAX_LEN];
        fgets(path, MAX_LEN, stdin);
        path[strcspn(path, "\n")] = '\0';
        char request[MAX_LEN + 13];
        snprintf(request, MAX_LEN + 13, "DELETEFILE %s", path);
        send_to_nm(request);
    }
    else if (fd == 2)
    {
        printf("Enter the path of the folder you waant to Delete: ");
        char path[MAX_LEN];
        fgets(path, MAX_LEN, stdin);
        path[strcspn(path, "\n")] = '\0';
        char request[MAX_LEN + 15];
        snprintf(request, MAX_LEN + 15, "DELETEFOLDER %s", path);
        send_to_nm(request);
    }
    else
    {
        printf("Please enter valid input(1 for File 2 for Directory)\n");
    }
}
void handle_copy()
{
    printf("Enter 1 to Copy a file and 2 to Copy a directory: ");
    int fd;
    scanf("%d", &fd);
    while (getchar() != '\n')
        ;
    if (fd == 1)
    {
        printf("Enter the path of Source File: ");
        char src_path[MAX_LEN];
        fgets(src_path, MAX_LEN, stdin);
        src_path[strcspn(src_path, "\n")] = '\0';
        printf("Enter the path of Destination File: ");
        char dest_path[MAX_LEN];
        fgets(dest_path, MAX_LEN, stdin);
        dest_path[strcspn(dest_path, "\n")] = '\0';
        char request[(2 * MAX_LEN) + 13];
        snprintf(request, (2 * MAX_LEN) + 13, "COPY %d %s %s", fd, src_path, dest_path);
        printf("%s\n", request);
        send_to_nm(request);
    }
    else if (fd == 2)
    {
        printf("Enter the path of Source Folder: ");
        char src_path[MAX_LEN];
        fgets(src_path, MAX_LEN, stdin);
        src_path[strcspn(src_path, "\n")] = '\0';
        printf("Enter the path of Destination File: ");
        char dest_path[MAX_LEN];
        fgets(dest_path, MAX_LEN, stdin);
        dest_path[strcspn(dest_path, "\n")] = '\0';
        char request[(2 * MAX_LEN) + 13];
        snprintf(request, (2 * MAX_LEN) + 13, "COPY %d %s %s", fd, src_path, dest_path);
        printf("%s\n", request);
        send_to_nm(request);
    }
    else
    {
        printf("Please enter valid input(1 for File 2 for Directory)\n");
    }
}
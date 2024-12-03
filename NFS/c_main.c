#include "client.h"
void write_data(int ss_sock)
{
    char data[BUFFER_SIZE];
    printf("Enter data and enter STOP once you are done\n\n");
    while (1)
    {
        printf("Enter data to write to req file (STOP to end): ");
        fgets(data, MAX_LEN, stdin);
        send(ss_sock, data, strlen(data), 0);
        if (strcmp(data, "STOP\n") == 0)
            break;
    }
}

int main(int argv, char *argc[])
{
    if (argv != 3)
    {
        printf("Usage : ./a.out <nm_client ip> <nm_client port>\n");
        return 0;
    }

    nm_client_ip = argc[1];
    nm_client_port = atoi(argc[2]);

    char operation[MAX_LEN];
    int connected = 0;
    while (1)
    {
        if (!connected)
        {
            connect_to_nm();
            connected = 1;
        }
        printf("Enter operation: ");
        fgets(operation, MAX_LEN, stdin);
        operation[strcspn(operation, "\n")] = '\0';
        if (strcmp(operation, "READ") == 0)
        {
            handle_read();
        }
        else if (strcmp(operation, "WRITE") == 0)
        {
            handle_write();
        }
        else if (strcmp(operation, "INFO") == 0)
        {
            handle_info();
        }
        else if (strcmp(operation, "LIST") == 0)
        {
            handle_list();
        }
        else if (strcmp(operation, "STREAM") == 0)
        {
            handle_audio();
        }
        else if (strcmp(operation, "CREATE") == 0)
        {
            handle_create();
        }
        else if (strcmp(operation, "DELETE") == 0)
        {
            handle_delete();
        }
        else if (strcmp(operation, "COPY") == 0)
        {
            handle_copy();
        }
        else if(strcmp(operation,"LOG")==0){
            send_to_nm("LOG");
        }
    }
}
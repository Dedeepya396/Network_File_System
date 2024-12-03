#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include "stream.h"
#include "error_codes.h"
#define MAX_LEN 1000
#define BUFFER_SIZE 8192
extern int nm_client_port;
extern const char *nm_client_ip;
extern int sockfd;
void write_data(int ss_sock);
int connect_to_nm();
void send_to_SS(const char *ss_ip, int port, char request[]);
void send_to_nm(char request[]);
void handle_read();
void handle_write();
void handle_info();
void handle_list();
void handle_audio();
void handle_create();
void handle_delete();
void handle_copy();
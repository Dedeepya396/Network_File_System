#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include<dirent.h>
#include <sys/stat.h>
#include <ifaddrs.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#define MAX_PATH_LENGTH 100000
#define MAX_LOGS 150
#define MAX_SS 100
#define PORT 8080
#define PORT_SS 8100
#define MAX_PATHS 100
#define MAX_LENGTH_PATH 1024
#define MAX_LEN 200
#define BUFFER_SIZE 8192
#define MAX_FILES 100
#define MAX_CACHE_LEN 4
#define HEARTBEATTIME 5
#define TIMEOUT 5
extern int nm_client_socket;
extern int nm_ss_socket;
extern char log_ack[MAX_LOGS][MAX_PATH_LENGTH];
extern int log_size;


struct serverinfo
{
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    char Accessible_Paths[MAX_PATHS][MAX_LENGTH_PATH];
    int count_paths;
    int ss_online;
    time_t last_heartbeat;
    int socket_fd;
    char backup_1_ip[INET_ADDRSTRLEN];
    char backup_2_ip[INET_ADDRSTRLEN];
    int backup_1_port;
    int backup_2_port;
    int is_backup[MAX_PATHS];
    int is_active;
    char backup[MAX_LENGTH_PATH];
    int backup_clientport_1;
    int backup_clientport_2;
    int file_dir[MAX_PATHS];
    int no_backup_ss;
    int first_bk_ss;
    int second_bk_ss;
};
typedef struct serverinfo ServerInfo;
struct cacheNode
{
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    char path[MAX_LENGTH_PATH];
};

typedef struct cacheNode CacheNode;

extern CacheNode LRU_cache[MAX_CACHE_LEN];
extern ServerInfo storageServers[MAX_SS];
extern int server_count;
extern int LRU_size;

typedef struct {
    char path[BUFFER_SIZE]; 
    char *data;             
    size_t size;            
} FileData;

typedef struct {
    FileData files[MAX_FILES]; 
    int count;                 
} FileSystem;


typedef struct TrieNode
{
    struct TrieNode *children[256]; // ASCII character set
    int is_end_of_path;
    int server_index;
    int path_index;
} TrieNode;

extern TrieNode *root;

extern ServerInfo storageServers[MAX_SS];
extern int server_count;

void getIPAddress(char *ipBuffer);
int isValidPathFormat(const char *path);
void *SS_reg(void *arg);
void initializeNM();
int get_ss_details(char path[], char result[]);
void handle_list(int client_socket);
void *handle_client(void *arg);
int searchInCache(char path[], char result[]);
void AfterHit(int a);
void addPathCache(char path[], char ip[], int port);
int path_exists_in_servers(const char *path);
void insertPathToTrie(const char *path, int server_index, int path_index);
TrieNode *createNode();
bool removePath(TrieNode *node, const char *path, int depth);
void *check_timeouts_thread(void *args);
void send_to_SS(const char *ss_ip, int port, char request[], int client_socket);
void send_to_SS_dup(const char *ss_ip, int port, char request[], int client_socket);
void last_slash(char* dest_path_backup, char* path);
int get_server_index_by_ip(char* ip_here , int port_here);
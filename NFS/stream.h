#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include "log.h"

#define BUFF_SIZE 4096

int init_audio_system();
int send_audio(char file_path[], int client_sock);
void cleanup_audio_system();
int play_audio(const char *file_path);
int recv_audio(int sock);

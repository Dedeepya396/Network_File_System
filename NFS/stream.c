#include "stream.h"

int init_audio_system()
{
    // if (SDL_Init(SDL_INIT_AUDIO) < 0)
    // {
    //     fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    //     return -1;
    // }

    // if (Mix_Init(MIX_INIT_MP3) == 0)
    // {
    //     fprintf(stderr, "Mix_Init Error: %s\n", Mix_GetError());
    //     SDL_Quit();
    //     return -1;
    // }

    // if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0)
    // {
    //     fprintf(stderr, "Mix_OpenAudio Error: %s\n", Mix_GetError());
    //     Mix_Quit();
    //     SDL_Quit();
    //     return -1;
    // }
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return -1;
    }

    if (Mix_Init(MIX_INIT_MP3) == 0)
    {
        printf("Mix_Init error: %s\n", Mix_GetError());
        SDL_Quit();
        return -1;
    }

    if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) < 0)
    {
        printf("Mix_OpenAudio error: %s\n", Mix_GetError());
        Mix_Quit();
        SDL_Quit();
        return -1;
    }
    return 0;
}

int send_audio(char file_path[], int client_sock)
{
    FILE *audio_file;
    char buffer[BUFF_SIZE];
    audio_file = fopen(file_path, "rb");
    if (!audio_file)
    {
        perror("File not found");
        close(client_sock);
        return 0;
    }
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFF_SIZE, audio_file)) > 0)
    {
        send(client_sock, buffer, bytes_read, 0);
    }
    fclose(audio_file);
    return 1;
}

void cleanup_audio_system()
{
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
}

int play_audio(const char *file_path)
{
    if (init_audio_system() < 0)
    {
        return -1;
    }
    printf("\nPath of audio file to be played: %s\n", file_path);
    Mix_Music *music = Mix_LoadMUS(file_path);
    if (!music)
    {
        fprintf(stderr, "Mix_LoadMUS Error: %s\n", Mix_GetError());
        return -1;
    }

    if (Mix_PlayMusic(music, 1) == -1)
    {
        fprintf(stderr, "Mix_PlayMusic Error: %s\n", Mix_GetError());
        Mix_FreeMusic(music);
        return -1;
    }

    while (Mix_PlayingMusic())
    {
        SDL_Delay(100); // Wait while music is playing
    }
    Mix_FreeMusic(music);
    return 0;
}

int recv_audio(int sock)
{
    FILE *audio_file = fopen("received_audio.mp3", "wb");
    if (!audio_file)
    {
        perror("Failed to open file to save audio");
        close(sock);
        cleanup_audio_system();
        return -1;
    }
    char buffer[BUFF_SIZE];
    int bytes_received;

    while ((bytes_received = recv(sock, buffer, BUFF_SIZE, 0)) > 0)
    {
        fwrite(buffer, 1, bytes_received, audio_file);
    }
    fclose(audio_file);
    printf("Audio received successfully:)\n");
    char path[BUFF_SIZE];
    getcwd(path,150);
    strcat(path,"/received_audio.mp3");
    if (play_audio("received_audio.mp3") < 0)
    {
        close(sock);
        cleanup_audio_system();
        return -1;
    }
    close(sock);
    cleanup_audio_system();
    return 1;
}

## STORAGE SERVERS

1) The accessible paths are given in input instead of command line input.

2) The command line input is of form "./a.out <naming_server_ip> <naming_server_port> <storage_server_ip> <storage_server_port_nm> <storage_server_port_client>"

3) In storage server until all the accessible paths are given and enter finish another storage server doesn't get registered because if the accessible paths are given 
   in command line input the sam happens.

4) Storage server uses one port for communicating with naming server and different port for communicating with clients.

5) Accessible paths of a storage server are getting stored in hashtables. So it is used to check whether the a particular path is present in it or not. So if the path 
   already exists then the path again doesn't get added to hashtables. So that duplicate paths are not allowed.

6) For every accessible path that are given we receive an acknowledgement from the naming server whether the path is already exists in same or some other storage servers.

7) In "send_data_to_nm" function sends the accessible path along with whether it is a file or directory to naming server and recieves the acknowledgement from the naming 
   server about it's validity.

8) In "registration_with_nm" , the storage servers get registers with naming server and asks for the input of file that is used to store the backup files of another 
   storage servers and then asks for the input of accesible paths and after completing the accessible path inputs give finish. It will check whether the given path is file/folder, if it is a folder it goes to list_directory_recursive function and adds all paths after getting acknowledgement from the nm that it is a valid path. Similar thing for file also. Sends END_PATHS when user gives finish, to stop the connection with nm. 

9) The handling of the storage server with the client happens in the way :
    The request sent by client is received by storage server and tokenize it into command and path and based on the operation given by the client it gets handled.

10) For reading a file it first checks whether the file is getting written by some client, if one of the client writes the file then it sends a message to the client that 
    write is happening so wait for some time to read the file. This is done by using semaphores.

11) For writing to the file, it first checks that whether a client is writing to that file,if yes then it sends a message to the client that other client is writing the data 
    to the file so wait for some time.

12) Similarly for deleting,copying the file it checks initially whether the file is getting written, if yes then client has to wait until write has been completed to 
    perform delete or copying the file.

13) For writing the data to the file we take the input data to be written until the client gives STOP.

14) When client is given the path of the folder to read or write then an error gets printed since the folder cannot be readable and writable.

15) When ever the client is connected with naming server a seperate thread is created for each client.

16) (nm_hadling) 
For creating a file we ask the user if they want ot create a file or folder and based on that we proceed to creating file or directory at the path provided.The path provided must be a folder.
For Deleting a file we again ask the user if they want to delete a file or directory and delete the file or directory a the path given.
For the copy function the empty files are not being copied.The COPY asks the user if they want to copy file or directory and destination must definitely be a folder and the copy can be between same storage servers or different storage servers.

17) Failure of storage server -> For the failure detection of the storage server, as soon as storage server gets registered by giving the accessible paths,it starts sending 
    the message to the naming server saying that I am online and the naming server receives the message and sends the acknowledgement to the stoarge server. This happens continuously and when the storage server
     is in offline then the storage server is unable to send message so the naming server detects that the storage server is offline.

18) For streaming the audio file we are using the libraries "<SDL2/SDL.h>, <SDL2/SDL_mixer.h>"

19) In "init_audio_system" 
    -> Initializes SDL's audio subsystem using SDL_Init(SDL_INIT_AUDIO)
    -> Sets up the SDL_mixer library to handle MP3 format with Mix_Init(MIX_INIT_MP3).
    -> Configures audio output using Mix_OpenAudio().
    -> Returns 0 if successful; otherwise, prints errors and cleans up resources.
20) In "sending_audio" function, the audio file is opened in read binary mode and reads the file and sends the file to the client.

21) In "recv_audio" function, It creates a new file received_audio.mp3 to save the incoming audio_file. and receives the audio data in chunks and writes the received data 
    to the file in write binary mode.

22) The audio gets played after the file has been transfered completely.

23) In "play_audio" function 
    -> Loads the audio file into memory using Mix_LoadMUS() (specific to music playback).
    -> Plays the audio file using Mix_PlayMusic(), looping once.
    -> Waits while the music plays, checking with Mix_PlayingMusic() and delaying the loop with SDL_Delay(100).
    -> Frees the music resource with Mix_FreeMusic() after playback completes.

24) For streaming the audio file, only MP3 files will get handled because of the libraries used.

#### INPUT FORMART

1) For naming server : gcc <naming_server.c> <nm_main.c> <globals.c>&& ./a.out
2) For storage server : gcc <new_ss.c> <stream.c> < -lSDL2 -lSDL2_mixer> && ./a.out <naming_server_ip> <naming_server_ss_port> 
                        <storage_server_ip> <storage_server_nm_port> <storage_server_client_port>
3) For client : gcc <client.c> <c_main.c> <stream.c> && ./a.out <naming_server_ip> <naming_server_client_port>



# CLIENT 

connect_to_nm creates a client socket which connects to naming server to send a request.
send_to_nm sends the request to naming server and processes response of naming server.
send_to_SS when client receives the ip and port of corresponding storage server it sends the request to that ss and waits for the response from storage server.
handle_X where X is the request from client processes the request and sends it to naming server
write_data it takes the input from client which should be written to the file until client enters STOP

# NAMING SERVER

We have implemented tries for efficient search.insertPathToTrie inserts path into a trie. get_ss_details searches path in trie and returns ss_ip and ss_port if found else it returns NOT FOUND
removePath removes path from trie. 
For implementing LRU cache, we have a struct which stores path, ip, port number. We search for a path in searchInCache the cache if it is found it increases its priority in AfterHit function. If path is not found then we will insert into cache in addPathCache function.
check_for_ss checks whether the server that is trying to connect to naming server already exists or not
SS_reg accepts storage server's requests infinitely. 
initializeNM initializes naming server, we are creating two sockets one for communicating with storage servers and other one for communicating with clients.
delete_from_cache we are deleting the path that has to be deleted from cache.
send_to_SS we send the request to storage server

check_timeouts_thread: this will check for the communication b/w and nm ans ss for every 5s
backup_ss_details: it will add the details of backup servers if ss > 2
remove_formality: it removes the '/' from the given path
call_copy:	it will call suitable copy function based on it is a file or directory
add_to_backup:	this will add the src files to the backup folder given when we take the first input, we are considering that th first path is a folder and using it as backup for other ss details.

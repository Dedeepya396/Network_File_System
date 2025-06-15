# Network File System
Consists of three major components `Client` `Storage server` `Naming server`

## Table of Contents

1. [Introduction](#introduction)  
    • [Client](#client)  
    • [Storage Servers](#storage-servers)  
    • [Naming Server](#naming-server)  
2. [Storage Servers](#storage-servers-1)  
3. [Client](#client-1)  
4. [Naming Server](#naming-server-1)  
5. [Additional Functionalities](#additional-functionalities)  
6. [How to Run](#how-to-run)


# Introduction
- ## Client:
    Clients represent the systems or users requesting access to files within the network file system. 
    They serve as the primary interface to interact with the NFS, initiating various file-related operations such as reading, writing, deleting, streaming, and more.

- ## Storage servers:
    These servers are responsible for the physical storage and retrieval of files and folders. They manage data persistence and distribution across the network, ensuring that files are stored securely and efficiently.

- ## Naming servers:
    This serves as a central hub, orchestrating communication between clients and the storage servers. 

    Its primary function is to provide clients with crucial information about the specific storage server where a requested file or folder resides. Essentially, it acts as a directory service, ensuring efficient and accurate file access.

---


## STORAGE SERVERS

1) The accessible paths are given in input instead of command line input.

2) The command line input is of form 

    `./a.out <naming_server_ip> <naming_server_port> <storage_server_ip> <storage_server_port_nm> <storage_server_port_client>`

3) A new storage server cannot be registered until the present storage server finishes entering its accessible paths

4) For efficient searching and retrieval of paths hashtables are used. Duplicate paths (already existing) paths are not added;

5) Storage server registers with naming server by sending details like its port,ip and port for client connections to naming server 

6) Upon command from naming server, it can create new file/directory, delete file/directory, copy file/directory

7) Upon request from client, a storage server can read a file or write to a file, get information about size, permissions, stream an audio file

## CLIENT

1) A client sends a request of an operation on a path to naming server, which redirects to storage server consisting of that path

2) The operations that facilitates client are creating files/directories, deleting files/directories, copying files/directories, listing all accessible paths

## NAMING SERVER

1) This mediates the process between client and storage servers to ensure smooth and secure execution

2) Clients connect to naming server, naming server takes requests from clients, processes them, connects client with correct storage server.

3) After execution of current operation, naming server acknowledges the same to client

## Additional Functionalities
1) For efficient search we have implemented `LRU Cache` Of size 4 and ``Tries``
    
    We search for a path in Cache if it is not found in cache, we search in trie, if it is found there, then we add it to the cache by removing the least recently used path.

2) NFS allows asynchronous writing i.e., when client tries to write large data at once, we write the data persistently periodically, instead of taking a lot of time by writing whole data at one go. 
    
    Storage servers store the data in local memory and flush them to actual files periodically

3) Partial writes are also handled by NFS, where if a Storage server goes down during a write operation client is informed with that

4) To avoid loss of data, each storage server's data is backed up in previous two storage servers, in case of failure of a storage server, we use the backup in its backup storage server.

5) If a storage server comes back online, its state is preserved to avoid redundancy.

6) Storage servers/clients can connect infinitely.


---


# How to Run:

1) For naming server : 

    `gcc <naming_server.c> <nm_main.c> <globals.c>`

    `./a.out`

2) For storage server : 


    `gcc <new_ss.c> <stream.c> < -lSDL2 -lSDL2_mixer>`  
    
    `./a.out <naming_server_ip><naming_server_ss_port> 
                        <storage_server_ip> <storage_server_nm_port> <storage_server_client_port>` 
3) For client : 

    `gcc <client.c> <c_main.c> <stream.c>`

    `./a.out <naming_server_ip> <naming_server_client_port>`

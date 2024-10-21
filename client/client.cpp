/*///////////////////////////////////////////////////////////
*
* FILE:		client.c
* AUTHOR:	Your Name Here
* PROJECT:	CNT 4007 Project 1 - Professor Traynor
* DESCRIPTION:	Network Client Code
*
*////////////////////////////////////////////////////////////

/* Included libraries */

#include <stdio.h>		    /* for printf() and fprintf() */
#include <sys/socket.h>		    /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>		    /* for sockaddr_in and inet_addr() */
#include <stdlib.h>
#include <unistd.h>
#include <regex>
#include <string.h>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <unordered_map>
#include <openssl/evp.h>	    /* for OpenSSL EVP digest libraries/SHA256 */

using namespace std;

/* Constants */
#define RCVBUFSIZE 512		    /* The receive buffer size */
#define SNDBUFSIZE 512		    /* The send buffer size */
#define MDLEN 32

struct Song {
    char title[256];
    uint32_t uid;
};

struct SongMatch {
    vector<Song> both;
    vector<Song> client;
    vector<Song> server;
};

/* Fatal Error Handler */
void fatal_error(const char *error_message) {
  perror(error_message);
  exit(EXIT_FAILURE);
}

uint32_t getSongHash(string filepath) {
    ifstream song(filepath, ios::binary);
    if (!song.is_open()) {
        fatal_error("Failed to open song file when attempting to hash");
    }

    // Setup hash variables
    const uint32_t FNV_prime = 16777619U;
    uint32_t hash_string = 2166136261U;

    // Create a buffer to read file data into
    char buffer[4096];
    while (song.read(buffer, sizeof(buffer)) || song.gcount()) {
        for (streamsize i=0; i<song.gcount(); i++) {
            hash_string ^= static_cast<unsigned char>(buffer[i]);
            hash_string *= FNV_prime;
        }
    }

    return hash_string;
}

vector<Song> getLocalSongs() {
    set<uint32_t> uids;
    vector<Song> unique_songs;
    filesystem::path root_dir = filesystem::current_path();

    // Create song objects and insert them into a map
    for (const auto &song : filesystem::directory_iterator(root_dir)) {
        // Check if we have a file not a directory
        if (song.is_regular_file()) {

            // Not including Makefile as a song
            if(song.path().filename().string().compare("Makefile") == 0){
                continue;
            }

            // Get the song's path so we can hash it
            string song_path = song.path().string();
            uint32_t song_hash = getSongHash(song_path);

            Song song_entry;
            song_entry.uid = song_hash;
            memset(song_entry.title, 0, sizeof(song_entry.title));
            strncpy(song_entry.title, song.path().filename().string().c_str(), sizeof(song_entry.title));
            song_entry.title[song.path().filename().string().size()] = '\0';

            // Ensure song is unique
            if(uids.find(song_entry.uid) == uids.end()) {
                // Ensure song isn't a client file
                string title = song_entry.title;
                if (("client.cpp" != title) && ("client" != title)) {
                    uids.insert(song_entry.uid);
                    unique_songs.push_back(song_entry);
                }
            }
        }
    }

    return unique_songs;
}

vector<Song> serverList(int &clientSock) {
    vector<Song> recv_songs;

    // Read number of files on server
    int song_count;
    if ((recv(clientSock, &song_count, sizeof(song_count), 0)) < 0) {
        fatal_error("Client failed when getting number of titles in LIST");
    }

    // Begin reading song file entries from the server
    if (song_count <= 0) {
        // We found no files on the server
        return {};
    }
    else {

        // Files were found and begin reading
        for(int i=0; i<song_count; i++) {
            Song song;
            if ((recv(clientSock, &song, sizeof(song), 0)) < 0) {
                fatal_error("Client failed when getting receiving a song entry in LIST");
            }
            recv_songs.push_back(song);
        }

        return recv_songs;
    }
}

bool handleList(int &clientSock) {

    // Send command to server
    char command[] = "LIST";
    int msglen = strlen(command);
    if (send(clientSock, command, msglen, 0) != msglen) {
        fatal_error("Failed to send LIST command to server.");
    }

    vector<Song> songs = serverList(clientSock);

    if (songs.empty()) {
        "Server did not have any songs in its directory.\n";
    }
    else {
        cout << "\n";
        cout << left << setw(50) << "Song Title" <<  " | " << "UID\n";
        cout << string(70, '-') << "\n";

        for (int i=0; i<songs.size(); i++) {
            cout << left << setw(50) << songs[i].title << " | " << songs[i].uid << "\n";
        }

        cout << "\n";
    }

    return true;
}

SongMatch getMatchingSongs(vector<Song> local, vector<Song> server) {

    unordered_set<uint32_t> local_uids;
    unordered_set<uint32_t> server_uids;
    SongMatch matches;

    // Create sets for songs
    for (int i=0; i<local.size(); i++) {
        local_uids.insert(local[i].uid);
    }
    for (int i=0; i<server.size(); i++) {
        server_uids.insert(server[i].uid);
    }

    // See what matches what and what is only on the client
    for (Song song : local) {
        // Song found in both
        if(server_uids.find(song.uid) != server_uids.end()) {
            matches.both.push_back(song);
        }
        else {
            matches.client.push_back(song);
        }
    }
    
    // See what is only on the server
    for (Song song : server) {
        if(local_uids.find(song.uid) == local_uids.end()) {
            matches.server.push_back(song);
        }
    }

    return matches;
}

bool handleDiff(int &clientSock) {
    // Send command to server
    char command[] = "DIFF";
    int msglen = strlen(command);
    if (send(clientSock, command, msglen, 0) != msglen) {
        fatal_error("Failed to send LIST command to server.");
    }

    // Grab all local song objects
    vector<Song> local_song_vec = getLocalSongs();

    // Fetch songs objects from internet
    vector<Song> server_song_vec = serverList(clientSock);

    // Create object with all song matches
    SongMatch song_matches = getMatchingSongs(local_song_vec, server_song_vec);

    // Print match outputs
    cout << "\n";
    cout << left << setw(50) << "Song Title" <<  " | " << "UID\n";
    cout << string(70, '-') << "\n";
    cout << "| Synced Songs |\n";

    for (Song song : song_matches.both) {
        cout << left << setw(50) << song.title << " | " << song.uid << "\n";
    }

    cout << string(70, '-') << "\n";
    cout << "| Client only Songs |\n";

    for (Song song : song_matches.client) {
        cout << left << setw(50) << song.title << " | " << song.uid << "\n";
    }

    cout << string(70, '-') << "\n";
    cout << "| Server only Songs |\n";

    for (Song song : song_matches.server) {
        cout << left << setw(50) << song.title << " | " << song.uid << "\n";
    }

    cout << string(70, '-') << "\n";
    cout << "\n";

    return true;
}

bool handlePull(int &clientSock) {
    // Send command to server
    char command[] = "PULL";
    int msglen = strlen(command);
    if (send(clientSock, command, msglen, 0) != msglen) {
        fatal_error("Failed to send PULL command to server");
    }

    // Grab list of files on the server but not the client
    vector<Song> local_song_vec = getLocalSongs();
    vector<Song> server_song_vec = serverList(clientSock);
    SongMatch matched_songs = getMatchingSongs(local_song_vec, server_song_vec);

    cout << "Attempting to send number of missing songs...\n";

    // Tell server how many missing songs
    int needed_song_count = matched_songs.server.size();
    if (send(clientSock, &needed_song_count, sizeof(needed_song_count), 0) != sizeof(needed_song_count)) {
        fatal_error("Failed to send missing song count to server");
    }

    cout << "Number of missing songs = " << needed_song_count << "\n";

    // Begin sending uids of files and getting their contents back
    for (Song song : matched_songs.server) {
        // Send UID to server
        uint32_t song_uid = song.uid;
        if (send(clientSock, &song_uid, sizeof(song_uid), 0) != sizeof(song_uid)) {
            fatal_error("Failed to send a song UID to server");
        }

        // Receive file contents
        streamsize song_size;
        if (recv(clientSock, &song_size, sizeof(song_size), 0) <= 0) {
            fatal_error("Failed to receive the size of a file when pulling");
        }

        cout << "Song: " << song.title << " | " << song.uid << " is estimated to be " << song_size << " Bytes.\n";
        cout << "Attempting to download ...\n";

        // Generate file to download song into
        ofstream song_file(song.title, ios::binary);
        if(!song_file.is_open()) {
            fatal_error("Failed to create file to download song into");
        }

        // Begin downloading song
        char buffer[1024];
        streamsize bytes_downloaded = 0;
        while(bytes_downloaded < song_size) {
            // Download a chunk of the song
            ssize_t byte_chunk_size;
            if ((byte_chunk_size = recv(clientSock, buffer, sizeof(buffer), 0)) <= 0) {
                fatal_error("Failed to download some chunk of a song file");
                break;
            }

            // Write the song chunk to the file
            song_file.write(buffer, byte_chunk_size);
            bytes_downloaded = bytes_downloaded + byte_chunk_size;
            // cout << "bytes downloaed: " << bytes_downloaded << "\n";
        }

        cout << "Downloaded: " << bytes_downloaded << "\n";
        cout << "completed download\n";

        if(bytes_downloaded != song_size) {
            fatal_error("Song download is not the same as expected file size");
        }

        song_file.close();
    }

    return true;
}

bool handleLeave(int &clientSock) {
    // Send command to server
    char command[] = "LEAVE";
    int msglen = strlen(command);
    if (send(clientSock, command, msglen, 0) != msglen) {
        fatal_error("Failed to send LIST command to server.");
    }

    // Receive and print server's file list
    return false;
}

bool handleInvalidCommand(int &clientSock, string command) {
    // Send command to server

    int msglen = strlen(command.c_str());
    if (send(clientSock, command.c_str(), msglen, 0) != msglen) {
        fatal_error("Failed to send LIST command to server.");
    }


    return true;
}

bool handleConnection(int &clientSock) {
    string command;
    cout << "Choose a command (LIST, DIFF, PULL, LEAVE): ";
    std::getline(cin, command);

    if (command == "LIST") {
        return handleList(clientSock);

    } else if (command == "DIFF") {
        return handleDiff(clientSock);

    } else if (command == "PULL") {
        return handlePull(clientSock);

    } else if (command == "LEAVE") {
        return handleLeave(clientSock);

    } else {
        cout << "Invalid command ...\n";
        return handleInvalidCommand(clientSock, command);
    }
}

/* The main function */
int main(int argc, char *argv[])
{
    int clientSock;		    /* socket descriptor */
    struct sockaddr_in serv_addr;   /* The server address */

    char sndBuf[SNDBUFSIZE];	    /* Send Buffer */
    char rcvBuf[RCVBUFSIZE];	    /* Receive Buffer */


    memset(&sndBuf, 0, RCVBUFSIZE);
    memset(&rcvBuf, 0, RCVBUFSIZE);

    /* Create a new TCP socket*/
    if ((clientSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        fatal_error("connect() failed");
    }

    /* Construct the server address structure */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(3179);

    printf("Attempting to connect to server ...\n");

    /* Establish connecction to the server */
    if (connect(clientSock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        fatal_error("connect() failed");
    }

    printf("Established connection with server!\n");

    /* BEGIN NEW FUNCTIONALITIES */

    while(handleConnection(clientSock)) {
        // load
    }

    close(clientSock);

    return 0;
}


/*///////////////////////////////////////////////////////////
*
* FILE:		server.c
* AUTHOR:	Your Name Here
* PROJECT:	CNT 4007 Project 1 - Professor Traynor
* DESCRIPTION:	Network Server Code
*
*////////////////////////////////////////////////////////////

/*Included libraries*/

#include <stdio.h>	  /* for printf() and fprintf() */
#include <sys/socket.h>	  /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>	  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>	  /* supports all sorts of functionality */
#include <unistd.h>	  /* for close() */
#include <string.h>	  /* support any string ops */
#include <iostream>
#include <string>
#include <set>
#include <unordered_map>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <fstream>
#include <filesystem>
#include <openssl/sha.h>
#include <openssl/evp.h>  /* for OpenSSL EVP digest libraries/SHA256 */
#include <ctime>  // For timestamp
#include <sys/stat.h> // For mkdir

using namespace std;

#define RCVBUFSIZE 512		/* The receive buffer size */
#define SNDBUFSIZE 512		/* The send buffer size */
#define BUFSIZE 40		/* Your name can be as many as 40 chars */
#define MAXPENDING 10 /* Max number of incoming connections */

mutex console_mutex;
mutex list_mutex;
mutex diff_mutex;
mutex pull_mutex;

struct Song {
    char title[256];
    uint32_t uid;
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

map<uint32_t, Song> getLocalSongs(int &clnt_sock) {
    map<uint32_t, Song> unique_songs;
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
            if(unique_songs.find(song_entry.uid) == unique_songs.end()) {
                // Ensure song isn't a server file
                string title = song_entry.title;
                if (("server.cpp" != title) && ("server" != title)) {
                    unique_songs.insert({song_entry.uid, song_entry});
                    
                }
            }
        }
    }

    return unique_songs;
}

map<uint32_t, Song> generateList(int &clnt_sock) {
    map<uint32_t, Song> unique_songs;
    filesystem::path root_dir = filesystem::current_path();

    // Create song objects and insert them into a map
    for (const auto &song : filesystem::directory_iterator(root_dir)) {
        // Check if we have a file not a directory
        if (song.is_regular_file()) {

            // not including Makefile as a song
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
            if(unique_songs.find(song_entry.uid) == unique_songs.end()) {
                // Ensure song isn't a server file
                string title = song_entry.title;
                if (("server.cpp" != title) && ("server" != title)) {
                    unique_songs.insert({song_entry.uid, song_entry});
                    
                }
            }
        }
    }

    // Send song entries to client

    // Tell client how many entries to expect
    int song_count = unique_songs.size();
    if (send(clnt_sock, &song_count, sizeof(song_count), 0) != sizeof(song_count)) {
        fatal_error("Failed to send a song title length to client in LIST");
    }

    // Send all unique entires
    for (auto it = unique_songs.begin(); it != unique_songs.end(); it++) {
        Song entry = it->second;
        if (send(clnt_sock, &entry, sizeof(entry), 0) != sizeof(entry)) {
            fatal_error("Failed to send a song title length to client in LIST");
        }
    }

    return unique_songs;
}

bool handleDiff(int &clnt_sock) {
    list_mutex.lock();

    // Send a list of local server files to the client
    generateList(clnt_sock);

    list_mutex.unlock();

    return true;
}

bool handleList(int &clnt_sock) {
    list_mutex.lock();
    
    map<uint32_t, Song> songs = generateList(clnt_sock);

    cout << "\n";
    cout << "This is a reference. Client should print same result.\n";
    cout << left << setw(50) << "Song Title" <<  " | " << "UID\n";
    cout << string(70, '-') << "\n";

    for (auto it = songs.begin(); it != songs.end(); it++) {
        Song entry = it->second;
        cout << left << setw(50) << entry.title << " | " << entry.uid << "\n";
    }

    cout << "\n";

    list_mutex.unlock();

    return true;
}

bool handlePull(int &clnt_sock) {
    list_mutex.lock();

    // Grab a list of songs that are on the server locally
    map<uint32_t, Song> local_songs = getLocalSongs(clnt_sock);
    cout << "Generated list of local songs\n";

    // Send client songs from server
    generateList(clnt_sock);

    // Get number of missing songs on client
    int missing_count = 0;
    if (recv(clnt_sock, &missing_count, sizeof(missing_count), 0) < 0) {
        fatal_error("Failed to receive number of missing files in PULL");
    }

    cout << "Received number of missing files: " << missing_count << "\n";

    // Start sending a song for each missing song on client
    for (int i=0; i<missing_count; i++) {
        // Receive missing song UID
        uint32_t song_uid;
        if (recv(clnt_sock, &song_uid, sizeof(song_uid), 0) < 0) {
            fatal_error("Failed to receive a UID in PULL");
        }

        cout << "Received UID: " << song_uid << "\n";

        // Retrieve the song file title corresponding to received UID
        string filename = local_songs.at(song_uid).title;
        
        // Load that song file into memory
        ifstream song_file(filename, ios::binary | ios::ate);
        if(!song_file.is_open()) {
            fatal_error("Failed to find the song file corresponding to the name we were given in PULL");
        }

        // Get the song's file size and send it to the client
        streamsize song_size = song_file.tellg();

        cout << "song size: " << song_size << "\n";

        song_file.seekg(0, ios::beg);
        if(send(clnt_sock, &song_size, sizeof(song_size), 0) < 0) {
            fatal_error("Failed to send the size of the song we're sending to the client in PULL");
        }

        // Begin sending raw song file data to client
        char buffer[1024];
        int total_sent = 0;
        while(song_file.read(buffer, sizeof(buffer))) {
            int sent;
            if((sent = send(clnt_sock, buffer, sizeof(buffer), 0)) < 0) {
                fatal_error("failed to send a chunk of data to the client");
                break;
            }
            total_sent = total_sent + sent;
        }

        if (song_file.gcount() > 0) {
            int sent;
            if ((sent = send(clnt_sock, buffer, song_file.gcount(), 0)) < 0) {
                fatal_error("Failed to send the last chunk of the file");
            }
            total_sent += sent;
        }

        cout << "total sent: " << total_sent << "\n";

        // Close song file
        song_file.close();
    }

    list_mutex.unlock();

    return true;

}

string currentTime() {

    // gets timestamp of action
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char timestamp[20];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday, 
            ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    return string(timestamp);
}

void createClientLogDirectory() {
    const char *dir = "clientlog";
    // Check if the directory already exists
    if (access(dir, F_OK) == -1) {
        // Create the directory
        if (mkdir(dir, 0755) != 0) {
            fatal_error("Failed to create clientlog directory");
        }
    }
}

void logClientAction(const string &client_id, const string &action) {

    // function to output timestamp and client action into log
    ofstream log_file(client_id, ios::app); 
    if (log_file.is_open()) {
        log_file << "[" << currentTime() << "] " << action << "\n";
        log_file.close();
    } else {
        fatal_error("Failed to open client log file");
    }
}

void handleClient(int clnt_sock, sockaddr_in clnt_addr) {

    // Ensure the clientlog directory exists
    createClientLogDirectory();

    // Generate a unique file name for logging this client's actions
    string client_id = "clientlog/client_" + string(inet_ntoa(clnt_addr.sin_addr)) + "_" + to_string(ntohs(clnt_addr.sin_port)) + ".log";
    ofstream log_file(client_id);
    if (!log_file.is_open()) {
        fatal_error("Failed to create log file for client");
    }
    log_file.close(); // Close after creation to append later

    // Log client connection
    logClientAction(client_id, "Client connected: " + string(inet_ntoa(clnt_addr.sin_addr)) + ":" + to_string(ntohs(clnt_addr.sin_port)));


    // Receive a command
    char buffer[RCVBUFSIZE];
    int rec_com_size;
    memset(buffer, 0, sizeof(buffer));

    bool still_running = true;
    while (true) {
        if ((rec_com_size = recv(clnt_sock, buffer, RCVBUFSIZE - 1, 0)) < 0) {
            fatal_error("Failed to receive a client command!");
        }
        else if (rec_com_size == 0) {
            console_mutex.lock();
            cout << "a client disconnected.\n";
            console_mutex.unlock();
            still_running = false;
            break;
        }
        buffer[rec_com_size] = '\0';

        // Process which command the server received
        string command = buffer;
        console_mutex.lock();
        cout << "Rec command: " << command << "\n";
        console_mutex.unlock();

        if (command == "LIST") {
            console_mutex.lock();
            cout << "Got list command!\n";
            logClientAction(client_id, "Processing LIST command");
            console_mutex.unlock();
            still_running = handleList(clnt_sock);

        } else if (command == "DIFF") {
            console_mutex.lock();
            cout << "Got diff command!\n";
            logClientAction(client_id, "Processing DIFF command");
            console_mutex.unlock();
            still_running = handleDiff(clnt_sock);

        } else if (command == "PULL") {
            console_mutex.lock();
            cout << "Got pull command!\n";
            logClientAction(client_id, "Processing PULL command");
            console_mutex.unlock();
            still_running = handlePull(clnt_sock);

        } else if (command == "LEAVE") {
            console_mutex.lock();
            cout << "Got leave command!\n";
            logClientAction(client_id, "Processing LEAVE command");
            console_mutex.unlock();
            still_running = false;
        } else {
            // Client somehow sent an incorrect command. Retry client handling
            // Handle this better later
            console_mutex.lock();
            cout << "Didn't receive a real command\n";
            logClientAction(client_id, "Invalid command received");
            console_mutex.unlock();
        }
    }
    close(clnt_sock);
}

/* The main function */
int main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    int clnt_sock;
    struct sockaddr_in clnt_addr;
    socklen_t clnt_len;

    char nameBuf[BUFSIZE];			/* Buff to store name from client */
    unsigned char md_value[EVP_MAX_MD_SIZE];	/* Buff to store change result */
    EVP_MD_CTX *mdctx;				/* Digest data structure declaration */
    const EVP_MD *md;				/* Digest data structure declaration */
    int md_len;					/* Digest data structure size tracking */

    /* Create new TCP Socket for incoming requests*/
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ((sock) < 0) {
      fatal_error("socket() failed");
    }
    // printf("Created new TCP socket\n");

    /* Construct local address structure*/
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(3179);

    // printf("Constructed local address structure\n");
    
    /* Bind to local address structure */
    if (bind(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
      fatal_error("bind() failed");
    }

    // printf("Bound to local address structure\n");

    /* Listen for incoming connections */
    if (listen(sock, MAXPENDING) < 0) {
      fatal_error("listen() failed");
    }

    // Vector for storing client threads
    vector<thread> client_threads;

    /* Loop server forever*/
    printf("Begin listening for clients on port: %d\n", 3179);
    while(true)
    {
      /* Accept incoming connection */
      clnt_len = sizeof(clnt_addr);
      if ( (clnt_sock = accept(sock, (struct sockaddr *) &clnt_addr, &clnt_len)) < 0) {
        fatal_error("accept() failed");
      }

      // Lock io
      console_mutex.lock();
      cout << "client accepted: " << inet_ntoa(clnt_addr.sin_addr) << ":" << ntohs(clnt_addr.sin_port) << "\n";
      console_mutex.unlock();

      /* BEGIN NEW FUNCTIONALITIES */
      client_threads.push_back(thread(handleClient, clnt_sock, clnt_addr));
      client_threads.back().detach();
    }
}


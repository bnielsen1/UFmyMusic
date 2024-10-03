# UFmyMusic

## Running

Building/running the server:
- cd to server folder
- compile with "g++ server.cpp -o server -lssl -lcrypto"
    - Note these are unix based libraries so you might have to compile on UF linux servers or a linux/mac based computer
- run with "./server"

Building/running the client:
- cd to client folder
- compile with "g++ client.cpp -o client -lssl -lcrypto"
- run with "./client Name"
    - I copied my code from P1 to begin working and didn't remove this requirement. Remove this later.

## TO - DO

Missing Content:
- Not multithreaded
- Does not log client interactions to a file on the server

Bugs:
- N/A :D

Potential Bugs:
- Song filename is larger than rcv buffer size on client
- Pulling when files on server and client have same name but different content
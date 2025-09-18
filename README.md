# Whiteboard_Server
whiteboard server for collaborative drawing
Collaborative Whiteboard (Windows)

Overview
--------
This is a real-time collaborative whiteboard server designed to run on Windows.
- Serves an HTML page to any browser on the same machine or LAN.
- Users can draw in real-time using WebSockets.
- Includes simple colour selection and an eraser.

Files
-----
- whiteboard_server_win_full.c – Windows server source code
- whiteboard_server_win_full.exe – Compiled Windows executable
- whiteboard.html – HTML client (embedded in the server; included for reference)

Requirements
------------
- Windows (10/11 recommended)
- GCC (MinGW / MSYS2)
- OpenSSL installed (for WebSocket handshake: SHA1 + Base64)

Compilation
-----------
Open PowerShell or MSYS2 terminal:

gcc whiteboard_server_win_full.c -o whiteboard_server_win_full -lws2_32 -lssl -lcrypto

- -lws2_32 → Winsock2 (Windows networking)
- -lssl -lcrypto → OpenSSL (SHA1/Base64 for WebSocket handshake)

Running the Server
------------------
1. Open PowerShell and navigate to the folder containing the executable.
2. Run the server:

.\whiteboard_server_win_full.exe

3. The console will display:

Collaborative Whiteboard running at http://localhost:8080

Connecting from a Browser
-------------------------
On the same PC:
- Open a browser and navigate to:
http://localhost:8080

On another device on the LAN (e.g., Mac):
1. Find the Windows machine’s IPv4 address:

ipconfig

- Look for IPv4 Address under the active network adapter (e.g., 192.168.1.42).

2. On the other device, open a browser and go to:

http://<your ip>:8080

Note: Do not use localhost or 127.0.0.1 on another device — it will point to that device instead of the server.

Firewall Considerations
----------------------
- Windows may block incoming connections.
- If prompted, allow access on Private networks.
- To allow manually:
  1. Open Windows Defender Firewall → Advanced settings.
  2. Create an Inbound Rule → TCP → Port 8080.
  3. Allow for Private networks.

Usage
-----
- Open the whiteboard page on one or more devices.
- Draw using the mouse; colour can be changed using the buttons.
- Drawings are broadcast in real-time to all connected clients.

Notes
-----
- This server is Windows-only.
- No external libraries are required for clients — just a modern web browser.
- WebSocket communication is unencrypted (HTTP only).
- The HTML page is embedded within the executable; no separate files are required for LAN usage.

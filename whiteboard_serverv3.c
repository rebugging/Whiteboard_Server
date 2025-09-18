#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096

// Linked list of connected clients
typedef struct ClientNode {
    SOCKET socket;
    struct ClientNode *next;
} ClientNode;

ClientNode *clients = NULL;
CRITICAL_SECTION clients_lock;

// ---- HTML page to serve ----
const char* html_page =
"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
"<!DOCTYPE html>"
"<html><head><title>Collaborative Whiteboard</title></head>"
"<body style='margin:0; overflow:hidden;'>"
"<canvas id='board' width='1000' height='600' style='border:1px solid black;'></canvas>"
"<div style='position:fixed;top:0;left:0;background:#eee;padding:5px;'>"
"<button onclick=\"setColor('black')\">Black</button>"
"<button onclick=\"setColor('red')\">Red</button>"
"<button onclick=\"setColor('blue')\">Blue</button>"
"<button onclick=\"setColor('white')\">Eraser</button>"
"</div>"
"<script>"
"let canvas=document.getElementById('board');"
"let ctx=canvas.getContext('2d');"
"let drawing=false;"
"let colour='black';"
"function setColor(c){ colour=c; }"
"let ws=new WebSocket('ws://'+location.host,'chat');"
"canvas.addEventListener('mousedown',()=>drawing=true);"
"canvas.addEventListener('mouseup',()=>{drawing=false;ctx.beginPath();});"
"canvas.addEventListener('mousemove',draw);"
"function draw(e){"
" if(!drawing) return;"
" let x=e.offsetX,y=e.offsetY;"
" ctx.lineWidth=5;ctx.lineCap='round';ctx.strokeStyle=colour;"
" ctx.lineTo(x,y);ctx.stroke();ctx.beginPath();ctx.moveTo(x,y);"
" ws.send(JSON.stringify({x:x,y:y,colour:colour}));"
"}"
"ws.onmessage=function(msg){"
" let d=JSON.parse(msg.data);"
" ctx.lineWidth=5;ctx.lineCap='round';ctx.strokeStyle=d.colour;"
" ctx.beginPath();ctx.moveTo(d.x,d.y);ctx.lineTo(d.x,d.y);ctx.stroke();"
"};"
"</script></body></html>";

// ---- Base64 encode helper (OpenSSL-safe) ----
char* base64_encode(const unsigned char* input, int length) {
    BIO *b64, *bmem;
    char *buff;
    int len;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    BIO_write(b64, input, length);
    BIO_flush(b64);

    len = BIO_get_mem_data(bmem, &buff);
    char *output = (char*)malloc(len + 1);
    memcpy(output, buff, len);
    output[len] = '\0';

    BIO_free_all(b64);
    return output;
}

// ---- Generate WebSocket accept key ----
char* websocket_accept_key(const char* client_key) {
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    unsigned char sha[SHA_DIGEST_LENGTH];
    snprintf(combined, sizeof(combined), "%s%s", client_key, guid);

    SHA1((unsigned char*)combined, strlen(combined), sha);
    return base64_encode(sha, SHA_DIGEST_LENGTH);
}

// ---- Broadcast message to all clients ----
void broadcast(const char *msg, int len, SOCKET exclude) {
    EnterCriticalSection(&clients_lock);
    ClientNode *node = clients;
    while (node) {
        if (node->socket != exclude) {
            send(node->socket, msg, len, 0);
        }
        node = node->next;
    }
    LeaveCriticalSection(&clients_lock);
}

// ---- Client handler ----
unsigned __stdcall client_handler(void *arg) {
    SOCKET client = *(SOCKET*)arg;
    char buffer[BUFFER_SIZE];
    int bytes;

    // ---- Receive initial request ----
    bytes = recv(client, buffer, sizeof(buffer)-1, 0);
    buffer[bytes] = '\0';

    if (strstr(buffer, "Upgrade: websocket")) {
        // WebSocket handshake
        char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
        if (!key_start) { closesocket(client); return 0; }
        key_start += 19;
        char *key_end = strstr(key_start, "\r\n");
        *key_end = '\0';
        char *accept_key = websocket_accept_key(key_start);

        char response[512];
        snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n",
            accept_key);

        send(client, response, strlen(response), 0);
        free(accept_key);

        // Add to client list
        EnterCriticalSection(&clients_lock);
        ClientNode *new_client = (ClientNode*)malloc(sizeof(ClientNode));
        new_client->socket = client;
        new_client->next = clients;
        clients = new_client;
        LeaveCriticalSection(&clients_lock);

        // ---- Receive messages from client ----
        while ((bytes = recv(client, buffer, sizeof(buffer), 0)) > 0) {
            if (bytes < 2) continue;

            int payload_len = buffer[1] & 0x7F;
            unsigned char mask[4];
            memcpy(mask, buffer + 2, 4);
            char *msg_data = buffer + 6;

            for (int i = 0; i < payload_len; i++)
                msg_data[i] ^= mask[i % 4];

            char frame[BUFFER_SIZE];
            frame[0] = 0x81; // text frame
            frame[1] = payload_len;
            memcpy(frame + 2, msg_data, payload_len);
            broadcast(frame, 2 + payload_len, client);
        }
    } else {
        // Serve HTML page for normal HTTP GET
        send(client, html_page, strlen(html_page), 0);
    }

    closesocket(client);

    // Remove from client list
    EnterCriticalSection(&clients_lock);
    ClientNode **curr = &clients;
    while (*curr) {
        if ((*curr)->socket == client) {
            ClientNode *to_free = *curr;
            *curr = (*curr)->next;
            free(to_free);
            break;
        }
        curr = &(*curr)->next;
    }
    LeaveCriticalSection(&clients_lock);

    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server, client;
    struct sockaddr_in server_addr, client_addr;
    int client_size = sizeof(client_addr);

    InitializeCriticalSection(&clients_lock);
    WSAStartup(MAKEWORD(2,2), &wsa);

    server = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server, 10);

    printf("Collaborative Whiteboard running at http://localhost:%d\n", PORT);

    while (1) {
        client = accept(server, (struct sockaddr*)&client_addr, &client_size);
        _beginthreadex(NULL, 0, client_handler, (void*)&client, 0, NULL);
    }

    closesocket(server);
    WSACleanup();
    DeleteCriticalSection(&clients_lock);
    return 0;
}

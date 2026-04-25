// tcp_transport.h
// ECE 470 - Project 1 Part 3
// Yousef Alhindi
//
// TCP transport layer for sending/receiving length-prefixed JSON messages.
// Provides sendMessage() and recvMessage() that handle the 4-byte
// big-endian length prefix framing over a raw TCP socket.
// Based on the recvLoop pattern from TCPBaseServer.cpp/TCPBaseClient.cpp.

#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <vector>

// Read exactly 'size' bytes from socket (handles partial reads)
// Returns number of bytes actually read
inline int recvLoop(int sock, char* data, int size) {
    int nleft = size;
    int nread;
    char* ptr = data;

    while (nleft > 0) {
        nread = recv(sock, ptr, nleft, 0);
        if (nread < 0) {
            std::cerr << "recv error: " << strerror(errno) << std::endl;
            return -1;
        } else if (nread == 0) {
            // connection closed
            break;
        }
        nleft -= nread;
        ptr += nread;
    }
    return size - nleft;
}

// Send exactly 'size' bytes to socket (handles partial writes)
inline int sendAll(int sock, const char* data, int size) {
    int nleft = size;
    int nsent;
    const char* ptr = data;

    while (nleft > 0) {
        nsent = send(sock, ptr, nleft, 0);
        if (nsent < 0) {
            std::cerr << "send error: " << strerror(errno) << std::endl;
            return -1;
        }
        nleft -= nsent;
        ptr += nsent;
    }
    return size;
}

// Send a framed message: [4-byte big-endian length][JSON payload]
inline bool sendMessage(int sock, const std::string& json) {
    uint32_t payloadLen = static_cast<uint32_t>(json.size());
    uint32_t netLen = htonl(payloadLen);

    // send 4-byte header
    if (sendAll(sock, reinterpret_cast<const char*>(&netLen), 4) != 4)
        return false;
    // send payload
    if (sendAll(sock, json.c_str(), payloadLen) != (int)payloadLen)
        return false;

    return true;
}

// Receive a framed message: reads 4-byte length, then payload
// Returns true on success, false on error/disconnect
inline bool recvMessage(int sock, std::string& json) {
    // read 4-byte length header
    uint32_t netLen;
    int n = recvLoop(sock, reinterpret_cast<char*>(&netLen), 4);
    if (n != 4) return false;

    uint32_t payloadLen = ntohl(netLen);
    if (payloadLen == 0) {
        json = "";
        return true;
    }
    if (payloadLen > 1048576) { // sanity check: 1MB max
        std::cerr << "Message too large: " << payloadLen << std::endl;
        return false;
    }

    // read payload
    std::vector<char> buf(payloadLen);
    n = recvLoop(sock, buf.data(), payloadLen);
    if (n != (int)payloadLen) return false;

    json.assign(buf.data(), payloadLen);
    return true;
}

#endif // TCP_TRANSPORT_H

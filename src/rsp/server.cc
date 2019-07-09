
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <thread>

#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "server.h"
#include "commands.h"

namespace RSP {

static std::thread *serverThread = NULL;
static bool doStopServer = false;

static int readChar(int clientSocket)
{
    unsigned char c;
    int ret = read(clientSocket, &c, 1);
    return ret <= 0 ? -1 : c;
}

static int writeChar(int clientSocket, unsigned char c)
{
    int ret = write(clientSocket, &c, 1);
    return ret <= 0 ? -1 : 0;
}

static int requestResend(int clientSocket)
{
    return writeChar(clientSocket, '-');
}

static int acknowledgePacket(int clientSocket)
{
    return writeChar(clientSocket, '+');
}

static int encodeHex(unsigned int digit)
{
    return (digit < 10) ? ('0' + digit) : ('a' + (digit - 10));
}

static int readPacket(int clientSocket, char *packet, size_t packetLen)
{
    // Start reading new packet.
    // First byte should be one of:
    //   - '$' indicate the start of a normal data transmission
    //   - 0x03 request to interrupt the program

    int c = readChar(clientSocket);
    if (c < 0) {
        std::cerr << "Failed to read from socket: " << strerror(errno) << std::endl;
        return -1;
    }

    if (c == 0x03) {
        std::cerr << "Request to interrupt emulator" << std::endl;
        return 0;
    }

    if (c != '$') {
        return 0;
    }

    // Read packet bytes until the terminating '#' character.
    // Compute a checksum at the same time, and unescape characters
    // before storing them in the packet.
    bool escape = false;
    unsigned int checksum = 0;
    size_t len = 0;

    while (len < packetLen) {
        c = readChar(clientSocket);
        if (c < 0) {
            std::cerr << "Failed to read from socket: " << strerror(errno) << std::endl;
            return -1;
        } else if (c == '#') {
            break;
        }
        // Update the checksum.
        checksum += c;
        // Handle character escaping.
        if (escape) {
            c ^= 0x20;
            escape = false;
        } else if (c == '{') {
            escape = true;
            continue;
        }
        packet[len++] = c;
    }

    if (len == packetLen) {
        std::cerr << "Reached maximum packet length" << std::endl;
        return -1;
    }

    // Check the checksum at the end of the packet.
    // The checksum is computed as the sum of all received bytes, and encode
    // with two hexadecimal characters.
    int c0 = readChar(clientSocket);
    int c1 = readChar(clientSocket);
    checksum %= 256;

    if (c0 != encodeHex(checksum / 16) ||
        c1 != encodeHex(checksum % 16)) {
        std::cerr << "Invalid packet checksum: " << c0 << " " << c1 << std::endl;
        return requestResend(clientSocket);
    }

    int ret = acknowledgePacket(clientSocket);
    if (ret < 0) {
        return -1;
    }

    packet[len] = '\0';
    return len;
}

static int writePacket(int clientSocket, char *packet, size_t packetLen)
{
    // Format the packet byes:
    //   - first byte is '$'
    //   - last byte is '#', followed by two checksum bytes
    //   - escape occuring '#' or '{' characters inside the packet data
    size_t formattedPacketLen = packetLen + 4;
    unsigned int checksum = 0;

    for (size_t i = 0; i < packetLen; i++) {
        if (packet[i] == '{' || packet[i] == '#')
            formattedPacketLen++;
    }

    char formattedPacket[formattedPacketLen];
    size_t off = 1;

    formattedPacket[0] = '$';
    for (size_t i = 0; i < packetLen; i++) {
        if (packet[i] == '{' || packet[i] == '#') {
            formattedPacket[off++] = '{';
            checksum += '{';
            formattedPacket[off++] = packet[i] ^ 0x20;
            checksum += packet[i] ^ 0x20;
        } else {
            formattedPacket[off++] = packet[i];
            checksum += packet[i];
        }
    }
    checksum %= 256;
    formattedPacket[off++] = '#';
    formattedPacket[off++] = encodeHex(checksum / 16);
    formattedPacket[off++] = encodeHex(checksum % 16);

    // Prepare to send the formatted packet until an positive acknowledgment is
    // received.
    int repeat = 0;
    while (repeat++ < 10) {
        int ret = write(clientSocket, formattedPacket, formattedPacketLen);
        if (ret < (int)formattedPacketLen) {
            std::cerr << "Failed to write to socket: " << strerror(errno) << std::endl;
            return -1;
        }

        int c = readChar(clientSocket);
        if (c < 0) {
            std::cerr << "Failed to read from socket: " << strerror(errno) << std::endl;
            return -1;
        } else if (c == '+') {
            // Successful send.
            return 0;
        } else {
            // Retry for rejected packet.
        }
    }
    std::cerr << "Failed to send packet after " << repeat << " retries" << std::endl;
    return -1;
}

static void serverRoutine()
{
    std::cerr << "RSP server thread started" << std::endl;

    int serverSocket = 0;
    int clientSocket = 0;
    struct sockaddr_in serverIp = { 0 };
    size_t packetLen = 1024;
    char input[packetLen];
    char output[packetLen];
    int ret;

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create server socket: " << strerror(errno) << std::endl;
        goto cancel;
    }

    serverIp.sin_family = AF_INET;
    serverIp.sin_addr.s_addr = htonl(INADDR_ANY);
    serverIp.sin_port = htons(1234);

    ret = bind(serverSocket, (struct sockaddr *)&serverIp , sizeof(serverIp));
    if (ret < 0) {
        std::cerr << "Failed to bind server socket: " << strerror(errno) << std::endl;
        goto cancel;
    }

    ret = listen(serverSocket, 20);
    if (ret < 0) {
        std::cerr << "Failed to listen on server socket: " << strerror(errno) << std::endl;
        goto cancel;
    }

    clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket < 0) {
        std::cerr << "Failed to accept client socket: " << strerror(errno) << std::endl;
        goto cancel;
    }

    while (true) {
        if (doStopServer) {
            std::cerr << "Server stop requested" << std::endl;
            goto cancel;
        }

        ret = readPacket(clientSocket, input, sizeof(input));
        if (ret < 0) {
            goto cancel;
        }
        if (ret == 0) {
            continue;
        }
        std::cerr << "<- " << input << std::endl;
        ret = handlePacket(input, ret, output, sizeof(output));
        if (ret < 0) {
            goto cancel;
        }
        output[ret] = '\0';
        std::cerr << "-> " << output << std::endl;
        ret = writePacket(clientSocket, output, ret);
        if (ret < 0) {
            goto cancel;
        }
    }

cancel:
    if (clientSocket >= 0)
        close(clientSocket);
    if (serverSocket >= 0)
        close(serverSocket);
    std::cerr << "RSP server thread exited" << std::endl;
    serverThread = NULL;
}

void startServer()
{
    doStopServer = false;
    serverThread = new std::thread(serverRoutine);
}

void stopServer()
{
    doStopServer = true;
}

};

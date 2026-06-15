#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dfone {
namespace net {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSendFlags = 0;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

inline bool initialize_sockets()
{
#ifdef _WIN32
    struct WinsockRuntime {
        bool ok = false;

        WinsockRuntime()
        {
            WSADATA data{};
            ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }

        ~WinsockRuntime()
        {
            if (ok) {
                WSACleanup();
            }
        }
    };

    static WinsockRuntime runtime;
    return runtime.ok;
#else
    return true;
#endif
}

inline bool is_valid(SocketHandle socket)
{
    return socket != kInvalidSocket;
}

inline int last_error_code()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline bool is_interrupted(int code)
{
#ifdef _WIN32
    return code == WSAEINTR;
#else
    return code == EINTR;
#endif
}

inline std::string last_error_message()
{
#ifdef _WIN32
    return "winsock error " + std::to_string(last_error_code());
#else
    return std::strerror(last_error_code());
#endif
}

inline void close_socket(SocketHandle socket)
{
    if (!is_valid(socket)) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

inline int send_socket(SocketHandle socket, const void *data, std::size_t size)
{
#ifdef _WIN32
    return ::send(socket, static_cast<const char *>(data), static_cast<int>(size), kSendFlags);
#else
    return static_cast<int>(::send(socket, data, size, kSendFlags));
#endif
}

inline int recv_socket(SocketHandle socket, void *data, std::size_t size)
{
#ifdef _WIN32
    return ::recv(socket, static_cast<char *>(data), static_cast<int>(size), 0);
#else
    return static_cast<int>(::recv(socket, data, size, 0));
#endif
}

inline void set_socket_timeouts(SocketHandle socket, int timeout_ms)
{
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    (void)::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    (void)::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                       reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    (void)::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

inline void set_socket_buffer(SocketHandle socket, int option, int bytes)
{
#ifdef _WIN32
    (void)::setsockopt(socket, SOL_SOCKET, option,
                       reinterpret_cast<const char *>(&bytes), sizeof(bytes));
#else
    (void)::setsockopt(socket, SOL_SOCKET, option, &bytes, sizeof(bytes));
#endif
}

inline void set_tcp_no_delay(SocketHandle socket, int enable)
{
#ifdef _WIN32
    (void)::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char *>(&enable), sizeof(enable));
#else
    (void)::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
#endif
}

}  // namespace net
}  // namespace dfone

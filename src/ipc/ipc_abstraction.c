#include "ipc_abstraction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    // Windows Named Pipes implementation
    #define PIPE_NAME_FORMAT "\\\\.\\pipe\\%s"
    
    int ipc_init(void) {
        // No global init needed for Windows pipes
        return IPC_OK;
    }
    
    ipc_handle_t ipc_connect(const char* server_name) {
        char pipe_name[256];
        snprintf(pipe_name, sizeof(pipe_name), PIPE_NAME_FORMAT, server_name);
        
        // Wait for the pipe to become available (up to 5 seconds)
        HANDLE hPipe;
        DWORD start_time = GetTickCount();
        const DWORD timeout_ms = 5000;
        
        do {
            hPipe = CreateFileA(
                pipe_name,
                GENERIC_READ | GENERIC_WRITE,
                0,                      // no sharing
                NULL,                   // default security
                OPEN_EXISTING,
                0,                      // default attributes
                NULL
            );
            
            if (hPipe != INVALID_HANDLE_VALUE)
                break;
            
            if (GetLastError() != ERROR_PIPE_BUSY) {
                fprintf(stderr, "IPC: CreateFile failed, error %lu\n", GetLastError());
                return IPC_INVALID_HANDLE;
            }
            
            // Pipe busy, wait for it
            if (!WaitNamedPipeA(pipe_name, timeout_ms)) {
                fprintf(stderr, "IPC: WaitNamedPipe timeout\n");
                return IPC_INVALID_HANDLE;
            }
        } while (GetTickCount() - start_time < timeout_ms);
        
        // Set pipe read mode to message mode
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
            fprintf(stderr, "IPC: SetNamedPipeHandleState failed\n");
            CloseHandle(hPipe);
            return IPC_INVALID_HANDLE;
        }
        
        return hPipe;
    }
    
    int ipc_send(ipc_handle_t handle, const uint8_t* data, uint32_t length) {
        DWORD bytes_written;
        // Prefix length in little-endian (same as browser protocol)
        BOOL success = WriteFile(handle, &length, sizeof(length), &bytes_written, NULL);
        if (!success || bytes_written != sizeof(length))
            return IPC_ERR_SEND;
        
        success = WriteFile(handle, data, length, &bytes_written, NULL);
        if (!success || bytes_written != length)
            return IPC_ERR_SEND;
        
        FlushFileBuffers(handle);
        return IPC_OK;
    }
    
    int ipc_recv(ipc_handle_t handle, uint8_t** out_data, uint32_t* out_length) {
        DWORD bytes_read;
        uint32_t length;
        
        // Read length prefix
        if (!ReadFile(handle, &length, sizeof(length), &bytes_read, NULL) || bytes_read != sizeof(length))
            return IPC_ERR_RECV;
        
        if (length == 0 || length > 10 * 1024 * 1024)  // 10MB limit
            return IPC_ERR_RECV;
        
        uint8_t* buffer = (uint8_t*)malloc(length);
        if (!buffer)
            return IPC_ERR_RECV;
        
        uint32_t total_read = 0;
        while (total_read < length) {
            if (!ReadFile(handle, buffer + total_read, length - total_read, &bytes_read, NULL)) {
                free(buffer);
                return IPC_ERR_RECV;
            }
            total_read += bytes_read;
        }
        
        *out_data = buffer;
        *out_length = length;
        return IPC_OK;
    }
    
    void ipc_close(ipc_handle_t handle) {
        if (handle != IPC_INVALID_HANDLE)
            CloseHandle(handle);
    }
    
    void ipc_cleanup(void) {
        // Nothing to clean up
    }

#else // Linux / Unix Domain Sockets

    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    #include <errno.h>
    
    #define SOCKET_PATH_FORMAT "/tmp/%s.sock"
    
    int ipc_init(void) {
        // No global init needed
        return IPC_OK;
    }
    
    ipc_handle_t ipc_connect(const char* server_name) {
        char socket_path[256];
        snprintf(socket_path, sizeof(socket_path), SOCKET_PATH_FORMAT, server_name);
        
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock == -1) {
            perror("socket");
            return IPC_INVALID_HANDLE;
        }
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("connect");
            close(sock);
            return IPC_INVALID_HANDLE;
        }
        
        return sock;
    }
    
    int ipc_send(ipc_handle_t handle, const uint8_t* data, uint32_t length) {
        // Send length prefix (network byte order? Use same as Windows: little-endian)
        uint32_t len_le = length;  // On little-endian machines no conversion needed
        ssize_t sent = send(handle, &len_le, sizeof(len_le), 0);
        if (sent != sizeof(len_le))
            return IPC_ERR_SEND;
        
        sent = send(handle, data, length, 0);
        if (sent != (ssize_t)length)
            return IPC_ERR_SEND;
        
        return IPC_OK;
    }
    
    int ipc_recv(ipc_handle_t handle, uint8_t** out_data, uint32_t* out_length) {
        uint32_t length;
        ssize_t received = recv(handle, &length, sizeof(length), MSG_WAITALL);
        if (received != sizeof(length))
            return IPC_ERR_RECV;
        
        if (length == 0 || length > 10 * 1024 * 1024)
            return IPC_ERR_RECV;
        
        uint8_t* buffer = (uint8_t*)malloc(length);
        if (!buffer)
            return IPC_ERR_RECV;
        
        uint32_t total_read = 0;
        while (total_read < length) {
            received = recv(handle, buffer + total_read, length - total_read, MSG_WAITALL);
            if (received <= 0) {
                free(buffer);
                return IPC_ERR_RECV;
            }
            total_read += received;
        }
        
        *out_data = buffer;
        *out_length = length;
        return IPC_OK;
    }
    
    void ipc_close(ipc_handle_t handle) {
        if (handle != IPC_INVALID_HANDLE)
            close(handle);
    }
    
    void ipc_cleanup(void) {
        // Nothing needed
    }
#endif
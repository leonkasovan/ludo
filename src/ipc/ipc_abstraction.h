#ifndef IPC_ABSTRACTION_H
#define IPC_ABSTRACTION_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE ipc_handle_t;
    #define IPC_INVALID_HANDLE NULL
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    typedef int ipc_handle_t;
    #define IPC_INVALID_HANDLE -1
#endif

// Common error codes
#define IPC_OK          0
#define IPC_ERR_CONNECT 1
#define IPC_ERR_SEND    2
#define IPC_ERR_RECV    3
#define IPC_ERR_CLOSE   4

// Initialize the IPC library (platform-specific setup)
int ipc_init(void);

// Connect to the main FDM app's server
// Returns IPC handle on success, IPC_INVALID_HANDLE on failure
ipc_handle_t ipc_connect(const char* server_name);

// Send a message (raw bytes) over IPC
// Returns IPC_OK on success
int ipc_send(ipc_handle_t handle, const uint8_t* data, uint32_t length);

// Receive a message (allocates buffer, caller must free)
// Returns IPC_OK on success, *out_data allocated, *out_length set
int ipc_recv(ipc_handle_t handle, uint8_t** out_data, uint32_t* out_length);

// Close the IPC connection
void ipc_close(ipc_handle_t handle);

// Cleanup global resources
void ipc_cleanup(void);

#endif // IPC_ABSTRACTION_H
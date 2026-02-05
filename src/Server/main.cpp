#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include "Perun/C/perun_c.h"

// Constants
const char* SHM_NAME = "/perun_shm";
const size_t SHM_SIZE = 4 * 1024 * 1024; // 4MB

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

const char* SOCKET_PATH = "/tmp/perun.sock";

void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char* argv[]) {
    std::cout << "[PerunServer] Starting..." << std::endl;

    // 1. Initialize Engine
    if (!Perun_Init()) {
        std::cerr << "Failed to init Perun" << std::endl;
        return 1;
    }

    PerunWindow* window = Perun_Window_Create("Perun Universal Frontend", 640, 480);
    if (!Perun_Window_Init(window)) {
         std::cerr << "Failed to init Window" << std::endl;
         return 1;
    }
    Perun_Renderer_Init();

    // 2. Setup Shared Memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        std::cerr << "Shared Memory Open Failed: " << strerror(errno) << std::endl;
        return 1;
    }
    ftruncate(shm_fd, SHM_SIZE);
    
    void* shm_ptr = mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        std::cerr << "Memory Map Failed" << std::endl;
        return 1;
    }
    std::cout << "[PerunServer] Shared Memory initialized at " << SHM_NAME << std::endl;

    // 3. Setup Socket
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Socket error");
        return 1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    unlink(SOCKET_PATH); // Remove old socket

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Bind error");
        return 1;
    }

    if (listen(server_sock, 5) == -1) {
        perror("Listen error");
        return 1;
    }
    SetNonBlocking(server_sock);
    std::cout << "[PerunServer] Listening on " << SOCKET_PATH << std::endl;

    // Create a texture (start black)
    PerunTexture* screen = Perun_Texture_Create(640, 480);
    int client_fd = -1;

    // 4. Main Loop
    std::cout << "[PerunServer] Waiting for updates..." << std::endl;
    bool running = true;
    while (running) {
        // Accept new connection if none
        if (client_fd == -1) {
            client_fd = accept(server_sock, NULL, NULL);
            if (client_fd != -1) {
                SetNonBlocking(client_fd);
                std::cout << "[PerunServer] Client Connected!" << std::endl;
            }
        }

        // Read Commands (Simple "Frame Ready" byte for now)
        if (client_fd != -1) {
            char buf[1];
            int n = read(client_fd, buf, 1);
            if (n > 0) {
                // Command received, update texture from SHM
                Perun_Texture_SetData(screen, shm_ptr, 640 * 480 * 4);
                // Maybe send back input state here
            } else if (n == 0) {
                // Disconnected
                close(client_fd);
                client_fd = -1;
                std::cout << "[PerunServer] Client Disconnected" << std::endl;
            }
        } else {
             // Fallback auto-update if no client (for debug)
             Perun_Texture_SetData(screen, shm_ptr, 640 * 480 * 4);
        }

        Perun_Renderer_BeginScene();
        Perun_Renderer_DrawTexture(0, 0, 1.0f, 1.0f, screen); 
        Perun_Renderer_EndScene();

        if (!Perun_Window_Update(window)) {
            running = false;
        }
    }

    // Cleanup
    if (client_fd != -1) close(client_fd);
    close(server_sock);
    unlink(SOCKET_PATH);
    munmap(shm_ptr, SHM_SIZE);
    close(shm_fd);
    shm_unlink(SHM_NAME);

    Perun_Renderer_Shutdown();
    Perun_Window_Destroy(window);
    Perun_Shutdown();
    
    return 0;
}

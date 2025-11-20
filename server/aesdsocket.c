#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define TAG "aesdsocket"
#define WRITE_FILE "/var/tmp/aesdsocketdata"
#define PORT 9000
#define BUFFER_SIZE 4096

static volatile sig_atomic_t exit_flag = false;

void signal_handler(int signal) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = true;
}

int main(int argc, char **argv) {
    bool daemon_mode = false;
    int fd = -1;
    int sock = -1;
    int ret = 0;

    // Parse command line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    openlog(TAG, 0, LOG_USER);

    // Register signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to register signal handlers");
        return -1;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Set socket options to allow reuse of address
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
        close(sock);
        return -1;
    }

    // Create address
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to address
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind to port %d: %s", PORT, strerror(errno));
        close(sock);
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
            close(sock);
            return -1;
        }
        if (pid > 0) {
            // Parent process exits
            exit(0);
        }
        // Child process continues
        if (setsid() == -1) {
            syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
            close(sock);
            return -1;
        }
        // Change working directory to root
        if (chdir("/") == -1) {
            syslog(LOG_ERR, "Failed to change directory: %s", strerror(errno));
        }
        // Redirect standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDONLY);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_RDWR);
    }

    // Listen for connections
    if (listen(sock, 10) == -1) {
        syslog(LOG_ERR, "Failed to listen: %s", strerror(errno));
        close(sock);
        return -1;
    }

    // Open data file
    fd = open(WRITE_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to open file: %s, %s", WRITE_FILE, strerror(errno));
        close(sock);
        return -1;
    }

    // Main server loop
    while (!exit_flag) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_sock == -1) {
            if (exit_flag) {
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        char *client_ip = inet_ntoa(client_addr.sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Allocate receive buffer
        char *data = malloc(BUFFER_SIZE);
        if (data == NULL) {
            syslog(LOG_ERR, "Failed to allocate memory for receive buffer");
            close(client_sock);
            continue;
        }
        size_t data_length = 0;
        size_t buffer_capacity = BUFFER_SIZE;

        // Receive data loop
        bool connection_error = false;
        while (!exit_flag && !connection_error) {
            // Check if we need to expand buffer
            if (data_length >= buffer_capacity) {
                size_t new_capacity = buffer_capacity * 2;
                char *new_data = realloc(data, new_capacity);
                if (new_data == NULL) {
                    syslog(LOG_ERR, "Failed to reallocate memory, discarding packet");
                    connection_error = true;
                    break;
                }
                data = new_data;
                buffer_capacity = new_capacity;
            }

            int count = recv(client_sock, data + data_length, buffer_capacity - data_length, 0);
            
            if (count == -1) {
                syslog(LOG_ERR, "Socket error: %s", strerror(errno));
                connection_error = true;
                break;
            }
            
            if (count == 0) {
                // Client closed connection
                break;
            }

            data_length += count;

            // Check for complete packets (terminated by newline)
            char *search_start = data;
            char *end_of_packet;
            while ((end_of_packet = memchr(search_start, '\n', data_length - (search_start - data))) != NULL) {
                int packet_size = end_of_packet - data + 1;

                // Write packet to file
                if (write(fd, data, packet_size) == -1) {
                    syslog(LOG_ERR, "Failed to write data to file: %s", strerror(errno));
                    connection_error = true;
                    break;
                }

                // Send entire file contents back to client
                off_t file_size = lseek(fd, 0, SEEK_END);
                if (file_size == -1) {
                    syslog(LOG_ERR, "Failed to get file size: %s", strerror(errno));
                    connection_error = true;
                    break;
                }
                
                if (lseek(fd, 0, SEEK_SET) == -1) {
                    syslog(LOG_ERR, "Failed to seek to beginning of file: %s", strerror(errno));
                    connection_error = true;
                    break;
                }

                char send_buffer[1024];
                ssize_t bytes_read;
                while ((bytes_read = read(fd, send_buffer, sizeof(send_buffer))) > 0) {
                    ssize_t bytes_sent = 0;
                    while (bytes_sent < bytes_read) {
                        ssize_t sent = send(client_sock, send_buffer + bytes_sent, bytes_read - bytes_sent, 0);
                        if (sent == -1) {
                            syslog(LOG_ERR, "Failed to send data: %s", strerror(errno));
                            connection_error = true;
                            break;
                        }
                        bytes_sent += sent;
                    }
                    if (connection_error) {
                        break;
                    }
                }

                if (bytes_read == -1) {
                    syslog(LOG_ERR, "Failed to read file: %s", strerror(errno));
                    connection_error = true;
                    break;
                }

                // Seek back to end for next append
                if (lseek(fd, 0, SEEK_END) == -1) {
                    syslog(LOG_ERR, "Failed to seek to end of file: %s", strerror(errno));
                }

                // Remove processed packet from buffer
                memmove(data, data + packet_size, data_length - packet_size);
                data_length -= packet_size;
                search_start = data;
            }
        }

        free(data);
        close(client_sock);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    // Cleanup
    close(sock);
    close(fd);
    unlink(WRITE_FILE);
    closelog();

    return ret;
}
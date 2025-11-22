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
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>

#define TAG "aesdsocket"
#ifdef USE_AESD_CHAR_DEVICE
#define WRITE_FILE "/dev/aesdchar"
#else
#define WRITE_FILE "/var/tmp/aesdsocketdata"
#endif
#define PORT 9000
#define BUFFER_SIZE 4096

struct thread_data_s
{
    bool finished;
    bool joined;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int client_sock;
    pthread_t thread;
    int fd;
    pthread_mutex_t *mutex;
    SLIST_ENTRY(thread_data_s)
    entries;
};
SLIST_HEAD(thread_data_head_t, thread_data_s);

struct timestamp_data_s
{
    int fd;
    pthread_mutex_t *mutex;
};

static volatile sig_atomic_t exit_flag = false;

void signal_handler(int signal)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = true;
}

void *connection_thread(void *data)
{

    struct thread_data_s *thread_data = (struct thread_data_s *)data;

    // Open data file
    thread_data->fd = open(WRITE_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (thread_data->fd < 0)
    {
        syslog(LOG_ERR, "Failed to open file: %s, %s", WRITE_FILE, strerror(errno));
        close(thread_data->client_sock);
        thread_data->finished = true;
        return data;
    }

    char *client_ip = inet_ntoa(thread_data->client_addr.sin_addr);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    // Allocate receive buffer
    char *buffer = malloc(BUFFER_SIZE);
    if (buffer == NULL)
    {
        syslog(LOG_ERR, "Failed to allocate memory for receive buffer");
        close(thread_data->client_sock);
        close(thread_data->fd);
        thread_data->finished = true;
        return data;
    }
    size_t buffer_length = 0;
    size_t buffer_capacity = BUFFER_SIZE;

    // Receive buffer loop
    bool connection_error = false;
    while (!exit_flag && !connection_error)
    {
        // Check if we need to expand buffer
        if (buffer_length >= buffer_capacity)
        {
            size_t new_capacity = buffer_capacity * 2;
            char *new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == NULL)
            {
                syslog(LOG_ERR, "Failed to reallocate memory, discarding packet");
                connection_error = true;
                break;
            }
            buffer = new_buffer;
            buffer_capacity = new_capacity;
        }

        int count = recv(thread_data->client_sock, buffer + buffer_length, buffer_capacity - buffer_length, 0);

        if (count == -1)
        {
            syslog(LOG_ERR, "Socket error: %s", strerror(errno));
            connection_error = true;
            break;
        }

        if (count == 0)
        {
            // Client closed connection
            break;
        }

        buffer_length += count;

        // Check for complete packets (terminated by newline)
        char *search_start = buffer;
        char *end_of_packet;
        while ((end_of_packet = memchr(search_start, '\n', buffer_length - (search_start - buffer))) != NULL)
        {
            int packet_size = end_of_packet - buffer + 1;

            // Write packet to file
            if (0 != pthread_mutex_lock(thread_data->mutex))
            {
                syslog(LOG_ERR, "Failed to lock mutex");
                connection_error = true;
                break;
            }

            if (write(thread_data->fd, buffer, packet_size) == -1)
            {
                syslog(LOG_ERR, "Failed to write data to file: %s", strerror(errno));
                connection_error = true;
                if (0 != pthread_mutex_unlock(thread_data->mutex))
                {
                    syslog(LOG_ERR, "Failed to unlock mutex");
                }
                break;
            }

            if (lseek(thread_data->fd, 0, SEEK_SET) == -1)
            {
                syslog(LOG_ERR, "Failed to seek to beginning of file: %s", strerror(errno));
                connection_error = true;
                if (0 != pthread_mutex_unlock(thread_data->mutex))
                {
                    syslog(LOG_ERR, "Failed to unlock mutex");
                }
                break;
            }

            char send_buffer[1024];
            ssize_t bytes_read;
            while ((bytes_read = read(thread_data->fd, send_buffer, sizeof(send_buffer))) > 0)
            {
                ssize_t bytes_sent = 0;
                while (bytes_sent < bytes_read)
                {
                    ssize_t sent = send(thread_data->client_sock, send_buffer + bytes_sent, bytes_read - bytes_sent, 0);
                    if (sent == -1)
                    {
                        syslog(LOG_ERR, "Failed to send buffer: %s", strerror(errno));
                        connection_error = true;
                        break;
                    }
                    bytes_sent += sent;
                }
                if (connection_error)
                {
                    if (0 != pthread_mutex_unlock(thread_data->mutex))
                    {
                        syslog(LOG_ERR, "Failed to unlock mutex");
                    }
                    break;
                }
            }

            if (0 != pthread_mutex_unlock(thread_data->mutex))
            {
                syslog(LOG_ERR, "Failed to unlock mutex");
                connection_error = true;
                break;
            }

            if (bytes_read == -1)
            {
                syslog(LOG_ERR, "Failed to read file: %s", strerror(errno));
                connection_error = true;
                break;
            }

            // Remove processed packet from buffer
            memmove(buffer, buffer + packet_size, buffer_length - packet_size);
            buffer_length -= packet_size;
            search_start = buffer;
        }
    }

    free(buffer);
    close(thread_data->client_sock);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(thread_data->fd);
    thread_data->fd = 0;
    thread_data->finished = true;

    return data;
}

#ifndef USE_AESD_CHAR_DEVICE
void *timestamp_thread(void *data)
{
    struct timestamp_data_s *timestamp_data = (struct timestamp_data_s *)data;

    struct timespec next_time;
    clock_gettime(CLOCK_MONOTONIC, &next_time);

    while (!exit_flag)
    {
        next_time.tv_sec += 10;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, NULL) == EINTR && !exit_flag)
        {
        }

        if (exit_flag)
        {
            break;
        }

        pthread_mutex_lock(timestamp_data->mutex);
        char timestamp[200];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

        ssize_t written = write(timestamp_data->fd, timestamp, strlen(timestamp));
        if (written == -1)
        {
            syslog(LOG_ERR, "Failed to write timestamp: %s", strerror(errno));
        }

        pthread_mutex_unlock(timestamp_data->mutex);
    }

    return NULL;
}
#endif

int main(int argc, char **argv)
{
    bool daemon_mode = false;
    int sock = -1;
    int ret = 0;

    // Parse command line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = true;
    }

    openlog(TAG, 0, LOG_USER);

    // Register signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1)
    {
        syslog(LOG_ERR, "Failed to register signal handlers");
        return -1;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Set socket options to allow reuse of address
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
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
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        syslog(LOG_ERR, "Failed to bind to port %d: %s", PORT, strerror(errno));
        close(sock);
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
            close(sock);
            return -1;
        }
        if (pid > 0)
        {
            // Parent process exits
            exit(0);
        }
        // Child process continues
        if (setsid() == -1)
        {
            syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
            close(sock);
            return -1;
        }
        // Change working directory to root
        if (chdir("/") == -1)
        {
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
    if (listen(sock, 10) == -1)
    {
        syslog(LOG_ERR, "Failed to listen: %s", strerror(errno));
        close(sock);
        return -1;
    }

    // Linked list head
    struct thread_data_head_t thread_data_head;
    SLIST_INIT(&thread_data_head);
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);

    // Start timestamp thread

#ifndef USE_AESD_CHAR_DEVICE

    pthread_t timestamp_pthread;
    struct timestamp_data_s timestamp_data;
    timestamp_data.fd = open(WRITE_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (timestamp_data.fd < 0)
    {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        close(sock);
        return -1;
    }
    timestamp_data.mutex = &mutex;

    if (0 != pthread_create(&timestamp_pthread, 0, timestamp_thread, (void *)&timestamp_data))
    {
        syslog(LOG_ERR, "Failed to create timestamp thread");
        close(timestamp_data.fd);
        close(sock);
        return -1;
    }
#endif

    // Main server loop
    while (!exit_flag)
    {
        // Make a file descriptor for accept select
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // Create a select timeout for polling exit_flag
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 100000;

        // Wait for connection or timeout
        int select_ret = select(sock + 1, &readfds, NULL, NULL, &timeout);
        if (select_ret == -1)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal, check exit_flag
                continue;
            }
            syslog(LOG_ERR, "select() failed: %s", strerror(errno));
            break;
        }
        if (select_ret == 0)
        {
            // Timeout - no connection available, loop back to check exit_flag
            continue;
        }

        // Create a new node
        struct thread_data_s *new_thread_data = malloc(sizeof(struct thread_data_s));

        new_thread_data->finished = false;
        new_thread_data->joined = false;
        memset(&new_thread_data->client_addr, 0, sizeof(new_thread_data->client_addr));
        new_thread_data->client_addr_len = sizeof(new_thread_data->client_addr);
        new_thread_data->client_sock = accept(sock, (struct sockaddr *)&new_thread_data->client_addr, &new_thread_data->client_addr_len);
        new_thread_data->mutex = &mutex;

        if (new_thread_data->client_sock == -1)
        {
            free(new_thread_data);
            if (exit_flag)
            {
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        // Spawn a new thread
        if (0 != pthread_create(&new_thread_data->thread, 0, connection_thread, (void *)new_thread_data))
        {
            syslog(LOG_ERR, "Failed to create new thread");
            close(new_thread_data->client_sock);
            free(new_thread_data);
            continue;
        }

        // Add the new thread data to the linked list
        SLIST_INSERT_HEAD(&thread_data_head, new_thread_data, entries);

        // Cleanup any dead threads
        struct thread_data_s *current;
        SLIST_FOREACH(current, &thread_data_head, entries)
        {
            // Join any finished threads
            if (current->finished && !current->joined)
            {
                pthread_join(current->thread, NULL);
                current->joined = true;
            }
        }
        while (true)
        {
            // Safely delete any joined thread at the top of the list
            struct thread_data_s *first = SLIST_FIRST(&thread_data_head);
            if (first != NULL && first->joined)
            {
                SLIST_REMOVE_HEAD(&thread_data_head, entries);
                free(first);
            }
            else
            {
                break;
            }
        }
    }

    shutdown(sock, SHUT_RDWR);
    // Cleanup

#ifndef USE_AESD_CHAR_DEVICE
    pthread_join(timestamp_pthread, NULL);
    close(timestamp_data.fd);
#endif

    // Join remaining connections
    while (true)
    {
        // Join and delete from the head
        struct thread_data_s *first = SLIST_FIRST(&thread_data_head);
        if (first != NULL)
        {
            if (!first->joined)
            {
                pthread_join(first->thread, NULL);
                first->joined = true;
            }
            SLIST_REMOVE_HEAD(&thread_data_head, entries);
            free(first);
        }
        else
        {
            break;
        }
    }

    pthread_mutex_destroy(&mutex);
    close(sock);

#ifndef USE_AESD_CHAR_DEVICE
    // Only delete the file if we are using the temp file
    unlink(WRITE_FILE);
#endif

    closelog();

    return ret;
}
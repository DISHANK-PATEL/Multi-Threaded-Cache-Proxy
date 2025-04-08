#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define CACHE_SIZE 5

// Structure for cache entry
typedef struct CacheEntry {
    char url[256];
    char *data;
    size_t size;
    struct CacheEntry *prev, *next;
} CacheEntry;

// Cache variables
CacheEntry *cache_head = NULL, *cache_tail = NULL;
int cache_count = 0;
pthread_mutex_t cache_lock;

// Utility to print headers for logs
void print_log_headers() {
    printf("\n=====================================================\n");
    printf("| Step                          | URL                          |\n");
    printf("|-------------------------------|-------------------------------|\n");
}

// Utility to log steps
void log_step(const char *step, const char *url) {
    printf("| %-29s | %-30s |\n", step, url);
}

// Function to remove a cache entry
void remove_cache_entry(CacheEntry *entry) {
    if (!entry) return;

    if (entry->prev) entry->prev->next = entry->next;
    else cache_head = entry->next;

    if (entry->next) entry->next->prev = entry->prev;
    else cache_tail = entry->prev;

    free(entry->data);
    free(entry);
    cache_count--;
}

// Function to add a new entry to the cache
void add_to_cache(const char *url, const char *data, size_t size) {
    if (cache_count == CACHE_SIZE) {
        remove_cache_entry(cache_tail);
    }

    CacheEntry *new_entry = (CacheEntry *)malloc(sizeof(CacheEntry));
    strncpy(new_entry->url, url, sizeof(new_entry->url));
    new_entry->data = (char *)malloc(size);
    memcpy(new_entry->data, data, size);
    new_entry->size = size;
    new_entry->prev = NULL;
    new_entry->next = cache_head;

    if (cache_head) cache_head->prev = new_entry;
    else cache_tail = new_entry;

    cache_head = new_entry;
    cache_count++;
}

// Function to find a URL in the cache
CacheEntry *find_in_cache(const char *url) {
    CacheEntry *current = cache_head;
    while (current) {
        if (strcmp(current->url, url) == 0) {
            // Move to head (LRU policy)
            if (current != cache_head) {
                if (current->prev) current->prev->next = current->next;
                if (current->next) current->next->prev = current->prev;
                else cache_tail = current->prev;

                current->next = cache_head;
                current->prev = NULL;
                if (cache_head) cache_head->prev = current;
                cache_head = current;
            }
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Function to fetch data from the server
char *fetch_from_server(const char *url) {
    const char *server_ip = "93.184.216.34"; // Example.com IP
    int server_port = 80;

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        return NULL;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(server_socket);
        return NULL;
    }

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: example.com\r\n\r\n", url);
    log_step("Proxy: Request Sent To", url);
    send(server_socket, request, strlen(request), 0);

    char *response = (char *)malloc(BUFFER_SIZE);
    size_t total_size = 0, received;
    while ((received = recv(server_socket, response + total_size, BUFFER_SIZE - total_size, 0)) > 0) {
        total_size += received;
        response = (char *)realloc(response, total_size + BUFFER_SIZE);
    }
    log_step("Proxy: Received Response From", url);

    close(server_socket);
    return response;
}

// Function to handle client requests
void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE], url[256];
    read(client_socket, buffer, sizeof(buffer));
    sscanf(buffer, "GET %s HTTP", url);

    log_step("Received Request For", url);

    pthread_mutex_lock(&cache_lock);

    CacheEntry *entry = find_in_cache(url);
    if (entry) {
        log_step("Cache Check For", url);
        log_step("Cache Hit", url);
        pthread_mutex_unlock(&cache_lock);

        send(client_socket, entry->data, entry->size, 0);
    } else {
        log_step("Cache Check For", url);
        log_step("Cache Miss", url);
        pthread_mutex_unlock(&cache_lock);

        log_step("Fetching From Server", url);
        char *response = fetch_from_server(url);

        pthread_mutex_lock(&cache_lock);
        add_to_cache(url, response, strlen(response));
        log_step("Cached Response For", url);
        pthread_mutex_unlock(&cache_lock);

        send(client_socket, response, strlen(response), 0);
        free(response);
    }

    log_step("Response Sent To Client From Proxy", url);
    close(client_socket);
    log_step("Request Processing Completed", url);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    pthread_mutex_init(&cache_lock, NULL);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) < 0) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Proxy server is running on port %d...\n", port);
    print_log_headers();

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("Failed to accept connection");
            continue;
        }

        int *client_socket_ptr = malloc(sizeof(int));
        *client_socket_ptr = client_socket;

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_socket_ptr);
        pthread_detach(thread);
    }

    close(server_socket);
    pthread_mutex_destroy(&cache_lock);

    return 0;
}

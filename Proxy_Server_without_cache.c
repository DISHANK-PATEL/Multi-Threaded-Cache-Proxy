#include "proxy_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>

#define MAX_BYTES         4096         // Maximum allowed size of request/response
#define MAX_CLIENTS       400          // Maximum number of client requests served concurrently
#define MAX_CACHE_SIZE    (200 * (1 << 20))  // Total cache size (in bytes)
#define MAX_ELEMENT_SIZE  (10 * (1 << 20))   // Maximum size of an individual cache element

// --- Cache Element Structure ---
typedef struct cache_element {
    char *data;                // Cached response data
    int len;                   // Length of the data
    char *url;                 // Request URL used as key
    time_t lru_time_track;     // Timestamp for LRU tracking
    struct cache_element *next;  // Pointer to next cache element
} cache_element;

// --- Global Variables ---
pthread_mutex_t cache_lock;           // Mutex for cache synchronization
sem_t semaphore;                      // Semaphore to limit concurrent client threads

cache_element *cache_head = NULL;     // Pointer to the head of the cache linked list
int cache_current_size = 0;           // Current total size of the cache

int port_number = 8080;               // Default proxy port number
int proxy_socketId;                   // Proxy server socket descriptor

pthread_t tid[MAX_CLIENTS];           // Array to store thread IDs for client threads

// --- Function Prototypes ---
cache_element *cache_find(const char *url);
int cache_add_element(char *data, int size, char *url);
void cache_remove_lru_element(void);
int sendErrorMessage(int socket, int status_code);
int connectRemoteServer(const char *host_addr, int port_num);
int handle_request(int clientSocket, struct ParsedRequest *request, char *buf, char *tempReq);
int checkHTTPversion(const char *msg);
void *thread_fn(void *socket_ptr);

// --- Function Implementations ---

/* 
 * sendErrorMessage - Sends an HTTP error message to the client.
 */
int sendErrorMessage(int socket, int status_code) {
    char response[1024];
    char currentTime[50];
    time_t now = time(NULL);
    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code) {
        case 400:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\n"
                     "Connection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\n"
                     "Server: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n"
                     "<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
            printf("400 Bad Request\n");
            break;
        case 403:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\n"
                     "Connection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n"
                     "<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
            printf("403 Forbidden\n");
            break;
        case 404:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\n"
                     "Connection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n"
                     "<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
            printf("404 Not Found\n");
            break;
        case 500:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\n"
                     "Connection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\n"
                     "Server: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n"
                     "<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
            send(socket, response, strlen(response), 0);
            return 1;
        case 501:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\n"
                     "Connection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\n"
                     "Server: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n"
                     "<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
            printf("501 Not Implemented\n");
            break;
        case 505:
            snprintf(response, sizeof(response),
                     "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\n"
                     "Connection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\n"
                     "Server: VaibhavN/14785\r\n\r\n"
                     "<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n"
                     "<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
            printf("505 HTTP Version Not Supported\n");
            break;
        default:
            return -1;
    }
    send(socket, response, strlen(response), 0);
    return 1;
}

/*
 * connectRemoteServer - Creates a socket and connects to a remote server given its hostname and port.
 */
int connectRemoteServer(const char *host_addr, int port_num) {
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0) {
        perror("Error creating remote socket");
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL) {
        fprintf(stderr, "No such host exists: %s\n", host_addr);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to remote server");
        return -1;
    }
    return remoteSocket;
}

/*
 * handle_request - Processes a client's request by forwarding it to the remote server
 * and then sending the response back to the client.
 */
int handle_request(int clientSocket, struct ParsedRequest *request, char *buf, char *tempReq) {
    // Construct the request line
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    // Ensure the "Connection" header is set to "close"
    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Failed to set Connection header\n");
    }

    // Ensure the "Host" header exists
    if (ParsedHeader_get(request, "Host") == NULL) {
        if (ParsedHeader_set(request, "Host", request->host) < 0) {
            printf("Failed to set Host header\n");
        }
    }

    // Unparse the headers and append them to the buffer
    if (ParsedRequest_unparse_headers(request, buf + len, MAX_BYTES - len) < 0) {
        printf("Unparsing headers failed, sending request without header\n");
    }

    int server_port = 80; // Default remote server port
    if (request->port != NULL)
        server_port = atoi(request->port);

    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0)
        return -1;

    int bytes_sent = send(remoteSocketID, buf, strlen(buf), 0);
    if (bytes_sent < 0) {
        perror("Error sending request to remote server");
        close(remoteSocketID);
        return -1;
    }

    memset(buf, 0, MAX_BYTES);
    bytes_sent = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    // Temporary buffer to hold the full response for caching
    char *temp_buffer = (char *)malloc(MAX_BYTES);
    if (!temp_buffer) {
        perror("malloc failed");
        close(remoteSocketID);
        return -1;
    }
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_sent > 0) {
        if (send(clientSocket, buf, bytes_sent, 0) < 0) {
            perror("Error sending data to client");
            break;
        }
        // Append received data to temporary buffer for caching
        for (int i = 0; i < bytes_sent; i++) {
            temp_buffer[temp_buffer_index++] = buf[i];
        }
        // Reallocate temp_buffer if needed
        if (temp_buffer_index + MAX_BYTES > temp_buffer_size) {
            temp_buffer_size += MAX_BYTES;
            temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
            if (!temp_buffer) {
                perror("realloc failed");
                break;
            }
        }
        memset(buf, 0, MAX_BYTES);
        bytes_sent = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';

    // Optionally add the response to cache (currently commented out)
    // add_cache_element(temp_buffer, temp_buffer_index, tempReq);

    printf("Done forwarding request\n");

    free(temp_buffer);
    free(tempReq);
    close(remoteSocketID);
    return 0;
}

/*
 * checkHTTPversion - Checks if the provided HTTP version is supported.
 */
int checkHTTPversion(const char *msg) {
    if (strncmp(msg, "HTTP/1.1", 8) == 0)
        return 1;
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
        return 1; // Treat 1.0 similar to 1.1
    else
        return -1;
}

/*
 * thread_fn - Function executed by each client-handling thread.
 */
void *thread_fn(void *socket_ptr) {
    sem_wait(&semaphore);
    int sem_val;
    sem_getvalue(&semaphore, &sem_val);
    printf("Semaphore value after wait: %d\n", sem_val);

    int clientSocket = *(int *)socket_ptr;
    int bytes_received, len;
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    if (!buffer) {
        perror("calloc failed");
        close(clientSocket);
        sem_post(&semaphore);
        return NULL;
    }
    memset(buffer, 0, MAX_BYTES);

    // Receive client request
    bytes_received = recv(clientSocket, buffer, MAX_BYTES, 0);
    while (bytes_received > 0) {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL) {
            bytes_received = recv(clientSocket, buffer + len, MAX_BYTES - len, 0);
        } else {
            break;
        }
    }

    // Duplicate the request buffer to use as key for cache lookup
    char *tempReq = (char *)malloc(strlen(buffer) + 10);
    if (!tempReq) {
        perror("malloc failed for tempReq");
        free(buffer);
        close(clientSocket);
        sem_post(&semaphore);
        return NULL;
    }
    strcpy(tempReq, buffer);

    // Check if the request exists in cache
    cache_element *cache_entry = cache_find(tempReq);
    if (cache_entry != NULL) {
        // Serve from cache
        int data_size = cache_entry->len;
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < data_size) {
            memset(response, 0, MAX_BYTES);
            int chunk = ((data_size - pos) < MAX_BYTES) ? (data_size - pos) : MAX_BYTES;
            memcpy(response, cache_entry->data + pos, chunk);
            send(clientSocket, response, chunk, 0);
            pos += chunk;
        }
        printf("Data retrieved from the cache\n");
    } else if (bytes_received > 0) {
        // Parse the HTTP request
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, strlen(buffer)) < 0) {
            printf("Parsing failed\n");
        } else {
            memset(buffer, 0, MAX_BYTES);
            if (strcmp(request->method, "GET") == 0) {
                if (request->host && request->path && (checkHTTPversion(request->version) == 1)) {
                    if (handle_request(clientSocket, request, buffer, tempReq) == -1)
                        sendErrorMessage(clientSocket, 500);
                } else {
                    sendErrorMessage(clientSocket, 500);
                }
            } else {
                printf("Only GET method is supported\n");
            }
        }
        ParsedRequest_destroy(request);
    } else if (bytes_received < 0) {
        perror("Error receiving from client");
    } else if (bytes_received == 0) {
        printf("Client disconnected!\n");
    }

    shutdown(clientSocket, SHUT_RDWR);
    close(clientSocket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &sem_val);
    printf("Semaphore value after post: %d\n", sem_val);
    return NULL;
}

/*
 * main - Entry point for the proxy server.
 */
int main(int argc, char *argv[]) {
    if (argc == 2)
        port_number = atoi(argv[1]);
    else {
        printf("Usage: %s <port_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Setting Proxy Server Port: %d\n", port_number);

    // Initialize semaphore and mutex
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&cache_lock, NULL);

    // Create proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0) {
        perror("Failed to create proxy socket");
        exit(EXIT_FAILURE);
    }
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // Setup server address
    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Port is not free");
        exit(EXIT_FAILURE);
    }
    printf("Binding on port: %d\n", port_number);

    if (listen(proxy_socketId, MAX_CLIENTS) < 0) {
        perror("Error while listening");
        exit(EXIT_FAILURE);
    }

    int client_socketId, client_len;
    int clientSockets[MAX_CLIENTS];
    int thread_index = 0;

    // Infinite loop for accepting client connections
    while (1) {
        memset(&client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        if (client_socketId < 0) {
            fprintf(stderr, "Error in accepting connection!\n");
            exit(EXIT_FAILURE);
        }
        clientSockets[thread_index] = client_socketId;

        // Display client IP address (optional)
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        //printf("Client connected: IP %s, Port %d\n", client_ip, ntohs(client_addr.sin_port));

        pthread_create(&tid[thread_index], NULL, thread_fn, (void *)&clientSockets[thread_index]);
        thread_index++;
    }

    close(proxy_socketId);
    return 0;
}

/*
 * cache_find - Searches for the cache element corresponding to the given URL.
 */
cache_element *cache_find(const char *url) {
    pthread_mutex_lock(&cache_lock);
    cache_element *curr = cache_head;
    while (curr != NULL) {
        if (strcmp(curr->url, url) == 0) {
            printf("Cache hit for url: %s\n", url);
            curr->lru_time_track = time(NULL);
            pthread_mutex_unlock(&cache_lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&cache_lock);
    printf("Cache miss for url: %s\n", url);
    return NULL;
}

/*
 * cache_remove_lru_element - Removes the least recently used element from the cache.
 */
void cache_remove_lru_element(void) {
    pthread_mutex_lock(&cache_lock);
    if (cache_head == NULL) {
        pthread_mutex_unlock(&cache_lock);
        return;
    }
    cache_element *prev = cache_head, *curr = cache_head, *lru_prev = cache_head, *lru = cache_head;
    while (curr->next != NULL) {
        if (curr->next->lru_time_track < lru->lru_time_track) {
            lru = curr->next;
            lru_prev = curr;
        }
        curr = curr->next;
    }

    if (lru == cache_head) {
        cache_head = cache_head->next;
    } else {
        lru_prev->next = lru->next;
    }
    cache_current_size -= (lru->len + 1 + strlen(lru->url) + sizeof(cache_element));
    free(lru->data);
    free(lru->url);
    free(lru);
    pthread_mutex_unlock(&cache_lock);
}

/*
 * cache_add_element - Adds a new element to the cache after making sure there is enough space.
 */
int cache_add_element(char *data, int size, char *url) {
    printf("\nAdding to cache, url: %s\nData: %s\n", url, data);
    pthread_mutex_lock(&cache_lock);
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);
    
    // Uncomment or modify the next block to enforce maximum element size if required
    /*
    if (element_size > MAX_ELEMENT_SIZE) {
        pthread_mutex_unlock(&cache_lock);
        return 0;
    }
    */

    while (cache_current_size + element_size > MAX_CACHE_SIZE) {
        cache_remove_lru_element();
    }

    cache_element *new_element = (cache_element *)malloc(sizeof(cache_element));
    if (!new_element) {
        perror("malloc failed for cache element");
        pthread_mutex_unlock(&cache_lock);
        return 0;
    }

    new_element->data = (char *)malloc(size + 10);
    if (!new_element->data) {
        free(new_element);
        perror("malloc failed for cache data");
        pthread_mutex_unlock(&cache_lock);
        return 0;
    }
    strcpy(new_element->data, data);

    new_element->url = (char *)malloc(10 + strlen(url) * sizeof(char));
    if (!new_element->url) {
        free(new_element->data);
        free(new_element);
        perror("malloc failed for cache URL");
        pthread_mutex_unlock(&cache_lock);
        return 0;
    }
    strcpy(new_element->url, url);
    new_element->lru_time_track = time(NULL);
    new_element->len = size;
    new_element->next = cache_head;
    cache_head = new_element;
    cache_current_size += element_size;
    pthread_mutex_unlock(&cache_lock);

    free(url);
    free(data);

    printf("\nCurrent cache size: %d bytes\n", cache_current_size);
    return 1;
}

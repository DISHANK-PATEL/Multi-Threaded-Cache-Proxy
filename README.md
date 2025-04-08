# Multi-Threaded Proxy Server with Cache

This project implements a Multi-Threaded Proxy Server in **C** and **C++** with HTTP request parsing and caching capabilities based on an LRU algorithm. It demonstrates essential OS concepts such as threading, semaphore-based synchronization, locks, and caching. The HTTP parsing functionality is integrated from a Proxy Server parsing library.

---

## Index

- [Project Theory](#project-theory)
- [How to Run](#how-to-run)
- [Demo](#demo)
- [Contributing](#contributing)

---

## Project Theory

### Introduction
The proxy server functions as an intermediary between client requests and remote servers. The implementation focuses on understanding:
- The flow of requests from a local computer to a remote server.
- Handling multiple client requests concurrently.
- Locking and synchronization to ensure safe concurrency.
- Caching techniques and their potential use in browsers.

### Basic Working Flow of the Proxy Server
1. **Client Request**: Clients send HTTP requests to the proxy server.
2. **Parsing HTTP Requests**: The incoming request is parsed using the provided HTTP parsing library.
3. **Multi-Threading**: Each client request is served by a new thread using semaphores for synchronization.
4. **Cache Lookup**: Before forwarding the request, the proxy checks if the response for the URL is already cached.
5. **Request Forwarding**: If the cache does not contain the requested URL, the proxy connects to the remote server, forwards the request, and relays the response back to the client while caching it.
6. **Response Delivery**: The response is either sent directly from the cache (on a cache hit) or after fetching from the remote server.
7. **Cache Replacement Policy**: The cache employs an LRU algorithm to remove the least recently used entries when the cache size limit is reached.

### How did we implement Multi-threading?
- **Threading Model**: Multiple threads are created to handle individual client requests.
- **Semaphore-based Synchronization**:
  - Uses `sem_wait()` and `sem_post()` for thread synchronization instead of condition variables.
  - **Advantage**: Semaphores do not require passing thread IDs (unlike `pthread_join()`), offering a simpler interface.
- **Locking**: Mutex locks are used to ensure safe concurrent access to cache data.

### Motivation/Need of Project
- To gain insight into the behavior of HTTP requests from a local machine to a server.
- To understand handling multiple client requests simultaneously.
- To explore concurrency and locking mechanisms.
- To learn about caching techniques and how various browsers might leverage them.

### What does the Proxy Server do?
- **Speed and Efficiency**: Speeds up responses by caching frequently accessed content and reducing server load.
- **Access Restrictions**: Can be configured to restrict access to specific websites.
- **Anonymization**: A good proxy can hide the client’s IP address from the remote server.
- **Extensibility**: Potential to encrypt requests to prevent unauthorized access.

### OS Components Used
- **Threading**: Handles concurrent client requests.
- **Locks**: Ensures safe access to shared data structures.
- **Semaphore**: Limits the number of concurrent client threads.
- **Cache**: Implements a caching mechanism using the LRU algorithm.

### Limitations
- **Cache Duplication**: If a URL opens multiple client connections simultaneously, the cache may store separate responses for each client. This could result in incomplete responses when retrieving from the cache.
- **Fixed Cache Element Size**: Large websites may not be fully stored in the cache due to a fixed maximum element size.

=

---

## How to Run

Follow these steps to build and run the proxy server:

1. **Clone the Repository**
   ```bash
   git clone

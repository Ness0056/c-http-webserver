#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include "data.h"
#include "http.h"
#include "util.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
// resolves host and port into a sockaddr
static struct sockaddr_in derive_sockaddr(const char *host, const char *port);

#define MAX_RESOURCES 100
#define MAX_CLIENTS 32

// static in-memory resources
struct tuple resources[MAX_RESOURCES] = {
    {"/static/foo", "Foo", sizeof "Foo" - 1}, 
    {"/static/bar", "Bar", sizeof "Bar" - 1}, 
    {"/static/baz", "Baz", sizeof "Baz" - 1}
};

// cache entry for hash -> node lookup
typedef struct {
    unsigned int hash_id;
    unsigned int node_id;
    char port[6];
    char ip[INET_ADDRSTRLEN];
} hash_cache_entry;
// small lookup cache
hash_cache_entry hash_table[10];  // Small cache, might need to increase this
const hash_cache_entry *lookup_hash_cache(unsigned int hash_id); 

// local node identity
int node_ID;
char *Node_IP;
char *Node_Port;

// ring neighbors
int PRED_ID;
int SUCC_ID;
char *PRED_IP;
char *SUCC_IP;
char *PRED_PORT;
char *SUCC_PORT;

int dht_enabled = 0;
int cache_count = 0;

// check if this node is responsible for a given hash
// note: handles wraparound case where PRED_ID > node_ID
bool is_responsible_for_hash(unsigned int hash_id, unsigned int pred_id, unsigned int current_node_id) {
    if (pred_id < current_node_id) {
        return (hash_id <= current_node_id && hash_id > pred_id);
    } else {
        // wraparound case
        return (hash_id > pred_id || hash_id <= current_node_id);
    }
}



// packs a lookup request into a udp buffer
static void prepare_lookup_message(uint8_t *buf, uint16_t hash, uint16_t sender_id)
{
    buf[0] = 0;  // Message type: LOOKUP
    buf[1] = (hash >> 8) & 0xff;
    buf[2] = (hash & 0xff);
    buf[3] = (sender_id >> 8) & 0xff;
    buf[4] = sender_id & 0xff;
}
// handles http verbs against local storage
int handle_http_method(const struct request *request, char *reply, size_t *offset, 
                       struct tuple *resources, size_t max_resources) {
    size_t resource_length;
    const char *resource;
    if (strcmp(request->method, "GET") == 0) {
        resource = get(request->uri, resources, max_resources, &resource_length);
        if (resource) {
            *offset = sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n", 
                            resource_length);
            memcpy(reply + *offset, resource, resource_length);
            *offset += resource_length;
        } else {
            strcpy(reply, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            *offset = strlen(reply);
        }
    }
    else if (strcmp(request->method, "PUT") == 0) {
        if (set(request->uri, request->payload, request->payload_length, resources, max_resources)) {
            strcpy(reply, "HTTP/1.1 204 No Content\r\n\r\n");  // Updated
        } else { 
            strcpy(reply, "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n");  // Created new
        }
        *offset = strlen(reply);
    }
    else if (strcmp(request->method, "DELETE") == 0) {
        if (delete(request->uri, resources, max_resources)) {
            strcpy(reply, "HTTP/1.1 204 No Content\r\n\r\n");
        } else {
            strcpy(reply, "HTTP/1.1 404 Not Found\r\n\r\n");
        }
        *offset = strlen(reply);
    }
    else {
        strcpy(reply, "HTTP/1.1 501 Method Not Supported\r\n\r\n");
        *offset = strlen(reply);
    }
    
    return 0;
}
// routes an http request either locally or through the dht
void send_reply(int conn, struct request *request, int udp_sock) {
    size_t offset = 0;
    char msg_buffer[HTTP_MAX_SIZE];
    char *reply = msg_buffer;
    if (!dht_enabled) {
        handle_http_method(request, reply, &offset, resources, MAX_RESOURCES);
        send(conn, reply, offset, 0);
        return;
    }
    uint16_t uri_hash;
    char *uri =request->uri;
    uri_hash= pseudo_hash((unsigned char *)uri, strlen(uri));
    const hash_cache_entry *cached = lookup_hash_cache(uri_hash);
    
    if (cached) {
        char redirect_msg[1000];
        sprintf(redirect_msg, 
                "HTTP/1.1 303 See Other\r\nLocation: http://%s:%s%s\r\nContent-Length: 0\r\n\r\n",
                cached->ip, cached->port, uri);
        send(conn, redirect_msg, strlen(redirect_msg), 0);
        return;
    }
    bool we_are_responsible = is_responsible_for_hash(uri_hash, PRED_ID, node_ID);

    if (!we_are_responsible) {
        bool succ_responsible = is_responsible_for_hash(uri_hash, node_ID, SUCC_ID);
        
        if (succ_responsible) {
            char redirect_msg[HTTP_MAX_SIZE];
            sprintf(redirect_msg, 
                    "HTTP/1.1 303 See Other\r\nLocation: http://%s:%s%s\r\nContent-Length: 0\r\n\r\n",
                    SUCC_IP, SUCC_PORT, request->uri);
            send(conn, redirect_msg, strlen(redirect_msg), 0);
            return;
        }
        char unavailable_response[1024]; 
        strcpy(unavailable_response, "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nContent-Length: 0\r\n\r\n");
        send(conn, unavailable_response, strlen(unavailable_response), 0);
        struct sockaddr_in succ_addr = derive_sockaddr(SUCC_IP, SUCC_PORT);
        uint8_t lookup_buf[11];
        memset(lookup_buf, 0, 11);
        prepare_lookup_message(lookup_buf, uri_hash, node_ID);
        uint32_t my_ip_network_order = inet_addr(Node_IP);
        memcpy(&lookup_buf[5], &my_ip_network_order, sizeof(my_ip_network_order));

        uint16_t my_port_network_order = htons((uint16_t)atoi(Node_Port));
        memcpy(&lookup_buf[9], &my_port_network_order, sizeof(my_port_network_order));
        
        sendto(udp_sock, lookup_buf, 11, 0, 
               (struct sockaddr *)&succ_addr, sizeof(succ_addr));
        return;
    }
    
    handle_http_method(request, reply, &offset, resources, MAX_RESOURCES);
    send(conn, reply, offset, 0);
}

ssize_t process_packet(int conn, char *buffer, size_t n, int socket) {
    struct request request = {
        .method = NULL, 
        .uri = NULL, 
        .payload = NULL, 
        .payload_length = -1
    };
    
    ssize_t bytes_processed = parse_request(buffer, n, &request);

    if (bytes_processed > 0) {
        send_reply(conn, &request, socket);
        
        const string connection_header = get_header(&request, "Connection");
        if (connection_header && strcmp(connection_header, "close") == 0) {
            return -1;
        }
    }
    else if (bytes_processed == -1) {
        const string bad_request = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(conn, bad_request, strlen(bad_request), 0);
        printf("Received malformed request, terminating connection.\n");
        close(conn);
        return -1;
    }

    return bytes_processed;
}

static void connection_setup(struct connection_state *state, int sock) {
    state->sock = sock;
    state->end = state->buffer;
    memset(state->buffer, 0, HTTP_MAX_SIZE);
}

char *buffer_discard(char *buffer, size_t discard, size_t keep) {
    memmove(buffer, buffer + discard, keep);
    memset(buffer + keep, 0, discard);
    return buffer + keep;
}

bool handle_connection(struct connection_state *state, int socket_udp) {
    const char *buffer_end = state->buffer + HTTP_MAX_SIZE;

    ssize_t bytes_read = recv(state->sock, state->end, buffer_end - state->end, 0);
    
    if (bytes_read == -1) {
        perror("recv");
        close(state->sock);
        exit(EXIT_FAILURE);
    }
    else if (bytes_read == 0) {
        return false;
    }

    char *window_start=state->buffer;
    char *window_end=state->end + bytes_read;

    ssize_t bytes_processed = 0;
    
    while ((bytes_processed = process_packet(state->sock, window_start,window_end - window_start, socket_udp)) > 0) {
        window_start += bytes_processed;
    }
    
    if (bytes_processed== -1) {
        return false;
    }

    state->end = buffer_discard(state->buffer, window_start - state->buffer, window_end - window_start);
    return true;
}

static struct sockaddr_in derive_sockaddr(const char *host, const char *port) {
    struct addrinfo hints = {.ai_family = AF_INET};
    struct addrinfo *result_info;

    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error parsing host/port\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in result = *((struct sockaddr_in *)result_info->ai_addr);
    freeaddrinfo(result_info);
    return result;
}

static int setup_server_socket(struct sockaddr_in addr) {
    const int enable = 1;
    const int backlog = 1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (listen(sock, backlog)) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return sock;
}

static int setup_udp_socket(struct sockaddr_in addr) {
    const int enable = 1;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    
    bind(s, (struct sockaddr *)&addr, sizeof(addr));
    return s;
}

void build_reply_message(uint8_t *reply_buf, uint8_t msg_type, uint16_t node_id, uint16_t successor_id, const char *successor_ip, const char *successor_port) {
    memset(reply_buf, 0, 11);
    reply_buf[0] = msg_type;
    reply_buf[1] = (node_id >> 8) & 255;
    reply_buf[2] = node_id & 255;
    reply_buf[3] = (successor_id >> 8) & 255;
    reply_buf[4] = successor_id & 255;
    uint32_t ip_network = inet_addr(successor_ip);
    memcpy(&reply_buf[5], &ip_network, sizeof(ip_network));
    uint16_t port_network = htons((uint16_t)atoi(successor_port));
    memcpy(&reply_buf[9], &port_network, sizeof(port_network));
}

const hash_cache_entry *lookup_hash_cache(unsigned int hash_id) {
    for (int i = 0; i < cache_count; ++i) {
        hash_cache_entry *entry = &hash_table[i];
        bool in_range;
        if (entry->hash_id < entry->node_id) {
            in_range = (hash_id > entry->hash_id && hash_id <= entry->node_id);
        } else {
            in_range = (hash_id > entry->hash_id || hash_id <= entry->node_id);
        }
        
        if (in_range) {
            return entry;
        }
    }
    
    return NULL;
}

void add_to_cache(unsigned int hash_id, unsigned int responsible_node, 
                  const char *ip, const char *port) {
    // simple FIFO eviction if cache is full
    if (cache_count >= 10) {
        memmove(hash_table, hash_table + 1, sizeof(hash_table[0]) * 9);
        cache_count = 9;
    }
    
    hash_cache_entry *new_entry = &hash_table[cache_count];
    new_entry->hash_id = hash_id;
    new_entry->node_id = responsible_node;
    
    strncpy(new_entry->ip, ip, INET_ADDRSTRLEN - 1);
    new_entry->ip[INET_ADDRSTRLEN - 1] = '\0';
    
    strncpy(new_entry->port, port, sizeof(new_entry->port) - 1);
    new_entry->port[sizeof(new_entry->port) - 1] = '\0';
    
    cache_count++;
}

int main(int argc, char **argv) {
    if (argc>4 || argc< 3) {
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr = derive_sockaddr(argv[1], argv[2]);
    if (argc == 4) {
        node_ID = atoi(argv[3]);
        Node_IP = argv[1];
        Node_Port = argv[2];
        PRED_ID = atoi(getenv("PRED_ID"));
        PRED_IP = getenv("PRED_IP");
        PRED_PORT = getenv("PRED_PORT");
        SUCC_ID = atoi(getenv("SUCC_ID"));
        SUCC_IP = getenv("SUCC_IP");
        SUCC_PORT = getenv("SUCC_PORT");

        dht_enabled = 1;
    }

    int server_socket = setup_server_socket(addr);
    int udp_socket = setup_udp_socket(addr);
    struct pollfd fds[2 + MAX_CLIENTS];
    struct connection_state states[MAX_CLIENTS];
    memset(fds, 0, sizeof(fds));
    memset(states, 0, sizeof(states));
    fds[0].fd = server_socket;
    fds[0].events = POLLIN;
    fds[1].fd = udp_socket;
    fds[1].events = POLLIN;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        fds[2+i].fd=-1;
        fds[2+i].events= 0;
    }
    while (1){
        int poll_result= poll(fds, 2 + MAX_CLIENTS, -1);
        if (poll_result< 0) {
            perror("poll");
            exit(EXIT_FAILURE);
        }
        if (fds[0].revents & POLLIN) {
            while (1) {
                int client_sock = accept(server_socket, NULL, NULL);
                if (client_sock < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    perror("accept");
                    break;
                }
                if (fcntl(client_sock, F_SETFL, O_NONBLOCK) == -1) {
                    perror("fcntl(client)");
                    close(client_sock);
                    continue;
                }

                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (fds[2 + i].fd == -1) {
                        slot = i;
                        break;
                    }
                }
                if (slot == -1) {
                    close(client_sock);
                }else {
                    connection_setup(&states[slot], client_sock);
                    fds[2+slot].fd = client_sock;
                    fds[2+slot].events= POLLIN;
                }
            }
        }
        if (fds[1].revents & POLLIN) {
            uint8_t udp_buf[11];
            struct sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);

            ssize_t recv_len = recvfrom(udp_socket, udp_buf, sizeof(udp_buf), 0,
                                        (struct sockaddr *)&sender_addr, &sender_len);
            
            if (recv_len > 0) {
                uint8_t msg_type = udp_buf[0];
                uint16_t hash_id = (udp_buf[1] << 8) | udp_buf[2];
                uint16_t sender_node_id = (udp_buf[3] << 8) | udp_buf[4];
                uint32_t sender_ip_raw;
                uint16_t sender_port_raw;
                memcpy(&sender_port_raw, &udp_buf[9], 2);
                memcpy(&sender_ip_raw, &udp_buf[5], 4);
                uint16_t sender_port_host = ntohs(sender_port_raw);
                char sender_ip_str[INET_ADDRSTRLEN];
                struct in_addr ip_struct = { sender_ip_raw };
                inet_ntop(AF_INET, &ip_struct, sender_ip_str, sizeof(sender_ip_str));

                if (msg_type == 1) {
                    char port_str[16];
                    snprintf(port_str, sizeof(port_str), "%u", sender_port_host);
                    add_to_cache(hash_id, sender_node_id, sender_ip_str, port_str);
                    
                } else if (msg_type == 0) {
                    bool we_handle_it = is_responsible_for_hash(hash_id, PRED_ID, node_ID);
                    if (!we_handle_it) {
                        if (hash_id <= SUCC_ID && hash_id > node_ID) {
                            uint8_t reply_buf[11];
                            build_reply_message(reply_buf, 1, node_ID, SUCC_ID, SUCC_IP, SUCC_PORT);
                            char port_str[16];
                            snprintf(port_str, sizeof(port_str), "%u", sender_port_host);
                            struct sockaddr_in original_sender= 
                                derive_sockaddr(sender_ip_str, port_str);
                            sendto(udp_socket, reply_buf, sizeof(reply_buf), 0,
                                   (struct sockaddr *)&original_sender, 
                                   sizeof(original_sender));
                        } else {
                            uint8_t forward_buf[11];
                            memset(forward_buf, 0, sizeof(forward_buf));
                            forward_buf[0] = 0;
                            forward_buf[1] = (hash_id >> 8) & 255;
                            forward_buf[2] = (hash_id & 255);
                            forward_buf[3] = (sender_node_id >> 8) & 255;
                            forward_buf[4] = (sender_node_id & 255);
                            memcpy(&forward_buf[5], &sender_ip_raw, 4);
                            memcpy(&forward_buf[9], &sender_port_raw, 2);
                            struct sockaddr_in succ_addr = derive_sockaddr(SUCC_IP, SUCC_PORT);
                            sendto(udp_socket, forward_buf, sizeof(forward_buf), 0,(struct sockaddr *)&succ_addr, sizeof(succ_addr));
                        }
                    }else {
                        uint8_t reply_buf[11];
                        memset(reply_buf, 0, sizeof(reply_buf));
                        reply_buf[0] = 1;  // REPLY
                        reply_buf[1] = (PRED_ID >> 8) & 255;
                        reply_buf[2] = (PRED_ID & 255);
                        reply_buf[3] = (node_ID >> 8) & 255;
                        reply_buf[4] = (node_ID & 255);
                        uint32_t my_ip_network = inet_addr(Node_IP);
                        memcpy(&reply_buf[5], &my_ip_network, 4);
                        uint16_t my_port_network = htons((uint16_t)atoi(Node_Port));
                        memcpy(&reply_buf[9], &my_port_network, 2);
                        char port_str[16];
                        snprintf(port_str, sizeof(port_str), "%u", sender_port_host);
                        struct sockaddr_in original_node=derive_sockaddr(sender_ip_str, port_str);
                        sendto(udp_socket, reply_buf, sizeof(reply_buf), 0,(struct sockaddr *)&original_node, sizeof(original_node));
                    }
                }
            }
        }
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int idx = 2 + i;
            if (fds[idx].fd == -1) continue;
            if (!(fds[idx].revents & POLLIN)) continue;
            bool keep_alive = handle_connection(&states[i], udp_socket);
            if (!keep_alive) {
                close(fds[idx].fd);
                fds[idx].fd = -1;
                fds[idx].events = 0;
            }
        }
    }
    return EXIT_SUCCESS;
}
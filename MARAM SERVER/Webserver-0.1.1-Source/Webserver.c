#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define BUFFER_SIZE 65536

// Request Structure
struct Request
{
    char *method;
    char *request_URI;
    char *version;
    char *message_body;
};

// Initialize Request
void init_request(struct Request *req)
{
    req->method = NULL;
    req->request_URI = NULL;
    req->version = NULL;
    req->message_body = NULL;
}

// Free Request
void free_request(struct Request *req)
{
    if (req->method)
        free(req->method);
    if (req->request_URI)
        free(req->request_URI);
    if (req->version)
        free(req->version);
    if (req->message_body)
        free(req->message_body);
}

// Parse the first line of the request
void parse_request_line(char *line, struct Request *req)
{
    size_t token_length = strcspn(line, " ");
    req->method = strndup(line, token_length);
    line += token_length + 1;

    token_length = strcspn(line, " ");
    req->request_URI = strndup(line, token_length);
    line += token_length + 1;

    token_length = strcspn(line, "\r\n");
    req->version = strndup(line, token_length);
}

// Handle GET method
int handle_get(struct Request *req)
{
    if (strncmp(req->request_URI, "/static/", 8) == 0)
    {
        if (strcmp(req->request_URI, "/static/foo") == 0)
        {
            req->message_body = strdup("Foo");
            return 200; // OK
        }
        else if (strcmp(req->request_URI, "/static/bar") == 0)
        {
            req->message_body = strdup("Bar");
            return 200; // OK
        }
        else if (strcmp(req->request_URI, "/static/baz") == 0)
        {
            req->message_body = strdup("Baz");
            return 200; // OK
        }
        return 404; // Not Found
    }

    return 404; // Not Found
}

// Generate Response
char *generate_response(int status, struct Request *req)
{
    const char *status_msg;
    switch (status)
    {
    case 200:
        status_msg = "OK";
        break;
    case 404:
        status_msg = "Not Found";
        break;
    case 400:
        status_msg = "Bad Request";
        break;
    case 501:
        status_msg = "Not Implemented";
        break;
    default:
        status_msg = "Internal Server Error";
        break;
    }

    size_t content_length = req->message_body ? strlen(req->message_body) : 0;
    char content_len[20];
    snprintf(content_len, sizeof(content_len), "%zu", content_length);

    size_t response_size = snprintf(NULL, 0, "HTTP/1.1 %d %s\r\nContent-Length: %s\r\n\r\n%s",
                                    status, status_msg, content_len,
                                    req->message_body ? req->message_body : "");

    char *response = malloc(response_size + 1);
    snprintf(response, response_size + 1, "HTTP/1.1 %d %s\r\nContent-Length: %s\r\n\r\n%s",
             status, status_msg, content_len,
             req->message_body ? req->message_body : "");

    return response;
}

// Handle Request
int handle_request(struct Request *req)
{
    if (strcmp(req->method, "GET") == 0)
    {
        return handle_get(req);
    }
    else if (strcmp(req->method, "HEAD") == 0)
    {
        return 501; // Not Implemented for HEAD
    }

    return 400; // Bad Request for unsupported methods
}

// Manage Replies
void manage_replies(int sock_acc)
{
    char buffer[BUFFER_SIZE];
    int total_bytes_received = 0;

    while (1)
    {
        ssize_t bytes_received = recv(sock_acc, buffer + total_bytes_received, BUFFER_SIZE - total_bytes_received, 0);
        if (bytes_received <= 0)
        {
            if (bytes_received < 0)
                perror("recv");
            break;
        }

        total_bytes_received += bytes_received;
        buffer[total_bytes_received] = '\0';

        struct Request req;
        init_request(&req);

        parse_request_line(buffer, &req);

        int status = handle_request(&req);
        char *response = generate_response(status, &req);

        send(sock_acc, response, strlen(response), 0);

        free(response);
        free_request(&req);

        memset(buffer, 0, BUFFER_SIZE);
        total_bytes_received = 0;
    }
}

// Listener Socket
int get_listener_socket(char *port, char *ip_address)
{
    struct addrinfo hints, *results, *p;
    int sock;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int check_infos = getaddrinfo(ip_address, port, &hints, &results);
    if (check_infos != 0)
    {
        perror("getaddrinfo");
        return -1;
    }

    for (p = results; p != NULL; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1)
        {
            perror("socket");
            continue;
        }

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(sock, p->ai_addr, p->ai_addrlen) == 0)
        {
            break;
        }

        close(sock);
    }

    freeaddrinfo(results);
    if (p == NULL)
    {
        fprintf(stderr, "Failed to bind\n");
        return -1;
    }

    if (listen(sock, 10) < 0)
    {
        perror("listen");
        return -1;
    }

    return sock;
}

// Main Function
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <ip_address> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *ip_address = argv[1];
    const char *port = argv[2];

    int sock = get_listener_socket((char *)port, (char *)ip_address);
    if (sock < 0)
    {
        perror("Error getting listening socket\n");
        return EXIT_FAILURE;
    }

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int sock_acc = accept(sock, (struct sockaddr *)&client_addr, &client_len);
        if (sock_acc == -1)
        {
            perror("accept");
            continue;
        }

        manage_replies(sock_acc);
        close(sock_acc);
    }

    close(sock);
    return EXIT_SUCCESS;
}

















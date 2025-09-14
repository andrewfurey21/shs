
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/socket.h>

#define MAX_REQ_SIZE 2048

const int MAX_BACKLOG = 10;

// linked list of clients.
struct client_info {
  socklen_t address_length;
  struct sockaddr_storage address;
  int socketfd;
  int bytes_received;
  struct client_info *next;
  char request[MAX_REQ_SIZE];
};

const char *content_type(const char *file_name) {
  char *extension = strrchr(file_name, '.');
  if (extension) {
    if (strcmp(extension, ".js") == 0)
      return "text/js";
    else if (strcmp(extension, ".css") == 0)
      return "text/css";
    else if (strcmp(extension, ".html") == 0)
      return "text/html";
    else if (strcmp(extension, ".txt") == 0)
      return "text/plain";
  }
  printf("Error: bad extension\n");
  return NULL;
}

void check_error(int error, const char *message) {
  if (error < 0) {
    printf("Error: %s\n Error Number: %d\n", message, errno);
    exit(-1);
  }
}

int check_socket(struct addrinfo *address) {
  int s =
      socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  check_error(s, "Could not create socket.");
  return s;
}

void check_bind(int socketfd, struct addrinfo *address) {
  int bound = bind(socketfd, address->ai_addr, address->ai_addrlen);
  check_error(bound, "Could not bind socket.");
}

void check_listen(int socketfd, int backlog) {
  int listening = listen(socketfd, backlog);
  check_error(listening, "Could not listen to socket.");
}

int socket_bind_listen(const char *host, const char *port) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *address;
  if (getaddrinfo(host, port, &hints, &address) != 0) {
    printf("Error: could not get address info. errno:%d\n", errno);
    exit(-1);
  }

  int s = check_socket(address);
  check_bind(s, address);
  freeaddrinfo(address);
  check_listen(s, MAX_BACKLOG);

  printf("Listening on %s:%s\n", host, port);
  return s;
}

struct client_info *get_client(struct client_info *root, int socketfd) {
  struct client_info *current = root;
  while (current != NULL && current->socketfd != socketfd)
    current = current->next;

  if (current == NULL) {
    struct client_info *new_client =
      (struct client_info *)calloc(1, sizeof(struct client_info));
    current->next = new_client;
    current = new_client;
    if (new_client == NULL) {
      printf("Error: could not allocate new client\n");
      exit(-1);
    }
  }

  return current;
}

void drop_client(struct client_info *root, int socketfd) {
  struct client_info *current = root;
  struct client_info *parent = NULL;
  
  while (current != NULL && current->socketfd != socketfd) {
    parent = current;
    current = current->next;
  }

  if (current == NULL) {
    printf("Error: Could not find socket %d\n", socketfd);
    exit(-1);
  }

  parent->next = current->next;
  free(current);
  close(socketfd);
}

const char *get_client_address(struct client_info* client) {
  static char buf[100];
  getnameinfo((struct sockaddr*)&client->address,
              client->address_length,
              buf,
              sizeof(char[100]),
              0,
              0,
              NI_NUMERICHOST);

  return buf;
}

fd_set wait_on_clients(struct client_info *root, int socketfd) {
  fd_set reads;
  FD_ZERO(&reads);
  int max = socketfd;

  struct client_info *current = root;
  while (current != NULL) {
    FD_SET(current->socketfd, &reads);
    if (current->socketfd > max)
      max = current->socketfd;
    current = current->next;
  }

  check_error(select(max + 1, &reads, 0, 0, 0), "Select failed.");
  return reads;
}

int main() {

  const char *type = content_type("/homepage.html");

  int server_socket = socket_bind_listen("127.0.0.1", "8080");

  return 0;
}

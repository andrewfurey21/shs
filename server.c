
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <netdb.h>
#include <sys/socket.h>

const int MAX_BACKLOG = 10;

const char* content_type(const char* file_name) {
  char* extension = strrchr(file_name, '.');
  if (extension) {
    if (strcmp(extension, ".js") == 0) return "text/js";
    else if (strcmp(extension, ".css") == 0) return "text/css";
    else if (strcmp(extension, ".html") == 0) return "text/html";
    else if (strcmp(extension, ".txt") == 0) return "text/plain";
  }
  printf("Error: bad extension\n");
  return NULL;
}

void check_error(int error, const char* message) {
  if (error < 0) {
    printf("Error: %s\n Error Number: %d\n", message, errno);
    exit(-1);
  }
}

int check_socket(struct addrinfo *address) {
  int s = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
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

int socket_bind_listen(const char* host, const char* port) {
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
  check_listen(s, MAX_BACKLOG);

  printf("Listening on %s:%s\n", host, port);
  return s;
}

int main() {

  const char* type = content_type("/homepage.html");

  socket_bind_listen("127.0.0.1", "8080");

  return 0;
}

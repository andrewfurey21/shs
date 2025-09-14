
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_REQ_SIZE 2048
#define MAX_SND_SIZE 2048

const int MAX_BACKLOG = 10;
const int MAX_FILE_PATH_LEN = 100;

// linked list of clients.
struct client_info {
  socklen_t address_length;
  struct sockaddr_storage address;
  int socketfd;
  int bytes_received;
  struct client_info *parent;
  char request[MAX_REQ_SIZE + 1];
};

const char *get_content_type(const char *file_name) {
  char *extension = strrchr(file_name, '.');
  if (extension) {
    if (strcmp(extension, ".css") == 0)
      return "text/css";
    else if (strcmp(extension, ".html") == 0)
      return "text/html";
    else if (strcmp(extension, ".png") == 0)
      return "image/png";
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
  int option = 1;
  check_error(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)),
              "Could not set SO_REUSEADDR option.");
  check_bind(s, address);
  freeaddrinfo(address);
  check_listen(s, MAX_BACKLOG);

  printf("Listening on %s:%s\n", host, port);
  return s;
}

struct client_info *get_client(struct client_info **client_list, int socketfd) {
  struct client_info *current = *client_list;
  while (current != NULL && current->socketfd != socketfd) {
    current = current->parent;
  }

  if (current == NULL) {
    struct client_info *new_client =
        (struct client_info *)calloc(1, sizeof(struct client_info));
    current = new_client;

    current->address_length = sizeof(current->address);

    if (*client_list != NULL)
      (*client_list)->parent = current;
    *client_list = current;
    if (new_client == NULL) {
      printf("Error: could not allocate new client\n");
      exit(-1);
    }
  }

  return current;
}

void drop_client(struct client_info **client_list, int socketfd) {
  struct client_info *current = *client_list;
  while (current != NULL && current->socketfd != socketfd)
    current = current->parent;

  if (current == NULL) {
    printf("Error: Could not find socket %d\n", socketfd);
    exit(-1);
  }

  *client_list = current->parent;
  free(current);
  close(socketfd);
}

char *get_client_address(struct client_info *client) {
  char *buf = (char *)calloc(1, 100);
  getnameinfo((struct sockaddr *)&client->address, client->address_length, buf,
              99, 0, 0, NI_NUMERICHOST);
  // getnameinfo((struct sockaddr*)&client->address,
  //             client->address_length,
  //             buf + strlen(buf),
  //             99,
  //             0,
  //             0,
  //             NI_NUMERICSERV);
  return buf;
}

fd_set wait_on_clients(struct client_info **client_list, int socketfd) {
  fd_set reads;
  FD_ZERO(&reads);
  FD_SET(socketfd, &reads);
  int max = socketfd;

  struct client_info *current = *client_list;
  while (current != NULL) {
    FD_SET(current->socketfd, &reads);
    if (current->socketfd > max)
      max = current->socketfd;
    current = current->parent;
  }
  check_error(select(max + 1, &reads, 0, 0, 0), "Select failed.");
  return reads;
}

void send_error_bad_request(struct client_info **client_list,
                            struct client_info *client) {
  const char *message = "HTTP/1.1 400 Bad Request\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 14\r\n"
                        "\r\n"
                        "Bad Request :(";
  send(client->socketfd, message, strlen(message), 0);
  drop_client(client_list, client->socketfd);
}

// redirect
void send_error_not_found(struct client_info **client_list,
                          struct client_info *client) {
  const char *message = "HTTP/1.1 404 Not Found\r\n"
                        "Connection: close\r\n"
                        "Content-Length: 25\r\n"
                        "\r\n"
                        "<h1>Not found, loser</h1>";

  send(client->socketfd, message, strlen(message), 0);
  drop_client(client_list, client->socketfd);
}

void send_resource(struct client_info **client_list, struct client_info *client,
                   const char *file_path) {
  time_t date;
  time(&date);
  char *addrstr = get_client_address(client);
  printf("sending \"%s\"\t%s\ttime: %s", file_path, addrstr, ctime(&date));
  free(addrstr);

  if (strlen(file_path) > MAX_FILE_PATH_LEN)
    send_error_bad_request(client_list, client);

  if (strstr(file_path, "..") || strstr(file_path, "\\.\\."))
    send_error_not_found(client_list, client);

  const char *public_dir = "./public";
  char full_path[MAX_FILE_PATH_LEN + strlen(public_dir)];

  if (strcmp(file_path, "/") == 0)
    sprintf(full_path, "%s/index.html", public_dir);
  else
    sprintf(full_path, "%s%s", public_dir, file_path);

  FILE *file = fopen(full_path, "rb");
  if (!file) {
    send_error_not_found(client_list, client);
    return;
  }

  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0L, SEEK_SET);

  const char *content_type = get_content_type(full_path);
  char send_buffer[MAX_SND_SIZE + 1] = {0};

  sprintf(send_buffer,
          "HTTP/1.1 200 OK\r\n"
          "Connection: close\r\n"
          "Content-Length: %ld\r\n"
          "Content-Type: %s\r\n"
          "\r\n",
          file_size, content_type);

  printf("size: %ld\n", file_size);
  send(client->socketfd, send_buffer, strlen(send_buffer), 0);

  bool reading = true;
  while (reading) {
    int bytes_read = fread(send_buffer, 1, MAX_SND_SIZE, file);
    reading = !!bytes_read;
    send(client->socketfd, send_buffer, bytes_read, 0);
  }

  fclose(file);
  drop_client(client_list, client->socketfd);
}

int main() {
  int server = socket_bind_listen("127.0.0.1", "8080");
  struct client_info *client_list = NULL;

  while (true) {
    fd_set reads;
    reads = wait_on_clients(&client_list, server);

    if (FD_ISSET(server, &reads)) {
      struct client_info *client = get_client(&client_list, -1);
      client->socketfd = accept(server, (struct sockaddr *)&client->address,
                                &client->address_length);
      check_error(client->socketfd, "Could not accept client.");
      char *addrstr = get_client_address(client);
      printf("------------------\nNew connection: %s\n", addrstr);
      free(addrstr);
      fflush(stdout);
    }

    struct client_info *client = client_list;
    while (client) {
      if (FD_ISSET(client->socketfd, &reads)) {
        if (client->bytes_received > MAX_REQ_SIZE) {
          send_error_bad_request(&client_list, client);
          continue;
        }
        int bytes_received =
            recv(client->socketfd, client->request + client->bytes_received,
                 MAX_REQ_SIZE - client->bytes_received, 0);

        if (bytes_received <= 0) {
          char *addrstr = get_client_address(client);
          printf("Unexpected disconnect: %s\n", addrstr);
          free(addrstr);
          drop_client(&client_list, client->socketfd);
          continue;
        }
        client->bytes_received += bytes_received;
        client->request[bytes_received] = 0; // make sure of null termination

        if (strncmp(client->request, "GET /", 5)) {
          send_error_bad_request(&client_list, client);
        } else {
          char *path_start = client->request + 4;
          char *path_end = strstr(path_start, " ");

          if (path_end == NULL) {
            send_error_bad_request(&client_list, client);
          } else {
            char *file_path =
                (char *)calloc(sizeof(char), path_end - path_start);
            strncpy(file_path, path_start, path_end - path_start);
            char *addrstr = get_client_address(client);
            printf("Sending %s to %s\n", file_path, addrstr);
            free(addrstr);
            send_resource(&client_list, client, file_path);
            free(file_path);
          }
        }
      }
      client = client->parent;
    }
  }

  printf("Server shutting down.\n");
  close(server);
  return 0;
}

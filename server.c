
#include <string.h>
#include <stdio.h>

const char* content_type(const char* file_name) {
  char* extension = strchr(file_name, '.');
  if (extension) {
    if (strcmp(extension, ".js") == 0) return "text/js";
    else if (strcmp(extension, ".css") == 0) return "text/css";
    else if (strcmp(extension, ".html") == 0) return "text/html";
    else if (strcmp(extension, ".txt") == 0) return "text/plain";
  }
  printf("Error: bad extension\n");
  return NULL;
}

int main() {

  const char* type = content_type("/homepage.html");

  return 0;
}

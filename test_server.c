#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <microhttpd.h>

#include <jansson.h>

#define PROG "test_server"
#define DEFAULT_PORT 8000

typedef enum option_type { OPTION_NONE, OPTION_PORT } option_type;

void usage(void) {
  fprintf(stderr,
          "Usage: %s [OPTIONS]\n\n"
          "Options:\n"
          "\t-h|--help\tprint this help\n"
          "\t-p|--port PORT\tset TCP port (default %d)\n\n",
          PROG, DEFAULT_PORT);
}

void error(char *format, ...) {
  va_list ap;

  va_start(ap, format);
  fprintf(stderr, "%s: ", PROG);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);

  exit(EXIT_FAILURE);
}

enum MHD_Result handler_cb(void *cls, struct MHD_Connection *connection,
                           const char *url, const char *method,
                           const char *version, const char *upload_data,
                           size_t *upload_data_size, void **con_cls) {
  struct MHD_Response *response;
  int ret;
  json_t *body;
  char *tmp;

  (void)cls;
  (void)url;
  (void)method;
  (void)version;
  (void)upload_data;
  (void)upload_data_size;
  (void)con_cls;

  body = json_object();
  json_object_set_new(body, "title", json_string("example"));
  json_object_set_new(body, "description",
                      json_string("this is an example todo item"));
  tmp = json_dumps(body, 0);
  json_decref(body);
  response = MHD_create_response_from_buffer(strlen(tmp), (void *)tmp,
                                             MHD_RESPMEM_MUST_FREE);
  ret = MHD_add_response_header(response, "Content-Type", "application/json");
  ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
  MHD_destroy_response(response);

  return ret;
}

int main(int argc, char **argv) {
  char *arg;
  option_type opt = OPTION_NONE;
  int port = DEFAULT_PORT;

  while (argc-- > 1) {
    arg = *(++argv);
    if ((strcmp(arg, "-h") == 0) || (strcmp(arg, "--help") == 0)) {
      usage();
      return 1;
    } else if ((strcmp(arg, "-p") == 0) || (strcmp(arg, "-port") == 0)) {
      opt = OPTION_PORT;
    } else {
      switch (opt) {
      case OPTION_NONE:
        error("Uknown argument: %s", arg);
        break;
      case OPTION_PORT:
        port = atoi(arg);
        if (!port) {
          error("Invalid port number: %s", arg);
        }
        break;
      }
    }
  }

  struct MHD_Daemon *daemon;
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, NULL, NULL,
                            handler_cb, NULL, MHD_OPTION_END);
  if (!daemon) {
    return EXIT_FAILURE;
  }

  getchar();

  MHD_stop_daemon(daemon);
  return EXIT_SUCCESS;
}

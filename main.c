#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utstring.h>

typedef enum { PROTO_HTTP, PROTO_HTTPS } proto;

typedef enum { METHOD_GET, METHOD_POST, METHOD_PUT, METHOD_DELETE } method;

typedef struct {
  proto proto;
  char *host;
  char *path;
  char *user;
  char *passwd;
} api;

typedef struct {
  char *name;
  char *value;
} header;

typedef struct {
  api *api;
  method method;
  char *path;
  header **headers;
  void *body;
  size_t body_len;
} request;

typedef struct {
  request *req;
  void *body;
  size_t body_len;
} response;

static void *init() { curl_init_global(CURL_INIT_ALL); }

static void *parallel(request **reqs) {}

int main(int argc, char **argv) { return 0; }

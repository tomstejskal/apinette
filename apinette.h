#ifndef APINETTE_H

#include <stdlib.h>

#define HEADER_ACCEPT "Accept"
#define HEADER_AUTHORIZATION "Authorization"
#define HEADER_CONTENT_TYPE "Content-Type"

#define MIME_JSON "application/json"

typedef enum { PROTO_HTTP, PROTO_HTTPS } proto;

typedef enum { METHOD_GET, METHOD_POST, METHOD_PUT, METHOD_DELETE } method;

typedef enum { AUTH_NONE, AUTH_BASIC } auth_type;

typedef struct {
  char *user;
  char *passwd;
} basic_auth;

typedef struct {
  proto proto;
  char *host;
  char *path;
  auth_type auth_type;
  void *auth;
} api;

typedef struct {
  int status;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  char *err;
} response;

typedef struct request {
  api *api;
  method method;
  char *path;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  response *resp;
  struct request *prev;
  struct request *next;
} request;

char *apinette_printf(char *format, ...);

void apinette_init(char **err);

void apinette_cleanup(void);

void apinette_parallel(request *head, char **err);

request *apinette_new_request(api *api, method method, char *path);

#endif // APINETTE_H

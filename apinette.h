#ifndef APINETTE_H

#include <lua.h>
#include <stdlib.h>

#define API_HEADER_ACCEPT "Accept"
#define API_HEADER_AUTHORIZATION "Authorization"
#define API_HEADER_CONTENT_TYPE "Content-Type"

#define API_MIME_JSON "application/json"

#define API_PROTO_HTTP_STR "http"
#define API_PROTO_HTTPS_STR "https"

#define API_METHOD_GET_STR "GET"
#define API_METHOD_POST_STR "POST"
#define API_METHOD_PUT_STR "PUT"
#define API_METHOD_DELETE_STR "GET"

typedef enum {
  API_TYPE_ENDPOINT,
  API_TYPE_AUTH,
  API_TYPE_REQUEST
} api_userdata_type;

typedef enum { API_PROTO_HTTP, API_PROTO_HTTPS } api_proto;

typedef enum {
  API_METHOD_GET,
  API_METHOD_POST,
  API_METHOD_PUT,
  API_METHOD_DELETE
} api_method;

typedef enum { API_AUTH_BASIC } api_auth_type;

typedef struct {
  char *user;
  char *passwd;
} api_basic_auth_t;

typedef struct {
  api_auth_type type;
  union {
    api_basic_auth_t *basic;
  };
} api_auth_t;

typedef struct {
  api_proto proto;
  char *host;
  char *path;
  api_auth_t *auth;
  int verbose;
} api_endpoint_t;

typedef struct {
  int status;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  char *err;
  char *url;
} api_response_t;

typedef struct api_request_t {
  api_endpoint_t *endpoint;
  api_method method;
  char *path;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  api_response_t *resp;
  struct api_request_t *prev;
  struct api_request_t *next;
} api_request_t;

char *api_printf(char *format, ...);

void api_init(char **err);

void api_init_lua(lua_State *L);

void api_cleanup(void);

void api_send(api_request_t *head, char **err);

#endif // APINETTE_H

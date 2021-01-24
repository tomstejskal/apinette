#ifndef APINETTE_H

#include <lua.h>
#include <stdlib.h>

#define API_HEADER_ACCEPT "Accept"
#define API_HEADER_AUTHORIZATION "Authorization"
#define API_HEADER_CONTENT_TYPE "Content-Type"

#define API_MIME_JSON "application/json"

#define API_PROTO_HTTP_STR "http"
#define API_PROTO_HTTPS_STR "https"

typedef enum API_userdata_type {
  API_TYPE_API,
  API_TYPE_AUTH,
  API_TYPE_REQUEST
} API_userdata_type;

typedef enum API_proto { API_PROTO_HTTP, API_PROTO_HTTPS } API_proto;

typedef enum API_method {
  API_METHOD_GET,
  API_METHOD_POST,
  API_METHOD_PUT,
  API_METHOD_DELETE
} API_method;

typedef enum API_auth_type { API_AUTH_BASIC } API_auth_type;

typedef struct API_basic_auth {
  char *user;
  char *passwd;
} API_basic_auth;

typedef struct API_auth {
  API_auth_type typ;
  union {
    API_basic_auth *basic;
  };
} API_auth;

typedef struct API_api {
  API_proto proto;
  char *host;
  char *path;
  API_auth *auth;
  int verbose;
} API_api;

typedef struct API_response {
  int status;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  char *err;
} API_response;

typedef struct API_request {
  API_api *api;
  API_method method;
  char *path;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  API_response *resp;
  struct API_request *prev;
  struct API_request *next;
} API_request;

char *api_printf(char *format, ...);

void api_init(char **err);

void api_init_lua(lua_State *L);

void api_cleanup(void);

void api_send(API_request *head, char **err);

API_request *api_new_request(API_api *api, API_method method, char *path);

#endif // APINETTE_H

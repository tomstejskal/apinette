#include <ctype.h>
#include <curl/curl.h>
#include <jansson.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "apinette.h"
#include "base64.h"
#include "utlist.h"
#include "utstring.h"

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

#define api_getstringfield(L, dst, name, table_index, tmp)                     \
  lua_getfield((L), (table_index), (name));                                    \
  if (lua_isnil((L), -1)) {                                                    \
    (dst) = NULL;                                                              \
  } else {                                                                     \
    (tmp) = lua_tostring((L), -1);                                             \
    if ((tmp)) {                                                               \
      (dst) = malloc(strlen((tmp)) + 1);                                       \
      strcpy((dst), (tmp));                                                    \
    }                                                                          \
  }                                                                            \
  lua_pop((L), 1);

#define api_setglobalstrconst(L, s)                                            \
  lua_pushstring((L), (s));                                                    \
  lua_setglobal((L), (s));

typedef enum {
  API_TYPE_ENDPOINT,
  API_TYPE_AUTH,
  API_TYPE_REQUEST
} api_userdata_type;

typedef enum { API_PROTO_HTTP, API_PROTO_HTTPS } api_proto_t;

typedef enum {
  API_METHOD_GET,
  API_METHOD_POST,
  API_METHOD_PUT,
  API_METHOD_DELETE,
  API_METHOD_CUSTOM
} api_method_t;

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
  api_proto_t proto;
  char *host;
  char *path;
  api_auth_t *auth;
  int verbose;
  char *handle_response_chunk;
  size_t handle_response_chunk_len;
} api_endpoint_t;

typedef struct {
  int status;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  char *err;
  char *url;
  double total_time;
} api_response_t;

typedef struct api_request_t {
  api_endpoint_t *endpoint;
  api_method_t method;
  char *custom_method;
  char *path;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  char *handle_response_chunk;
  size_t handle_response_chunk_len;
  api_response_t *resp;
  struct api_request_t *prev;
  struct api_request_t *next;
} api_request_t;

char *api_printf(char *format, ...) {
  va_list va;
  UT_string *s;
  char *buf;

  va_start(va, format);
  utstring_new(s);
  utstring_printf_va(s, format, va);
  size_t len = utstring_len(s);
  buf = malloc(len + 1);
  memcpy(buf, utstring_body(s), len);
  buf[len] = 0;
  utstring_free(s);
  va_end(va);

  return buf;
}

static char *api_proto_t_str(api_proto_t p) {
  switch (p) {
  case API_PROTO_HTTP:
    return API_PROTO_HTTP_STR;
  case API_PROTO_HTTPS:
    return API_PROTO_HTTP_STR;
  }
  return NULL;
}

static char *api_method_str(api_request_t *req) {
  switch (req->method) {
  case API_METHOD_GET:
    return API_METHOD_GET_STR;
  case API_METHOD_POST:
    return API_METHOD_POST_STR;
  case API_METHOD_PUT:
    return API_METHOD_PUT_STR;
  case API_METHOD_DELETE:
    return API_METHOD_DELETE_STR;
  case API_METHOD_CUSTOM:
    return req->custom_method;
  }
  return NULL;
}

static void api_add_header(api_request_t *req, char *name, char *val) {
  UT_string *s;

  utstring_new(s);
  utstring_printf(s, "%s: %s", name, val);
  req->headers = curl_slist_append(req->headers, utstring_body(s));

  utstring_free(s);
}

static void api_add_basic_auth(api_request_t *req, api_basic_auth_t *auth) {
  UT_string *s;
  char *base64;
  size_t base64_len;

  utstring_new(s);
  utstring_printf(s, "%s:%s", auth->user, auth->passwd);
  base64 = (char *)base64_encode((const unsigned char *)utstring_body(s),
                                 utstring_len(s), &base64_len);
  utstring_clear(s);
  utstring_printf(s, "Basic ");
  utstring_bincpy(s, base64, base64_len);

  api_add_header(req, API_HEADER_AUTHORIZATION, utstring_body(s));

  utstring_free(s);
  free(base64);
}

static void api_add_auth(api_request_t *req) {
  if (req->endpoint->auth) {
    switch (req->endpoint->auth->type) {
    case API_AUTH_BASIC:
      api_add_basic_auth(req, req->endpoint->auth->basic);
      break;
    }
  }
}

static size_t api_write_body(char *ptr, size_t n, size_t l,
                             api_request_t *req) {
  size_t len = n * l;

  req->resp->body = realloc(req->resp->body, req->resp->body_len + len);
  memcpy(req->resp->body + req->resp->body_len, ptr, len);
  req->resp->body_len += len;

  return len;
}

static size_t api_write_header(char *buf, size_t l, size_t n,
                               api_request_t *req) {
  size_t len = n * l;
  char *tmp;

  tmp = malloc(len + 1);
  memcpy(tmp, buf, len);
  tmp[len] = 0;
  req->resp->headers = curl_slist_append(req->resp->headers, tmp);
  free(tmp);

  return len;
}

static void api_add_request(CURLM *cm, api_request_t *req, char **err) {
  CURL *c;

  c = curl_easy_init();
  if (!c) {
    *err = api_printf(*err, "Cannot init request");
  }

  req->resp = calloc(1, sizeof(api_response_t));

  curl_easy_setopt(c, CURLOPT_VERBOSE, (long)req->endpoint->verbose);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, api_write_body);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, req);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, api_write_header);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, req);
  req->resp->url = api_printf(
      "%s://%s%s%s", api_proto_t_str(req->endpoint->proto), req->endpoint->host,
      req->endpoint->path ? req->endpoint->path : "",
      req->path ? req->path : "");
  curl_easy_setopt(c, CURLOPT_URL, req->resp->url);
  curl_easy_setopt(c, CURLOPT_PRIVATE, req);

  switch (req->method) {
  case API_METHOD_GET:
    break;
  case API_METHOD_POST:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    break;
  case API_METHOD_PUT:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
    break;
  case API_METHOD_DELETE:
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  case API_METHOD_CUSTOM:
    if (req->body) {
      curl_easy_setopt(c, CURLOPT_POST, 1L);
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    }
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req->custom_method);
    break;
  }

  api_add_auth(req);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, req->headers);
  curl_multi_add_handle(cm, c);
}

static void api_send_requests(api_request_t *head, char **err) {
  CURLM *cm;
  CURLMsg *msg;
  int running = 1;
  int msgs_left = -1;
  api_request_t *req;

  cm = curl_multi_init();
  if (!cm) {
    *err = api_printf("Cannot init transfer");
    return;
  }

  DL_FOREACH(head, req) {
    api_add_request(cm, req, err);
    if (*err) {
      return;
    }
  }

  while (running) {
    curl_multi_perform(cm, &running);

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      CURL *c = msg->easy_handle;
      api_request_t *req;
      curl_easy_getinfo(c, CURLINFO_PRIVATE, &req);
      curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &req->resp->total_time);
      if (msg->msg == CURLMSG_DONE) {
        long status;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        req->resp->status = (int)status;
        if (msg->data.result > 0) {
          req->resp->err =
              api_printf("%s", curl_easy_strerror(msg->data.result));
        }
        curl_multi_remove_handle(cm, c);
        curl_easy_cleanup(c);
      } else {
        req->resp->err = api_printf("Unexpected message type: %d", msg->msg);
      }
    }
    if (running) {
      curl_multi_wait(cm, NULL, 0, 100, NULL);
    }
  }

  curl_multi_cleanup(cm);
}

static int api_endpoint_gc(lua_State *L) {
  api_endpoint_t *ep = lua_touserdata(L, -1);

  free(ep->handle_response_chunk);
  if (ep->auth) {
    switch (ep->auth->type) {
    case API_AUTH_BASIC:
      if (ep->auth->basic) {
        free(ep->auth->basic->user);
        free(ep->auth->basic->passwd);
        free(ep->auth->basic);
      }
      break;
    }
    free(ep->auth);
  }
  free(ep->host);
  free(ep->path);

  return 0;
}

static int api_request_gc(lua_State *L) {
  api_request_t *req = lua_touserdata(L, -1);

  free(req->custom_method);
  free(req->path);
  curl_slist_free_all(req->headers);
  free(req->body);
  free(req->handle_response_chunk);
  if (req->resp) {
    curl_slist_free_all(req->resp->headers);
    free(req->resp->body);
    free(req->resp->err);
    free(req->resp->url);
    free(req->resp);
  }

  return 0;
}

static void api_read_json(lua_State *L, json_t *json) {
  json_t *value;
  const char *key;
  size_t i;

  if (json_is_false(json)) {
    lua_pushboolean(L, 0);
  } else if (json_is_true(json)) {
    lua_pushboolean(L, 1);
  } else if (json_is_null(json)) {
    lua_pushnil(L);
  } else if (json_is_integer(json)) {
    lua_pushinteger(L, json_integer_value(json));
  } else if (json_is_real(json)) {
    lua_pushnumber(L, json_real_value(json));
  } else if (json_is_string(json)) {
    lua_pushlstring(L, json_string_value(json), json_string_length(json));
  } else if (json_is_array(json)) {
    lua_newtable(L);
    json_array_foreach(json, i, value) {
      api_read_json(L, value);
      lua_seti(L, -2, i + 1);
    }
  } else if (json_is_object(json)) {
    lua_newtable(L);
    json_object_foreach(json, key, value) {
      api_read_json(L, value);
      lua_setfield(L, -2, key);
    }
  } else {
    lua_pushnil(L);
  }
}

static json_t *api_write_json(lua_State *L) {
  json_t *json, *value;
  const char *s;
  size_t size;
  int i, len;

  switch (lua_type(L, -1)) {
  case LUA_TNIL:
    json = json_null();
    break;
  case LUA_TBOOLEAN:
    json = json_boolean(lua_toboolean(L, -1));
    break;
  case LUA_TNUMBER:
    if (lua_isinteger(L, -1)) {
      json = json_integer(lua_tointeger(L, -1));
    } else {
      json = json_real(lua_tonumber(L, -1));
    }
    break;
  case LUA_TSTRING:
    s = lua_tolstring(L, -1, &size);
    json = json_stringn(s, size);
    break;
  case LUA_TTABLE:
    lua_len(L, -1);
    len = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (len > 0) {
      // array
      json = json_array();
      for (i = 1; i <= len; i++) {
        lua_geti(L, -1, i);
        value = api_write_json(L);
        json_array_append_new(json, value);
      }
    } else {
      // object
      json = json_object();
      lua_pushnil(L);
      while (lua_next(L, -2)) {
        value = api_write_json(L);
        s = lua_tostring(L, -1);
        json_object_set_new(json, s, value);
      }
    }
    break;
  }

  lua_pop(L, 1);
  return json;
}

static int api_from_json(lua_State *L) {
  json_t *json;
  const char *tmp;
  size_t size;
  json_error_t err;

  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pushstring(L, "from_json: expecting string as an argument");
    lua_error(L);
  }

  tmp = lua_tolstring(L, -1, &size);
  json = json_loadb(tmp, size, 0, &err);
  if (!json) {
    lua_pushfstring(L, "from_json: %s (line: %d, column: %d)", err.text,
                    err.line, err.column);
    lua_error(L);
  }

  lua_pop(L, 1);
  api_read_json(L, json);
  json_decref(json);
  return 1;
}

static int api_to_json(lua_State *L) {
  json_t *json;
  char *tmp;

  json = api_write_json(L);
  tmp = json_dumps(json, 0);
  lua_pushstring(L, tmp);
  free(tmp);
  json_decref(json);

  return 1;
}

static int api_request_handle_response_chunk_cb(lua_State *L, const void *p,
                                                size_t sz, void *ud) {
  api_request_t *req = (api_request_t *)ud;
  (void)L;
  size_t new_len = req->handle_response_chunk_len + sz;
  req->handle_response_chunk = realloc(req->handle_response_chunk, new_len);
  memcpy(req->handle_response_chunk + req->handle_response_chunk_len, p, sz);
  req->handle_response_chunk_len += sz;
  return 0;
}

static int api_create_request(lua_State *L, api_method_t method,
                              char *custom_method) {
  api_request_t *req = lua_newuserdata(L, sizeof(api_request_t));
  api_endpoint_t *endpoint;
  const char *k, *v, *tmp;
  char *header;
  size_t sz;

  memset(req, 0, sizeof(api_request_t));

  endpoint = lua_touserdata(L, lua_upvalueindex(1));
  req->endpoint = endpoint;
  req->method = method;
  req->custom_method = custom_method;

  switch (lua_type(L, -2)) {
  case LUA_TTABLE:
    if (method == API_METHOD_CUSTOM) {
      api_getstringfield(L, req->custom_method, "method", -2, tmp);
    }
    api_getstringfield(L, req->path, "path", -2, tmp);
    lua_getfield(L, -2, "body");
    if (!lua_isnil(L, -1)) {
      if (lua_istable(L, -1)) {
        api_to_json(L);
        header = api_printf("%s: %s", API_HEADER_CONTENT_TYPE, API_MIME_JSON);
        req->headers = curl_slist_append(req->headers, header);
        free(header);
      }
      tmp = lua_tolstring(L, -1, &sz);
      req->body = malloc(sz);
      memcpy(req->body, tmp, sz);
      req->body_len = sz;
    }
    lua_pop(L, 1);
    lua_getfield(L, -2, "headers");
    if (lua_istable(L, -1)) {
      lua_pushnil(L);
      while (lua_next(L, -2)) {
        k = lua_tostring(L, -2);
        v = lua_tostring(L, -1);
        header = api_printf("%s: %s", k, v);
        req->headers = curl_slist_append(req->headers, header);
        free(header);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    lua_getfield(L, -2, "handle_response");
    if (!lua_isnil(L, -1)) {
      lua_dump(L, api_request_handle_response_chunk_cb, req, 0);
    }
    lua_pop(L, 1);
    break;
  case LUA_TSTRING:
    tmp = (char *)lua_tostring(L, -2);
    req->path = malloc(strlen(tmp) + 1);
    strcpy(req->path, tmp);
    break;
  default:
    lua_pushstring(L, "request function parameter should be string or table");
    lua_error(L);
  }

  if (!req->method) {
    /* default method */
    req->method = API_METHOD_GET;
  }

  lua_pushinteger(L, API_TYPE_REQUEST);
  lua_setuservalue(L, -2);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, api_request_gc);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);

  return 1;
}

static int api_endpoint_request(lua_State *L) {
  return api_create_request(L, API_METHOD_CUSTOM, NULL);
}

static int api_endpoint_get(lua_State *L) {
  return api_create_request(L, API_METHOD_GET, NULL);
}

static int api_endpoint_post(lua_State *L) {
  return api_create_request(L, API_METHOD_POST, NULL);
}

static int api_endpoint_put(lua_State *L) {
  return api_create_request(L, API_METHOD_PUT, NULL);
}

static int api_endpoint_delete(lua_State *L) {
  return api_create_request(L, API_METHOD_DELETE, NULL);
}

static int api_endpoint_index(lua_State *L) {
  const char *field = lua_tostring(L, -1);
  if (strcmp(field, "get") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, api_endpoint_get, 1); // api_endpoint_t in a closure
  } else if (strcmp(field, "post") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, api_endpoint_post, 1); // api_endpoint_t in a closure
  } else if (strcmp(field, "put") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, api_endpoint_put, 1); // api_endpoint_t in a closure
  } else if (strcmp(field, "delete") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, api_endpoint_delete, 1); // api_endpoint_t in a closure
  } else if (strcmp(field, "request") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, api_endpoint_request, 1); // api_endpoint_t in a closure
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int api_endpoint_tostring(lua_State *L) {
  api_endpoint_t *api = lua_touserdata(L, -1);
  lua_pushfstring(L, "api: %s", api->host);
  return 1;
}

static int api_auth_gc(lua_State *L) {
  api_auth_t *auth = lua_touserdata(L, -1);

  switch (auth->type) {
  case API_AUTH_BASIC:
    if (auth->basic) {
      free(auth->basic->user);
      free(auth->basic->passwd);
      free(auth->basic);
    }
    break;
  }
  return 0;
}

static int api_endpoint_handle_response_chunk_cb(lua_State *L, const void *p,
                                                 size_t sz, void *ud) {
  api_endpoint_t *ep = (api_endpoint_t *)ud;
  (void)L;
  size_t new_len = ep->handle_response_chunk_len + sz;
  ep->handle_response_chunk = realloc(ep->handle_response_chunk, new_len);
  memcpy(ep->handle_response_chunk + ep->handle_response_chunk_len, p, sz);
  ep->handle_response_chunk_len += sz;
  return 0;
}

static int api_endpoint(lua_State *L) {
  api_endpoint_t *ep = lua_newuserdata(L, sizeof(api_endpoint_t));
  const char *s;
  api_auth_t *auth;

  memset(ep, 0, sizeof(api_endpoint_t));

  lua_pushinteger(L, API_TYPE_ENDPOINT);
  lua_setuservalue(L, -2);

  if (!lua_istable(L, -2)) {
    lua_pushstring(L, "api: expects table as its argument");
    lua_error(L);
  }

  lua_pushstring(L, "proto");
  lua_gettable(L, -3);
  s = lua_tostring(L, -1);
  if (strcmp(s, API_PROTO_HTTP_STR) == 0) {
    ep->proto = API_PROTO_HTTP;
  } else if (strcmp(s, API_PROTO_HTTPS_STR) == 0) {
    ep->proto = API_PROTO_HTTPS;
  } else {
    lua_pushstring(L, "api: 'proto' should be http or https");
    lua_error(L);
  }
  lua_pop(L, 1);

  api_getstringfield(L, ep->host, "host", -2, s);
  api_getstringfield(L, ep->path, "path", -2, s);

  lua_getfield(L, -2, "verbose");
  ep->verbose = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_pushstring(L, "auth");
  lua_gettable(L, -3);
  if (!lua_isnil(L, -1)) {
    lua_getuservalue(L, -1);
    if (lua_tointeger(L, -1) != API_TYPE_AUTH) {
      lua_pushstring(L, "api: 'auth' is not an auth type");
      lua_error(L);
    }
    auth = lua_touserdata(L, -2);
    ep->auth = malloc(sizeof(api_auth_t));
    ep->auth->type = auth->type;
    switch (auth->type) {
    case API_AUTH_BASIC:
      ep->auth->basic = auth->basic;
      auth->basic = NULL;
      break;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_pushstring(L, "handle_response");
  lua_gettable(L, -3);
  if (!lua_isnil(L, -1)) {
    lua_dump(L, api_endpoint_handle_response_chunk_cb, ep, 0);
  }
  lua_pop(L, 1);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, api_endpoint_gc);
  lua_rawset(L, -3);
  lua_pushstring(L, "__index");
  lua_pushcfunction(L, api_endpoint_index);
  lua_rawset(L, -3);
  lua_pushstring(L, "__tostring");
  lua_pushcfunction(L, api_endpoint_tostring);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);
  return 1;
}

static int api_basic_auth(lua_State *L) {
  api_auth_t *auth = lua_newuserdata(L, sizeof(api_auth_t));
  const char *tmp;

  memset(auth, 0, sizeof(api_auth_t));

  lua_pushinteger(L, API_TYPE_AUTH);
  lua_setuservalue(L, -2);

  if (!lua_istable(L, -2)) {
    lua_pushstring(L, "basic: expects table as its argument");
    lua_error(L);
  }

  auth->type = API_AUTH_BASIC;
  auth->basic = calloc(1, sizeof(api_basic_auth_t));
  api_getstringfield(L, auth->basic->user, "user", -2, tmp);
  api_getstringfield(L, auth->basic->passwd, "password", -2, tmp);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, api_auth_gc);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);
  return 1;
}

static int api_url_encode(lua_State *L) {
  const char *tmp;
  size_t size;
  CURL *c;
  char *escaped;

  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pushstring(L, "url_encode expects strings argument");
    lua_error(L);
  }

  c = curl_easy_init();
  tmp = lua_tolstring(L, -1, &size);
  escaped = curl_easy_escape(c, tmp, size);
  if (escaped) {
    lua_pushstring(L, escaped);
  }
  curl_free(escaped);
  curl_easy_cleanup(c);

  return 1;
}
static void api_create_result(lua_State *L, api_request_t *req) {
  struct curl_slist *header;
  char *tmp, *name, *val, *content_type;
  int i, len;
  api_endpoint_t *ep;

  ep = req->endpoint;
  lua_newtable(L);
  if (req->resp->err) {
    lua_pushstring(L, req->resp->err);
    lua_setfield(L, -2, "err");
  } else {
    lua_pushinteger(L, req->resp->status);
    lua_setfield(L, -2, "status");
    content_type = NULL;
    lua_newtable(L);
    for (i = 0, header = req->resp->headers; header;
         i++, header = header->next) {
      tmp = strchr(header->data, ':');
      if (tmp) {
        len = tmp - header->data;
        name = malloc(len + 1);
        memcpy(name, header->data, len);
        name[len] = 0;
        lua_pushstring(L, name);
        while (isspace((int)(++tmp)[0]))
          ;
        len = strlen(tmp);
        while ((len > 0) && isspace((int)tmp[len - 1]))
          len--;
        val = malloc(len + 1);
        memcpy(val, tmp, len);
        val[len] = 0;
        lua_pushstring(L, val);
        lua_settable(L, -3);
        if (strcasecmp(name, API_HEADER_CONTENT_TYPE) == 0) {
          content_type = val;
          val = NULL;
        }
        free(name);
        free(val);
      }
    }
    lua_setfield(L, -2, "headers");
    lua_pushlstring(L, req->resp->body, req->resp->body_len);
    if (content_type) {
      if (strcmp(content_type, API_MIME_JSON) == 0) {
        api_from_json(L);
      }
      free(content_type);
    }
    lua_setfield(L, -2, "body");
  }
  lua_pushstring(L, req->resp->url);
  lua_setfield(L, -2, "url");
  lua_pushstring(L, api_method_str(req));
  lua_setfield(L, -2, "method");
  lua_pushnumber(L, req->resp->total_time);
  lua_setfield(L, -2, "total_time");

  if (ep->handle_response_chunk) {
    lua_pushvalue(L, -1);
    luaL_loadbuffer(L, ep->handle_response_chunk, ep->handle_response_chunk_len,
                    "handle_response");
    lua_rotate(L, -2, 1);
    lua_call(L, 1, 0);
  }
  if (req->handle_response_chunk) {
    lua_pushvalue(L, -1);
    luaL_loadbuffer(L, req->handle_response_chunk,
                    req->handle_response_chunk_len, "handle_response");
    lua_rotate(L, -2, 1);
    lua_call(L, 1, 0);
  }
}

static int api_send(lua_State *L) {
  int i, len;
  api_request_t *head = NULL, *req;
  char *err = NULL;
  int single_req = 0;

  switch (lua_type(L, -1)) {
  case LUA_TUSERDATA:
    lua_getuservalue(L, -1);
    if (lua_tointeger(L, -1) != API_TYPE_REQUEST) {
      lua_pushstring(L, "send: expects request as an argument");
      lua_error(L);
    }
    lua_pop(L, 1);
    head = lua_touserdata(L, -1);
    single_req = 1;
    break;
  case LUA_TTABLE:
    lua_len(L, -1);
    len = lua_tointeger(L, -1);
    lua_pop(L, 1);
    for (i = 1; i <= len; i++) {
      lua_geti(L, -1, i);
      req = lua_touserdata(L, -1);
      lua_pop(L, 1);
      DL_APPEND(head, req);
    }
    break;
  default:
    lua_pushstring(L, "send: expects request or list of requests");
    lua_error(L);
    break;
  }

  api_send_requests(head, &err);
  if (err) {
    lua_pushstring(L, err);
    free(err);
    lua_error(L);
  }

  if (single_req) {
    api_create_result(L, head);
  } else {
    lua_newtable(L);
    i = 1;
    DL_FOREACH(head, req) {
      api_create_result(L, req);
      lua_seti(L, -2, i);
      i++;
    }
  }
  return 1;
}

static int api_url_decode(lua_State *L) {
  const char *tmp;
  size_t size;
  CURL *c;
  char *unescaped;
  int len;

  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pushstring(L, "url_decode expects strings argument");
    lua_error(L);
  }

  c = curl_easy_init();
  tmp = lua_tolstring(L, -1, &size);
  unescaped = curl_easy_unescape(c, tmp, size, &len);
  if (unescaped) {
    lua_pushstring(L, unescaped);
  }
  curl_free(unescaped);
  curl_easy_cleanup(c);

  return 1;
}

lua_State *api_init(char **err) {
  lua_State *L;
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (res != 0) {
    *err = api_printf("%s", curl_easy_strerror(res));
    return NULL;
  }

  L = luaL_newstate();
  luaL_openlibs(L);

  // http constant
  api_setglobalstrconst(L, API_PROTO_HTTP_STR);

  // https constant
  api_setglobalstrconst(L, API_PROTO_HTTPS_STR);

  // GET constant
  api_setglobalstrconst(L, API_METHOD_GET_STR);

  // POST constant
  api_setglobalstrconst(L, API_METHOD_POST_STR);

  // PUT constant
  api_setglobalstrconst(L, API_METHOD_PUT_STR);

  // DELETE constant
  api_setglobalstrconst(L, API_METHOD_DELETE_STR);

  // endpoint function
  lua_register(L, "endpoint", api_endpoint);

  // basic function
  lua_register(L, "basic_auth", api_basic_auth);

  // send function
  lua_register(L, "send", api_send);

  // from_json function
  lua_register(L, "from_json", api_from_json);

  // to_json function
  lua_register(L, "to_json", api_to_json);

  // url_encode function
  lua_register(L, "url_encode", api_url_encode);

  // url_decode function
  lua_register(L, "url_decode", api_url_decode);

  return L;
}

void api_cleanup(lua_State *L) {
  if (L) {
    lua_close(L);
  }
  curl_global_cleanup();
}

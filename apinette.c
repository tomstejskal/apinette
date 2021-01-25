#include <cjson/cJSON.h>
#include <ctype.h>
#include <curl/curl.h>
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

#define l_getstringfield(dst, name, table_index, tmp)                          \
  lua_getfield(L, (table_index), (name));                                      \
  (tmp) = lua_tostring(L, -1);                                                 \
  if ((tmp)) {                                                                 \
    (dst) = malloc(strlen((tmp)) + 1);                                         \
    strcpy((dst), (tmp));                                                      \
  }                                                                            \
  lua_pop(L, 1);

#define l_setglobalstrconst(L, s)                                              \
  lua_pushstring(L, (s));                                                      \
  lua_setglobal(L, API_PROTO_HTTP_STR);

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

void api_init(char **err) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (res != 0) {
    *err = api_printf("%s", curl_easy_strerror(res));
    return;
  }
}

void api_cleanup(void) { curl_global_cleanup(); }

static char *api_proto_str(API_proto p) {
  switch (p) {
  case API_PROTO_HTTP:
    return API_PROTO_HTTP_STR;
  case API_PROTO_HTTPS:
    return API_PROTO_HTTP_STR;
  }
  return NULL;
}

static void api_add_header(API_request *req, char *name, char *val) {
  UT_string *s;

  utstring_new(s);
  utstring_printf(s, "%s: %s", name, val);
  req->headers = curl_slist_append(req->headers, utstring_body(s));

  utstring_free(s);
}

static void api_add_basic_auth(API_request *req, API_basic_auth *auth) {
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

static void api_add_auth(API_request *req) {
  if (req->api->auth) {
    switch (req->api->auth->typ) {
    case API_AUTH_BASIC:
      api_add_basic_auth(req, req->api->auth->basic);
      break;
    }
  }
}

static size_t api_write_body(char *ptr, size_t n, size_t l, API_request *req) {
  size_t len = n * l;

  req->resp->body = realloc(req->resp->body, req->resp->body_len + len);
  memcpy(req->resp->body + req->resp->body_len, ptr, len);
  req->resp->body_len += len;

  return len;
}

static size_t api_write_header(char *buf, size_t l, size_t n,
                               API_request *req) {
  size_t len = n * l;
  char *tmp;

  tmp = malloc(len + 1);
  memcpy(tmp, buf, len);
  tmp[len] = 0;
  req->resp->headers = curl_slist_append(req->resp->headers, tmp);
  free(tmp);

  return len;
}

static void api_add_request(CURLM *cm, API_request *req, char **err) {
  CURL *c;

  c = curl_easy_init();
  if (!c) {
    *err = api_printf(*err, "Cannot init request");
  }

  req->resp = malloc(sizeof(API_response));
  memset(req->resp, 0, sizeof(API_response));

  curl_easy_setopt(c, CURLOPT_VERBOSE, (long)req->api->verbose);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, api_write_body);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, req);
  curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, api_write_header);
  curl_easy_setopt(c, CURLOPT_HEADERDATA, req);
  req->resp->url = api_printf("%s://%s%s%s", api_proto_str(req->api->proto),
                              req->api->host, req->api->path, req->path);
  curl_easy_setopt(c, CURLOPT_URL, req->resp->url);
  curl_easy_setopt(c, CURLOPT_PRIVATE, req);

  api_add_header(req, API_HEADER_ACCEPT, API_MIME_JSON);

  switch (req->method) {
  case API_METHOD_GET:
    break;
  case API_METHOD_POST:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    api_add_header(req, API_HEADER_CONTENT_TYPE, API_MIME_JSON);
    break;
  case API_METHOD_PUT:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
    api_add_header(req, API_HEADER_CONTENT_TYPE, API_MIME_JSON);
    break;
  case API_METHOD_DELETE:
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  }

  api_add_auth(req);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, req->headers);
  curl_multi_add_handle(cm, c);
}

void api_send(API_request *head, char **err) {
  CURLM *cm;
  CURLMsg *msg;
  int running = 1;
  int msgs_left = -1;
  API_request *req;

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
      API_request *req;
      curl_easy_getinfo(c, CURLINFO_PRIVATE, &req);
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

      if (running) {
        curl_multi_wait(cm, NULL, 0, 100, NULL);
      }
    }
  }

  curl_multi_cleanup(cm);
}

API_request *api_new_request(API_api *api, API_method method, char *path) {
  API_request *req;

  req = malloc(sizeof(API_request));
  memset(req, 0, sizeof(API_request));
  req->api = api;
  req->method = method;
  req->path = path;

  return req;
}

static int l_api_gc(lua_State *L) {
  API_api *api = lua_touserdata(L, -1);

  if (api->auth) {
    switch (api->auth->typ) {
    case API_AUTH_BASIC:
      if (api->auth->basic) {
        free(api->auth->basic->user);
        free(api->auth->basic->passwd);
        free(api->auth->basic);
      }
      break;
    }
    free(api->auth);
  }
  free(api->host);
  free(api->path);

  return 0;
}

static int l_request_gc(lua_State *L) {
  API_request *req = lua_touserdata(L, -1);

  free(req->path);
  curl_slist_free_all(req->headers);
  free(req->body);
  if (req->resp) {
    curl_slist_free_all(req->resp->headers);
    free(req->resp->body);
    free(req->resp->err);
    free(req->resp->url);
    free(req->resp);
  }

  return 0;
}

static int l_create_request(lua_State *L, API_method method) {
  API_request *req = lua_newuserdatauv(L, sizeof(API_request), 1);
  API_api *api;
  const char *k, *v, *tmp;
  char *header;

  memset(req, 0, sizeof(API_request));

  api = lua_touserdata(L, lua_upvalueindex(1));
  req->api = api;
  req->method = method;

  if (lua_istable(L, -2)) {
    l_getstringfield(req->path, "path", -2, tmp);
    l_getstringfield(req->body, "body", -2, tmp);
    if (req->body) {
      req->body_len = strlen(req->body);
    }
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
  } else {
    tmp = (char *)lua_tostring(L, -2);
    req->path = malloc(strlen(tmp) + 1);
    strcpy(req->path, tmp);
  }

  lua_pushinteger(L, API_TYPE_REQUEST);
  lua_setiuservalue(L, -2, 1);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, l_request_gc);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);

  return 1;
}

static int l_api_get(lua_State *L) {
  return l_create_request(L, API_METHOD_GET);
}

static int l_api_post(lua_State *L) {
  return l_create_request(L, API_METHOD_POST);
}

static int l_api_put(lua_State *L) {
  return l_create_request(L, API_METHOD_PUT);
}

static int l_api_delete(lua_State *L) {
  return l_create_request(L, API_METHOD_DELETE);
}

static int l_api_index(lua_State *L) {
  const char *field = lua_tostring(L, -1);
  if (strcmp(field, "get") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, l_api_get, 1); // API_api in a closure
  } else if (strcmp(field, "post") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, l_api_post, 1); // API_api in a closure
  } else if (strcmp(field, "put") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, l_api_put, 1); // API_api in a closure
  } else if (strcmp(field, "delete") == 0) {
    lua_pop(L, 1);
    lua_pushcclosure(L, l_api_delete, 1); // API_api in a closure
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int l_api_tostring(lua_State *L) {
  API_api *api = lua_touserdata(L, -1);
  lua_pushfstring(L, "api: %s", api->host);
  return 1;
}

static int l_auth_gc(lua_State *L) {
  API_auth *auth = lua_touserdata(L, -1);

  switch (auth->typ) {
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

static int l_api(lua_State *L) {
  API_api *api = lua_newuserdatauv(L, sizeof(API_api), 1);
  const char *s;
  API_auth *auth;

  memset(api, 0, sizeof(API_api));

  lua_pushinteger(L, API_TYPE_API);
  lua_setiuservalue(L, -2, 1);

  if (!lua_istable(L, -2)) {
    lua_pushstring(L, "api: expects table as its argument");
    lua_error(L);
  }

  lua_pushstring(L, "proto");
  lua_gettable(L, -3);
  s = lua_tostring(L, -1);
  if (strcmp(s, API_PROTO_HTTP_STR) == 0) {
    api->proto = API_PROTO_HTTP;
  } else if (strcmp(s, API_PROTO_HTTPS_STR) == 0) {
    api->proto = API_PROTO_HTTPS;
  } else {
    lua_pushstring(L, "api: 'proto' should be http or https");
    lua_error(L);
  }
  lua_pop(L, 1);

  l_getstringfield(api->host, "host", -2, s);
  l_getstringfield(api->path, "path", -2, s);

  lua_getfield(L, -2, "verbose");
  api->verbose = lua_toboolean(L, -1);
  lua_pop(L, 1);

  lua_pushstring(L, "auth");
  lua_gettable(L, -3);
  if (!lua_isnil(L, -1)) {
    lua_getiuservalue(L, -1, 1);
    if (lua_tointeger(L, -1) != API_TYPE_AUTH) {
      lua_pushstring(L, "api: 'auth' is not an auth type");
      lua_error(L);
    }
    auth = lua_touserdata(L, -2);
    api->auth = malloc(sizeof(API_auth));
    api->auth->typ = auth->typ;
    switch (auth->typ) {
    case API_AUTH_BASIC:
      api->auth->basic = auth->basic;
      auth->basic = NULL;
      break;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, l_api_gc);
  lua_rawset(L, -3);
  lua_pushstring(L, "__index");
  lua_pushcfunction(L, l_api_index);
  lua_rawset(L, -3);
  lua_pushstring(L, "__tostring");
  lua_pushcfunction(L, l_api_tostring);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);
  return 1;
}

static int l_basic(lua_State *L) {
  API_auth *auth = lua_newuserdatauv(L, sizeof(API_auth), 1);
  const char *tmp;

  memset(auth, 0, sizeof(API_auth));

  lua_pushinteger(L, API_TYPE_AUTH);
  lua_setiuservalue(L, -2, 1);

  if (!lua_istable(L, -2)) {
    lua_pushstring(L, "basic: expects table as its argument");
    lua_error(L);
  }

  auth->typ = API_AUTH_BASIC;
  auth->basic = malloc(sizeof(API_basic_auth));
  memset(auth->basic, 0, sizeof(API_basic_auth));
  l_getstringfield(auth->basic->user, "user", -2, tmp);
  l_getstringfield(auth->basic->passwd, "password", -2, tmp);

  lua_newtable(L);
  lua_pushstring(L, "__gc");
  lua_pushcfunction(L, l_auth_gc);
  lua_rawset(L, -3);
  lua_setmetatable(L, -2);
  return 1;
}
static void l_create_result(lua_State *L, API_request *req) {
  struct curl_slist *header;
  char *tmp;
  int i, len;

  lua_newtable(L);
  if (req->resp->err) {
    lua_pushstring(L, req->resp->err);
    lua_setfield(L, -2, "err");
  } else {
    lua_pushinteger(L, req->resp->status);
    lua_setfield(L, -2, "status");
    lua_pushlstring(L, req->resp->body, req->resp->body_len);
    lua_setfield(L, -2, "body");
    lua_newtable(L);
    for (i = 0, header = req->resp->headers; header;
         i++, header = header->next) {
      tmp = strchr(header->data, ':');
      if (tmp) {
        lua_pushlstring(L, header->data, tmp - header->data);
        while (isspace((++tmp)[0]))
          ;
        len = strlen(tmp);
        while ((len > 0) && isspace(tmp[len - 1]))
          len--;
        lua_pushlstring(L, tmp, len);
        lua_settable(L, -3);
      }
    }
    lua_setfield(L, -2, "headers");
    lua_pushstring(L, req->resp->url);
    lua_setfield(L, -2, "url");
  }
}

static int l_send(lua_State *L) {
  int i, len;
  API_request *head = NULL, *req;
  char *err = NULL;
  int single_req = 0;

  switch (lua_type(L, -1)) {
  case LUA_TUSERDATA:
    lua_getiuservalue(L, -1, 1);
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

  api_send(head, &err);
  if (err) {
    lua_pushstring(L, err);
    free(err);
    lua_error(L);
  }

  if (single_req) {
    l_create_result(L, head);
  } else {
    lua_newtable(L);
    i = 1;
    DL_FOREACH(head, req) {
      l_create_result(L, req);
      lua_seti(L, -2, i);
      i++;
    }
  }
  return 1;
}

static void l_read_json(lua_State *L, cJSON *json) {
  cJSON *item;
  int i;

  if (cJSON_IsFalse(json)) {
    lua_pushboolean(L, 0);
  } else if (cJSON_IsTrue(json)) {
    lua_pushboolean(L, 1);
  } else if (cJSON_IsNull(json)) {
    lua_pushnil(L);
  } else if (cJSON_IsNumber(json)) {
    lua_pushnumber(L, json->valuedouble);
  } else if (cJSON_IsString(json)) {
    lua_pushstring(L, json->valuestring);
  } else if (cJSON_IsArray(json)) {
    lua_newtable(L);
    i = 1;
    cJSON_ArrayForEach(item, json) {
      l_read_json(L, item);
      lua_seti(L, -2, i);
      i++;
    }
  } else if (cJSON_IsObject(json)) {
    lua_newtable(L);
    cJSON_ArrayForEach(item, json) {
      l_read_json(L, item);
      lua_setfield(L, -2, item->string);
    }
  } else {
    lua_pushnil(L);
  }
}

static cJSON *l_write_json(lua_State *L) {
  int i, len;
  cJSON *json, *item;
  const char *name;

  switch (lua_type(L, -1)) {
  case LUA_TNIL:
    json = cJSON_CreateNull();
    break;
  case LUA_TBOOLEAN:
    json = cJSON_CreateBool(lua_toboolean(L, -1));
    break;
  case LUA_TNUMBER:
    json = cJSON_CreateNumber(lua_tonumber(L, -1));
    break;
  case LUA_TSTRING:
    json = cJSON_CreateString(lua_tostring(L, -1));
    break;
  case LUA_TTABLE:
    lua_len(L, -1);
    len = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (len > 0) {
      // array
      json = cJSON_CreateArray();
      for (i = 1; i <= len; i++) {
        lua_geti(L, -1, i);
        item = l_write_json(L);
        cJSON_AddItemToArray(json, item);
      }
    } else {
      // object
      json = cJSON_CreateObject();
      lua_pushnil(L);
      while (lua_next(L, -2)) {
        item = l_write_json(L);
        name = lua_tostring(L, -1);
        cJSON_AddItemToObject(json, name, item);
      }
    }
    break;
  }

  lua_pop(L, 1);
  return json;
}

static int l_from_json(lua_State *L) {
  cJSON *json;
  const char *tmp;

  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pushstring(L, "from_json: expecting string as an argument");
    lua_error(L);
  }

  json = cJSON_ParseWithOpts(lua_tostring(L, -1), &tmp, 1);
  if (!json) {
    lua_pushfstring(L, "from_json: error in json at '%s'", tmp);
    lua_error(L);
  }

  l_read_json(L, json);
  cJSON_Delete(json);
  return 1;
}

static int l_to_json(lua_State *L) {
  cJSON *json;
  char *tmp;

  json = l_write_json(L);
  tmp = cJSON_Print(json);
  lua_pushstring(L, tmp);
  free(tmp);
  cJSON_Delete(json);

  return 1;
}

static int l_use(lua_State *L) {
  int err;

  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pushstring(L, "use: expected string as an argument");
    lua_error(L);
  }

  err = luaL_loadfile(L, lua_tostring(L, -1));
  if (err) {
    lua_pushfstring(L, "%s", lua_tostring(L, -1));
    lua_error(L);
  }

  lua_call(L, 0, LUA_MULTRET);
  return lua_gettop(L);
}

void api_init_lua(lua_State *L) {
  // http constant
  l_setglobalstrconst(L, API_PROTO_HTTP_STR);

  // https constant
  l_setglobalstrconst(L, API_PROTO_HTTPS_STR);

  // api function
  lua_register(L, "api", l_api);

  // basic function
  lua_register(L, "basic", l_basic);

  // send function
  lua_register(L, "send", l_send);

  // from_json function
  lua_register(L, "from_json", l_from_json);

  // to_json function
  lua_register(L, "to_json", l_to_json);

  // use function
  lua_register(L, "use", l_use);
}

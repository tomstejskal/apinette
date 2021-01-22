#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "utlist.h"
#include "utstring.h"

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

static void init(UT_string **err) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (res != 0) {
    utstring_new(*err);
    utstring_printf(*err, "%s", curl_easy_strerror(res));
    return;
  }
}

static void cleanup(void) { curl_global_cleanup(); }

static char *proto_str(proto p) {
  switch (p) {
  case PROTO_HTTP:
    return "http";
  case PROTO_HTTPS:
    return "https";
  }
  return NULL;
}

static void add_header(request *req, char *name, char *val) {
  UT_string *s;

  utstring_new(s);
  utstring_printf(s, "%s: %s", name, val);
  req->headers = curl_slist_append(req->headers, utstring_body(s));

  utstring_free(s);
}

static void add_basic_auth(request *req, basic_auth *auth) {
  UT_string *s;
  char *base64;
  size_t base64_len;

  utstring_new(s);
  utstring_printf(s, "%s:%s", auth->user, auth->passwd);
  base64 = (char *)base64_encode((const unsigned char *)utstring_body(s),
                                 utstring_len(s), &base64_len);
  utstring_clear(s);
  utstring_printf(s, "Basic %s", base64);

  add_header(req, HEADER_AUTHORIZATION, utstring_body(s));

  utstring_free(s);
  free(base64);
}

static void add_auth(request *req) {
  switch (req->api->auth_type) {
  case AUTH_NONE:
    break;
  case AUTH_BASIC:
    add_basic_auth(req, (basic_auth *)req->api->auth);
  }
}

static size_t write_cb(char *ptr, size_t n, size_t l, request *req) {
  size_t len;

  len = n * l;
  req->resp->body = realloc(req->resp->body, req->resp->body_len + len);
  memcpy(req->resp->body + req->resp->body_len, ptr, len);
  req->resp->body_len += len;

  return len;
}

static void add_request(CURLM *cm, request *req, UT_string **err) {
  CURL *c;
  UT_string *url;

  c = curl_easy_init();
  if (!c) {
    utstring_new(*err);
    utstring_printf(*err, "Cannot init request");
  }

  req->resp = malloc(sizeof(response));
  req->resp->status = 0;
  req->resp->headers = NULL;
  req->resp->body = NULL;
  req->resp->body_len = 0;

  // curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, req);
  utstring_new(url);
  utstring_printf(url, "%s://%s%s%s", proto_str(req->api->proto),
                  req->api->host, req->api->path, req->path);
  curl_easy_setopt(c, CURLOPT_URL, utstring_body(url));
  curl_easy_setopt(c, CURLOPT_PRIVATE, req);

  add_header(req, HEADER_ACCEPT, MIME_JSON);

  switch (req->method) {
  case METHOD_GET:
    break;
  case METHOD_POST:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    add_header(req, HEADER_CONTENT_TYPE, MIME_JSON);
    break;
  case METHOD_PUT:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
    add_header(req, HEADER_CONTENT_TYPE, MIME_JSON);
    break;
  case METHOD_DELETE:
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  }

  add_auth(req);

  curl_easy_setopt(c, CURLOPT_HTTPHEADER, req->headers);

  curl_multi_add_handle(cm, c);
}

static void parallel(request *reqs, UT_string **err) {
  CURLM *cm;
  CURLMsg *msg;
  int running = 1;
  int msgs_left = -1;
  request *req;

  cm = curl_multi_init();
  if (!cm) {
    utstring_new(*err);
    utstring_printf(*err, "Cannot init parallel requests");
    return;
  }

  DL_FOREACH(reqs, req) {
    add_request(cm, req, err);
    if (*err) {
      return;
    }
  }

  while (running) {
    curl_multi_perform(cm, &running);

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        CURL *c = msg->easy_handle;
        request *req;
        long status;
        curl_easy_getinfo(c, CURLINFO_PRIVATE, &req);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        req->resp->status = (int)status;
        if (msg->data.result > 0) {
          fprintf(stderr, "%s - %s\n", req->path,
                  curl_easy_strerror(msg->data.result));
        } else if ((status < 200) || (status >= 400)) {
          fprintf(stderr, "%s - status %ld\n", req->path, status);
          cJSON *json =
              cJSON_ParseWithLength(req->resp->body, req->resp->body_len);
          if (json) {
            char *s = cJSON_Print(json);
            fprintf(stderr, "%s\n", s);
            free(s);
          }
          cJSON_Delete(json);
        }
        curl_multi_remove_handle(cm, c);
        curl_easy_cleanup(c);
      } else {
        fprintf(stderr, "%d\n", msg->msg);
      }

      if (running) {
        curl_multi_wait(cm, NULL, 0, 100, NULL);
      }
    }
  }

  curl_multi_cleanup(cm);
}

static request *new_request(api *api, method method, char *path) {
  request *req;

  req = malloc(sizeof(request));
  memset(req, 0, sizeof(request));
  req->api = api;
  req->method = method;
  req->path = path;

  return req;
}

int main(int argc, char **argv) {
  UT_string *err = NULL;
  api api;
  basic_auth *auth;
  request *reqs = NULL;
  request *req;

  (void)argc;
  (void)argv;
  init(&err);
  if (err) {
    fprintf(stderr, "Error: %s\n", utstring_body(err));
    utstring_free(err);
    return EXIT_FAILURE;
  }

  api.proto = PROTO_HTTP;
  api.host = "localhost:8081";
  api.path = "/data";
  api.auth_type = AUTH_BASIC;
  auth = malloc(sizeof(basic_auth));
  auth->user = "Supervisor";
  auth->passwd = "";
  api.auth = auth;

  req = new_request(&api, METHOD_GET, "/firms");
  DL_APPEND(reqs, req);
  req = new_request(&api, METHOD_GET, "/issuedinvoices");
  DL_APPEND(reqs, req);
  req = new_request(&api, METHOD_GET, "/issuedorders");
  DL_APPEND(reqs, req);
  req = new_request(&api, METHOD_GET, "/receivedorders");
  DL_APPEND(reqs, req);
  req = new_request(&api, METHOD_GET, "/storecards");
  DL_APPEND(reqs, req);
  req = new_request(&api, METHOD_GET, "/storeprices");
  DL_APPEND(reqs, req);

  parallel(reqs, &err);

  cleanup();
  return EXIT_SUCCESS;
}

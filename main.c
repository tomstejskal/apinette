#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utstring.h>

#include "base64.h"

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

typedef struct {
  api *api;
  method method;
  char *path;
  struct curl_slist *headers;
  char *body;
  size_t body_len;
  response *resp;
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
  req->body = realloc(req->body, req->body_len + len);
  memcpy(req->body + req->body_len, ptr, len);
  req->body_len += len;

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

  curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
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

static void parallel(request **reqs, int req_len, UT_string **err) {
  CURLM *cm;
  CURLMsg *msg;
  int i;
  int running = 1;
  int msgs_left = -1;

  cm = curl_multi_init();
  if (!cm) {
    utstring_new(*err);
    utstring_printf(*err, "Cannot init parallel requests");
    return;
  }

  for (i = 0; i < req_len; i++) {
    add_request(cm, reqs[i], err);
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

int main(int argc, char **argv) {
  UT_string *err = NULL;
  api api;
  basic_auth *auth;
  request *reqs[4];

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

  reqs[0] = malloc(sizeof(request));
  memset(reqs[0], 0, sizeof(request));
  reqs[0]->api = &api;
  reqs[0]->method = METHOD_GET;
  reqs[0]->path = "/firms";

  reqs[1] = malloc(sizeof(request));
  memset(reqs[1], 0, sizeof(request));
  reqs[1]->api = &api;
  reqs[1]->method = METHOD_GET;
  reqs[1]->path = "/issuedinvoices";

  reqs[2] = malloc(sizeof(request));
  memset(reqs[2], 0, sizeof(request));
  reqs[2]->api = &api;
  reqs[2]->method = METHOD_GET;
  reqs[2]->path = "/firms";

  reqs[3] = malloc(sizeof(request));
  memset(reqs[3], 0, sizeof(request));
  reqs[3]->api = &api;
  reqs[3]->method = METHOD_GET;
  reqs[3]->path = "/issuedinvoices";

  parallel(reqs, 4, &err);

  cleanup();
  return EXIT_SUCCESS;
}

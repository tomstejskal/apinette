#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "apinette.h"
#include "base64.h"
#include "utlist.h"
#include "utstring.h"

char *apinette_printf(char *format, ...) {
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

void apinette_init(char **err) {
  CURLcode res;

  res = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (res != 0) {
    *err = apinette_printf("%s", curl_easy_strerror(res));
    return;
  }
}

void apinette_cleanup(void) { curl_global_cleanup(); }

static char *apinette_proto_str(proto p) {
  switch (p) {
  case PROTO_HTTP:
    return "http";
  case PROTO_HTTPS:
    return "https";
  }
  return NULL;
}

static void apinette_add_header(request *req, char *name, char *val) {
  UT_string *s;

  utstring_new(s);
  utstring_printf(s, "%s: %s", name, val);
  req->headers = curl_slist_append(req->headers, utstring_body(s));

  utstring_free(s);
}

static void apinette_add_basic_auth(request *req, basic_auth *auth) {
  UT_string *s;
  char *base64;
  size_t base64_len;

  utstring_new(s);
  utstring_printf(s, "%s:%s", auth->user, auth->passwd);
  base64 = (char *)base64_encode((const unsigned char *)utstring_body(s),
                                 utstring_len(s), &base64_len);
  utstring_clear(s);
  utstring_printf(s, "Basic %s", base64);

  apinette_add_header(req, HEADER_AUTHORIZATION, utstring_body(s));

  utstring_free(s);
  free(base64);
}

static void apinette_add_auth(request *req) {
  switch (req->api->auth_type) {
  case AUTH_NONE:
    break;
  case AUTH_BASIC:
    apinette_add_basic_auth(req, (basic_auth *)req->api->auth);
  }
}

static size_t apinette_write_body(char *ptr, size_t n, size_t l, request *req) {
  size_t len;

  len = n * l;
  req->resp->body = realloc(req->resp->body, req->resp->body_len + len);
  memcpy(req->resp->body + req->resp->body_len, ptr, len);
  req->resp->body_len += len;

  return len;
}

static void apinette_add_request(CURLM *cm, request *req, char **err) {
  CURL *c;
  UT_string *url;

  c = curl_easy_init();
  if (!c) {
    *err = apinette_printf(*err, "Cannot init request");
  }

  req->resp = malloc(sizeof(response));
  memset(req->resp, 0, sizeof(response));

  // curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, apinette_write_body);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, req);
  utstring_new(url);
  utstring_printf(url, "%s://%s%s%s", apinette_proto_str(req->api->proto),
                  req->api->host, req->api->path, req->path);
  curl_easy_setopt(c, CURLOPT_URL, utstring_body(url));
  utstring_free(url);
  curl_easy_setopt(c, CURLOPT_PRIVATE, req);

  apinette_add_header(req, HEADER_ACCEPT, MIME_JSON);

  switch (req->method) {
  case METHOD_GET:
    break;
  case METHOD_POST:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    apinette_add_header(req, HEADER_CONTENT_TYPE, MIME_JSON);
    break;
  case METHOD_PUT:
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
    apinette_add_header(req, HEADER_CONTENT_TYPE, MIME_JSON);
    break;
  case METHOD_DELETE:
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  }

  apinette_add_auth(req);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, req->headers);
  curl_multi_add_handle(cm, c);
}

void apinette_parallel(request *head, char **err) {
  CURLM *cm;
  CURLMsg *msg;
  int running = 1;
  int msgs_left = -1;
  request *req;

  cm = curl_multi_init();
  if (!cm) {
    *err = apinette_printf("Cannot init parallel requests");
    return;
  }

  DL_FOREACH(head, req) {
    apinette_add_request(cm, req, err);
    if (*err) {
      return;
    }
  }

  while (running) {
    curl_multi_perform(cm, &running);

    while ((msg = curl_multi_info_read(cm, &msgs_left))) {
      CURL *c = msg->easy_handle;
      request *req;
      curl_easy_getinfo(c, CURLINFO_PRIVATE, &req);
      if (msg->msg == CURLMSG_DONE) {
        long status;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        req->resp->status = (int)status;
        if (msg->data.result > 0) {
          req->resp->err =
              apinette_printf("%s", curl_easy_strerror(msg->data.result));
        }
        curl_multi_remove_handle(cm, c);
        curl_easy_cleanup(c);
      } else {
        req->resp->err =
            apinette_printf("Unexpected message type: %d", msg->msg);
      }

      if (running) {
        curl_multi_wait(cm, NULL, 0, 100, NULL);
      }
    }
  }

  curl_multi_cleanup(cm);
}

request *apinette_new_request(api *api, method method, char *path) {
  request *req;

  req = malloc(sizeof(request));
  memset(req, 0, sizeof(request));
  req->api = api;
  req->method = method;
  req->path = path;

  return req;
}

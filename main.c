#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apinette.h"
#include "base64.h"
#include "utlist.h"
#include "utstring.h"

int main(int argc, char **argv) {
  char *err = NULL;
  api api;
  basic_auth *auth;
  request *head = NULL;

  (void)argc;
  (void)argv;
  apinette_init(&err);
  if (err) {
    fprintf(stderr, "Error: %s\n", err);
    free(err);
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

  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/firms"));
  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/issuedinvoices"));
  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/issuedorders"));
  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/receivedorders"));
  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/storecards"));
  head = apinette_append_request(
      head, apinette_new_request(&api, METHOD_GET, "/storeprices"));

  apinette_parallel(head, &err);

  if (err) {
    fprintf(stderr, "%s\n", err);
    return EXIT_FAILURE;
  }

  apinette_cleanup();
  return EXIT_SUCCESS;
}

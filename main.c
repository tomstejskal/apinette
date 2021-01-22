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
  request *head = NULL, *req, *tmp;
  int res = EXIT_SUCCESS;

  (void)argc;
  (void)argv;
  apinette_init(&err);
  if (err) {
    fprintf(stderr, "Error: %s\n", err);
    free(err);
    res = EXIT_FAILURE;
    goto end;
  }

  api.proto = PROTO_HTTP;
  api.host = "localhost:8081";
  api.path = "/data";
  api.auth_type = AUTH_BASIC;
  auth = malloc(sizeof(basic_auth));
  auth->user = "Supervisor";
  auth->passwd = "";
  api.auth = auth;

  req = apinette_new_request(&api, METHOD_GET, "/firms");
  DL_APPEND(head, req);
  req = apinette_new_request(&api, METHOD_GET, "/issuedinvoices");
  DL_APPEND(head, req);
  req = apinette_new_request(&api, METHOD_GET, "/issuedorders");
  DL_APPEND(head, req);
  req = apinette_new_request(&api, METHOD_GET, "/receivedorders");
  DL_APPEND(head, req);
  req = apinette_new_request(&api, METHOD_GET, "/storecards");
  DL_APPEND(head, req);
  req = apinette_new_request(&api, METHOD_GET, "/storeprices");
  DL_APPEND(head, req);

  apinette_parallel(head, &err);

  if (err) {
    res = EXIT_FAILURE;
    goto end;
  }

end:
  DL_FOREACH_SAFE(head, req, tmp) {
    DL_DELETE(head, req);
    free(req->resp);
    free(req);
  }
  free(api.auth);
  apinette_cleanup();
  return res;
}

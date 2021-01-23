#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
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
  int res = EXIT_SUCCESS;
  char *script;
  lua_State *L = NULL;
  int lua_err;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s SCRIPT\n", argv[0]);
    res = EXIT_FAILURE;
    goto end;
  }
  script = argv[1];

  api_init(&err);

  L = luaL_newstate();
  luaL_openlibs(L);
  api_init_lua(L);
  lua_err = luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0);
  if (lua_err) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    res = EXIT_FAILURE;
    goto end;
  }

end:
  if (L) {
    lua_close(L);
  }
  api_cleanup();
  return res;
}

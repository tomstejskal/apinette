#include <curl/curl.h>
#include <lauxlib.h>
#include <libgen.h>
#include <lua.h>
#include <lualib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apinette.h"
#include "base64.h"
#include "linenoise.h"
#include "utlist.h"
#include "utstring.h"

#define PROGNAME "apinette"
#define HISTORY_FILE ".apinette_history"

int repl(lua_State *L) {
  char *line;
  int lua_err;

  linenoiseSetMultiLine(1);
  linenoiseHistoryLoad(HISTORY_FILE);
  while ((line = linenoise(PROGNAME "> "))) {
    lua_err = luaL_loadstring(L, line) || lua_pcall(L, 0, 0, 0);
    if (lua_err) {
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
      lua_pop(L, 1);
    }
    linenoiseHistoryAdd(line);
    linenoiseHistorySave(HISTORY_FILE);
    free(line);
  }
  return EXIT_SUCCESS;
}

int run_script(lua_State *L, const char *script) {
  char *tmp;
  char *dir;
  const char *path;
  int lua_err;

  tmp = malloc(strlen(script) + 1);
  strcpy(tmp, script);
  dir = dirname(tmp);
  if (dir && strlen(dir)) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    path = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_pushfstring(L, "%s;%s/?.lua", path, dir);
    lua_setfield(L, -2, "path");
  }
  free(tmp);
  lua_err = luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0);
  if (lua_err) {
    fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
  char *err = NULL;
  int res = EXIT_SUCCESS;
  lua_State *L = NULL;

  if (argc > 2) {
    fprintf(stderr, "Usage: %s SCRIPT\n", argv[0]);
    return EXIT_FAILURE;
  }

  L = api_init(&err);

  if (argc == 1) {
    res = repl(L);
  } else {
    res = run_script(L, argv[1]);
  }

  api_cleanup(L);
  return res;
}

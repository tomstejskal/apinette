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
#include "linenoise/linenoise.h"
#include "utlist.h"
#include "utstring.h"

#define PROGNAME "apinette"
#define HISTORY_FILE ".history"

int main(int argc, char **argv) {
  char *err = NULL;
  int res = EXIT_SUCCESS;
  char *script;
  lua_State *L = NULL;
  int lua_err;
  char *line;
  char *brk;
  char *tok;
  char *tmp;
  char *dir;
  const char *path;
  int len;

  if (argc > 2) {
    fprintf(stderr, "Usage: %s SCRIPT\n", argv[0]);
    res = EXIT_FAILURE;
    goto end;
  }

  L = api_init(&err);

  linenoiseSetMultiLine(1);
  linenoiseHistoryLoad(HISTORY_FILE);

  if (argc == 1) {
    while ((line = linenoise(PROGNAME "> "))) {
      tmp = NULL;
      brk = strpbrk(line, " \t\r\n({");
      if (brk) {
        len = brk - line;
      } else {
        len = strlen(line);
      }
      tok = malloc(len + 1);
      strncpy(tok, line, len);
      tok[len] = 0;
      if (!((strcmp(tok, "print") == 0) || (strcmp(tok, "if") == 0) ||
            (strcmp(tok, "for") == 0) || (strcmp(tok, "while") == 0) ||
            (strcmp(tok, "function") == 0))) {
        tmp = api_printf("print(%s)", line);
      }
      free(tok);
      if (!tmp) {
        tmp = line;
      }
      lua_err = luaL_loadstring(L, tmp) || lua_pcall(L, 0, 0, 0);
      if (lua_err) {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
      }
      linenoiseHistoryAdd(line);
      linenoiseHistorySave(HISTORY_FILE);
      if (tmp != line) {
        free(tmp);
      }
      free(line);
    }
  } else {
    script = argv[1];
    tmp = malloc(strlen(script) + 1);
    strcpy(tmp, script);
    dir = dirname(tmp);
    if (dir && strlen(dir)) {
      lua_getglobal(L, "package");
      lua_getfield(L, -1, "path");
      path = lua_tostring(L, -1);
      lua_pop(L, 1);
      lua_pushfstring(L, "%s;%s/?/?.lua", path, dir);
      lua_setfield(L, -2, "path");
    }
    free(tmp);
    lua_err = luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0);
    if (lua_err) {
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
      lua_pop(L, 1);
      res = EXIT_FAILURE;
      goto end;
    }
  }

end:
  api_cleanup(L);
  return res;
}

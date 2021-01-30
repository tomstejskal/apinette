#ifndef APINETTE_H

#include <lua.h>

char *api_printf(char *format, ...);

lua_State *api_init(char **err);

void api_cleanup(lua_State *L);

#endif // APINETTE_H

#ifndef KICKCAT_LUA_BINDINGS_H
#define KICKCAT_LUA_BINDINGS_H

extern "C"
{
    #include "lua.h"

    int luaopen_kickcat(lua_State* L);
}

namespace kickcat
{

    // Call at init to load bindings: new bindings have to be added inside bindings/load.cc).
    void load_kickcat_bindings(lua_State* L);

    // Lua bindings
    int luaload_socket(lua_State* L);
    int luaload_link(lua_State* L);
}

#endif

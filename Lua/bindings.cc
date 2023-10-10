#include "bindings.h"

namespace kickcat
{
    // Lua bindings
    void luaload_socket(lua_State* L);
    void luaload_link(lua_State* L);
    void luaload_bus(lua_State* L);

    static const luaL_Reg fcts[] =
    {
        { nullptr,  nullptr }
    };
}

extern "C" int luaopen_kickcat(lua_State* L)
{
    luaL_newlib(L, kickcat::fcts);
    lua_pushstring(L, "1.0.0");
    lua_setfield(L, -2, "_VERSION");

    kickcat::luaload_socket(L);
    kickcat::luaload_link(L);
    kickcat::luaload_bus(L);

    return 1;
}

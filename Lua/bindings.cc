#include "bindings.h"
#include "helpers.h"

extern "C"
{
    #include "lauxlib.h"
}

namespace kickcat
{
    static const luaL_Reg fcts[] =
    {
        { nullptr,              nullptr         }
    };

    void load_kickcat_bindings(lua_State* L)
    {
        luaL_requiref(L, "kickcat",    luaload_socket,    0);
    }
}

extern "C" int luaopen_kickcat(lua_State* L)
{
    luaL_newlib(L, kickcat::fcts);
    lua_pushstring(L, "1.0.0");
    lua_setfield(L, -2, "_VERSION");

    kickcat::luaload_socket(L);
    lua_setfield(L, -2, "socket");

    return 1;
}

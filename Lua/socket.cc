#include <memory>

#include "helpers.h"
#include "bindings.h"
#include "kickcat/AbstractSocket.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

namespace kickcat
{
    static int socket_open(lua_State* L)
    {
        auto socket = *static_cast<std::shared_ptr<AbstractSocket>*>(luaL_checkudata(L, 1, "kickcat::socket"));
        std::string interface = luaL_checkstring(L, 2);

        try
        {
            socket->open(interface);
        }
        catch (Error const& error)
        {
            // TODO: return error object?
            luaL_error(L, "socket open(): %s", error.what());
        }
        return 0;
    }

    static int socket_close(lua_State* L)
    {
        auto socket = *static_cast<std::shared_ptr<AbstractSocket>*>(luaL_checkudata(L, 1, "kickcat::socket"));
        socket->close();
        return 0;
    }

    static int socket_set_timeout(lua_State* L)
    {
        auto socket = *static_cast<std::shared_ptr<AbstractSocket>*>(luaL_checkudata(L, 1, "kickcat::socket"));
        microseconds timeout{luaL_checkinteger(L, 2)};

        socket->setTimeout(timeout);
        return 0;
    }

    static int socket_constructor(lua_State* L)
    {
        createClassForLua<std::shared_ptr<Socket>>(L, "kickcat::socket");
        return 1;
    }

    int luaload_socket(lua_State* L)
    {
        registerClass<std::shared_ptr<AbstractSocket>>(L, "kickcat::socket",
        {
            {"open",        socket_open         },
            {"close",       socket_close        },
            {"set_timeout", socket_set_timeout  },
        });

        lua_pushcfunction(L, socket_constructor);
        return 1;
    }
}

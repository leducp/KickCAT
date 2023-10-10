#include "bindings.h"
#include "protocol.h"
#include "kickcat/Link.h"

namespace kickcat
{
    static int link_read_reg32(lua_State* L)
    {
        auto link = *static_cast<std::shared_ptr<Link>*>(luaL_checkudata(L, 1, "kickcat::Link"));
        uint16_t pos = luaL_checkinteger(L, 2);
        uint16_t reg = luaL_checkinteger(L, 3);

        uint32_t data;
        auto callback = [L](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            lua_pushinteger(L, wkc);
            return DatagramState::OK;
        };

        link->addDatagram(Command::APRD, createAddress(0 - pos, reg), data,
                         callback,
                         [](DatagramState const&){});

        link->processDatagrams();
        return 1;
    }

    static int link_set_timeout(lua_State* L)
    {
        auto link = *static_cast<std::shared_ptr<Link>*>(luaL_checkudata(L, 1, "kickcat::Link"));
        microseconds timeout{luaL_checkinteger(L, 2)};

        link->setTimeout(timeout);
        return 0;
    }

    static int link_finalize_datagrams(lua_State* L)
    {
        auto link = *static_cast<std::shared_ptr<Link>*>(luaL_checkudata(L, 1, "kickcat::Link"));
        link->finalizeDatagrams();
        return 0;
    }

        static int link_process_datagrams(lua_State* L)
    {
        auto link = *static_cast<std::shared_ptr<Link>*>(luaL_checkudata(L, 1, "kickcat::Link"));
        link->processDatagrams();
        return 0;
    }

    static int link_attach_ecat_event(lua_State* L)
    {
        auto link = *static_cast<std::shared_ptr<Link>*>(luaL_checkudata(L, 1, "kickcat::Link"));
        int idx = luaL_checkoption(L, 2, nullptr, ECAT_EVENT);

        // make callback
        luaL_checktype(L, 3, LUA_TFUNCTION);
        lua_pushvalue(L, 3);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        auto callback = [ref, L]()
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_pcall(L, 0, 0, 0);
        };

        link->attachEcatEventCallback(EcatEvent(1 << idx), callback);
        return 0;
    }

    static int link_constructor(lua_State* L)
    {
        auto socketNor = *static_cast<std::shared_ptr<AbstractSocket>*>(luaL_checkudata(L, 1, "kickcat::socket"));
        auto socketRed = *static_cast<std::shared_ptr<AbstractSocket>*>(luaL_checkudata(L, 2, "kickcat::socket"));

        createClassForLua<std::shared_ptr<Link>>(L, "kickcat::Link", socketNor, socketRed, [](){});
        return 1;
    }

    void luaload_link(lua_State* L)
    {
        registerClass<std::shared_ptr<AbstractSocket>>(L, "kickcat::Link",
        {
            { "read_reg32",           link_read_reg32         },
            { "finalize_datagrams",   link_finalize_datagrams },
            { "process_datagrams",    link_process_datagrams  },
            { "attach_ecat_event",    link_attach_ecat_event  },
            { "set_timeout",          link_set_timeout        },
        });

        lua_pushcfunction(L, link_constructor);
        lua_setfield(L, -2, "link");
    }
}

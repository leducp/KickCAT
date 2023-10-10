#ifndef KICKCAT_LUA_HELPERS_H
#define KICKCAT_LUA_HELPERS_H

#include <vector>
#include <string>
#include <tuple>
#include <memory>

extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

namespace kickcat
{
    template<typename T>
    struct is_shared_ptr : public std::false_type {};
    template<typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : public std::true_type {};

    template<typename T>
    void registerClass(lua_State* L, char const* name, std::vector<std::tuple<std::string, lua_CFunction>> const& functions)
    {
        int top = lua_gettop(L);

        auto destructor = [](lua_State* sL) -> int
        {
            if constexpr (is_shared_ptr<T>{})
            {
                auto socket = *static_cast<T*>(lua_touserdata(sL, 1));
                socket.reset();
            }
            else
            {
                delete *static_cast<T**>(lua_touserdata(sL, 1));
            }
            return 0;
        };

        // Create the metatable and associate the destructor to the garbage collector callback
        luaL_newmetatable(L, name);
        lua_pushcfunction(L, destructor); lua_setfield(L, -2, "__gc");

        for (auto& [fname, fcall] : functions)
        {
            lua_pushcfunction(L, fcall);
            lua_setfield(L, -2, fname.c_str());
        }

        // set __index on ourself
        lua_pushvalue(L, -1);
        lua_setfield(L, -1, "__index");

        lua_settop(L, top);
    }

    template<typename T, typename ...Args>
    void createClassForLua(lua_State* L, char const* name, Args&& ...args)
    {
        if constexpr (is_shared_ptr<T>{})
        {
            // Allocate memory and record item in Lua - shared pointer
            void* item = lua_newuserdatauv(L, sizeof(T), 0);

            // Placement new
            auto shrItem = std::make_shared<typename T::element_type>(std::forward<Args>(args)...);
            new(item) T(shrItem);
        }
        else
        {
            // Allocate memory and record item in Lua - raw pointer
            T** item = static_cast<T**>(lua_newuserdatauv(L, sizeof(T*), 0));
            *item = new T{std::forward<Args>(args)...};
        }

        // Apply its metatable on it
        luaL_setmetatable(L, name);
    }

    void dumpstack(lua_State *L);
}

#endif

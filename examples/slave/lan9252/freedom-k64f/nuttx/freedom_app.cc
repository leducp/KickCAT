#include "freedom_app.h"

#include <cstdio>
#include <exception>

using namespace kickcat;

namespace freedom
{
    namespace
    {
        char const* state_name(State state)
        {
            if (state == State::INIT)
            {
                return "INIT";
            }
            if (state == State::PRE_OP)
            {
                return "PRE_OP";
            }
            if (state == State::BOOT)
            {
                return "BOOT";
            }
            if (state == State::SAFE_OP)
            {
                return "SAFE_OP";
            }
            if (state == State::OPERATIONAL)
            {
                return "OPERATIONAL";
            }
            return "INVALID";
        }

        // Combined Hooks.ctx (Hooks has one ctx for all callbacks): data source + trace state.
        struct AppCtx
        {
            DataSource data_source;
            void* data_ctx;
            State last;
        };

        void app_log(void*, char const* line)
        {
            printf("%s", line);
        }

        void app_on_operational(void* vctx, Pdo const& io)
        {
            AppCtx* app = static_cast<AppCtx*>(vctx);
            app->data_source(app->data_ctx, io);
        }

        void app_on_cycle(void* vctx, State state)
        {
            AppCtx* app = static_cast<AppCtx*>(vctx);
            if (state != app->last)
            {
                printf("[slave] state -> %s\n", state_name(state));
                app->last = state;
            }
        }
    }

    int app_run(std::shared_ptr<AbstractSPI> spi, DataSource data_source, void* data_ctx)
    {
        // newlib block-buffers stdout by default, so unbuffer it (and stderr, which carries
        // KickCAT's bring-up traces) to reach the console live; NuttX is unaffected.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);

        AppCtx app;
        app.data_source = data_source;
        app.data_ctx = data_ctx;
        app.last = State::INVALID;

        Hooks hooks;
        hooks.ctx = &app;
        hooks.log = app_log;
        hooks.on_operational = app_on_operational;
        hooks.on_cycle = app_on_cycle;

        // KickCAT throws (THROW_SYSTEM_ERROR_CODE); an escaping exception would terminate
        // the image, so guard the whole run.
        try
        {
            run(spi, hooks);
        }
        catch (std::exception const& e)
        {
            printf("[slave] exception: %s\n", e.what());
            return 1;
        }
        return 0;
    }
}

#include "freedom_slave.h"

#include "kickcat/CoE/OD.h"
#include "kickcat/ESC/Lan9252.h"
#include "kickcat/Mailbox.h"
#include "kickcat/PDO.h"
#include "kickcat/slave/Slave.h"

#include <cstdio>
#include <cstring>

using namespace kickcat;

namespace freedom
{
    int run(std::shared_ptr<AbstractSPI> spi, Hooks const& hooks)
    {
        char line[96];

        Lan9252 esc = Lan9252(spi);
        int32_t rc = esc.init();
        if (rc < 0)
        {
            snprintf(line, sizeof(line), "error init %ld - %s\n",
                     static_cast<long>(rc), strerror(static_cast<int>(-rc)));
            hooks.log(hooks.ctx, line);
        }
        PDO pdo(&esc);
        slave::Slave slave(&esc, &pdo);

        // The master can request less inputs/ouputs and these buffers are the space
        // that the slave app allocated to let the master play with the mapping.
        constexpr uint32_t PDO_MAX_SIZE = 16;

        uint8_t buffer_in[PDO_MAX_SIZE];
        uint8_t buffer_out[PDO_MAX_SIZE];

        // init values
        for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
        {
            buffer_in[i] = i;
            buffer_out[i] = 0xFF;
        }

        mailbox::response::Mailbox mbx(&esc, 1024);
        auto dictionary = CoE::createOD();
        mbx.enableCoE(dictionary);          // the firmware owns the OD; the mailbox references it

        slave.setMailbox(&mbx);
        slave.setDictionary(&dictionary);   // and the slave uses it for bind / PDO mapping
        pdo.setInput(buffer_in, PDO_MAX_SIZE);
        pdo.setOutput(buffer_out, PDO_MAX_SIZE);

        uint8_t esc_config;
        esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));

        bool is_emulated = esc_config & PDI_EMULATION;
        snprintf(line, sizeof(line), "esc config 0x%x, is emulated %i \n", esc_config, is_emulated);
        hooks.log(hooks.ctx, line);

        uint8_t pdi_config;
        esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
        snprintf(line, sizeof(line), "pdi config 0x%x \n", pdi_config);
        hooks.log(hooks.ctx, line);

        slave.start();

        Pdo io{};
        io.ax = nullptr;
        io.ay = nullptr;
        io.az = nullptr;
        io.mx = nullptr;
        io.my = nullptr;
        io.mz = nullptr;
        io.led_r = nullptr;
        io.led_g = nullptr;
        io.led_b = nullptr;

        // Latch the bind + validateOutputData to ONCE per master session. Re-calling
        // validateOutputData() every SAFE_OP tick re-arms is_valid_output_data, so after a
        // watchdog drop OP<->SAFE_OP the slave keeps re-entering OP against a vanished
        // master -- an endless flap. A fresh master session always walks back through
        // INIT/PRE_OP, which clears the latch so the PDO is re-bound + re-validated.
        bool bound = false;

        while (true)
        {
            slave.routine();

            const State state = slave.state();

            if (state == State::INIT or state == State::PRE_OP)
            {
                bound = false;
            }

            if (state == State::SAFE_OP and not bound)
            {
                slave.bind(0x6000, io.ax);
                slave.bind(0x6001, io.ay);
                slave.bind(0x6002, io.az);
                slave.bind(0x6003, io.mx);
                slave.bind(0x6004, io.my);
                slave.bind(0x6005, io.mz);
                slave.bind(0x7000, io.led_r);
                slave.bind(0x7001, io.led_g);
                slave.bind(0x7002, io.led_b);

                if (buffer_out[1] != 0xFF)
                {
                    slave.validateOutputData();
                    bound = true;
                }
            }
            else if (state == State::OPERATIONAL and bound)
            {
                hooks.on_operational(hooks.ctx, io);
            }

            if (hooks.on_cycle != nullptr)
            {
                hooks.on_cycle(hooks.ctx, state);
            }
        }

        return 0;
    }
}

#include "kickcat/CoE/OD.h"
#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/EEPROM/XMC4800EEPROM.h"
#include "kickcat/ESC/XMC4800.h"
#include "kickcat/Mailbox.h"
#include "kickcat/OS/Time.h"
#include "kickcat/PDO.h"
#include "kickcat/slave/Slave.h"

using namespace kickcat;

int main(int, char*[])
{
    printf("XMC hello relax\n");
    
    XMC4800 esc;
    XMC4800EEPROM eeprom;
    PDO pdo(&esc);
    slave::Slave slave(&esc, &pdo);
    
    eeprom.init();
    
    uint16_t al_status;
    uint16_t al_control;
    esc.read(reg::AL_STATUS, &al_status, sizeof(al_status));
    esc.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
    printf("al_status %x al_control %x  \n", al_status, al_control);
    
    // The master can request less inputs/ouputs and these buffers are the space
    // that the slave app allocated to let the master play with the mapping.
    constexpr uint32_t PDO_MAX_SIZE = 32;
    uint8_t buffer_in[PDO_MAX_SIZE];
    uint8_t buffer_out[PDO_MAX_SIZE];
    
    // Init values
    for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
    {
        buffer_in[i] = i;
        buffer_out[i] = 0xFF;
    }

    mailbox::response::Mailbox mbx(&esc, 1024);
    auto dictionary = CoE::createOD();
    mbx.enableCoE(std::move(dictionary));

    slave.setMailbox(&mbx);
    pdo.setInput(buffer_in, PDO_MAX_SIZE);
    pdo.setOutput(buffer_out, PDO_MAX_SIZE);
    
    uint8_t esc_config;
    esc.read(reg::ESC_CONFIG, &esc_config, sizeof(esc_config));
    bool is_emulated = esc_config & PDI_EMULATION;
    printf("esc config 0x%x, is emulated %i \n", esc_config, is_emulated);
    
    uint8_t pdi_config;
    esc.read(reg::PDI_CONFIGURATION, &pdi_config, sizeof(pdi_config));
    printf("pdi config 0x%x \n", pdi_config);
    
    slave.start();
    
    // Variables for toggling pattern
    uint32_t iteration_counter = 0;
    uint8_t current_value = 0x11;  // Start with 0x11

    constexpr uint32_t ITER = 10000; // Number of iterations before updating input buffer
    
    while (true)
    {
        eeprom.process();
        slave.routine();
        
        if (slave.state() == State::SAFE_OP)
        {
            if (buffer_out[1] != 0xFF)
            {
                slave.validateOutputData();
            }
        }
        
        // Update input buffer every ITER iterations
        iteration_counter++;
        if (iteration_counter >= ITER)
        {
            iteration_counter = 0;
            
            // Fill buffer with current value
            for (uint32_t i = 0; i < PDO_MAX_SIZE; ++i)
            {
                buffer_in[i] = current_value;
            }
            
            // Move to next value: 0x11 -> 0x22 -> 0x33 -> ... -> 0xFF -> 0x00 -> 0x11
            if (current_value == 0xFF)
            {
                current_value = 0x00;
            }
            else
            {
                current_value += 0x11;
            }            
        }
    }
    
    return 0;
}

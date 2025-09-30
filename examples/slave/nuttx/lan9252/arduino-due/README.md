This example is based on the following setup:
* an Arduino-Due
* an easycat shield
* NuttX RTOS (more information in the board README)

The slave is configured as simple: no mailbox is enabled and the PDOs are configured through the EEPROM only.
However, compared to the original easycat EEPROm file, the emulation is disabled: the slave stack is therefore
required to acknowledge the various steps (i.e. EtherCAT state machine).

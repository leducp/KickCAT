This example is based on the following setup:
* an NXP Kinetis Freedom-K64F evaluation baord
* an easycat shield
* NuttX RTOS (more information in the board README)

The slave is configured as a complexe one: mailbox is enable with a CAN Open dictionary.
The PDOs cannot be configured sicne the slave stakc do not manage this feature (yet).

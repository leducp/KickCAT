A C++ implementation of EtherCAT master stack (and currently so unfinished that you cannot do anything with it)


==== EtherCAT doc ====
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html&id=3196541253205318339
https://www.ethercat.org/download/documents/EtherCAT_Device_Protocol_Poster.pdf

registers:
https://download.beckhoff.com/download/Document/io/ethercat-development-products/ethercat_esc_datasheet_sec2_registers_3i0.pdf

eeprom :
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1358008331.html&id=5054579963582410224


==== TODO ====
Handle more than 15 slaves and more than 1500 bytes -> datagram packing need to be reworked:
    - prepare a set of frame depending on number of datagfram and size of datagram
    - write then read each frame
    - extract data for each frame

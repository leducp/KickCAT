A C++ implementation of EtherCAT master stack.

Current state:
 - Can go to OP state
 - Can read and write PI (no read/write though)
 - Can read SDO  - blocking call
 - Can write SDO - blocking call

TODO:
 - CoE: read and write SDO - async call
 - CoE: segmented transfer
 - read/write PI
 - rework error handling
 - CoE: Emergency message
 - bus diagnostic
 - better mailbox handling - timeout, answer queue (to avoid drop of unexpected messages that may be useful like CoE emergency)
 - CoE: diagnosis message - 0x10F3
 - FoE
 - Distributed clock


### EtherCAT doc
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1257993099.html&id=3196541253205318339
https://www.ethercat.org/download/documents/EtherCAT_Device_Protocol_Poster.pdf

registers:
https://download.beckhoff.com/download/Document/io/ethercat-development-products/ethercat_esc_datasheet_sec2_registers_3i0.pdf

eeprom :
https://infosys.beckhoff.com/english.php?content=../content/1033/tc3_io_intro/1358008331.html&id=5054579963582410224

various:
https://sir.upc.edu/wikis/roblab/index.php/Development/Ethercat

diag:
https://www.automation.com/en-us/articles/2014-2/diagnostics-with-ethercat-part-4
https://infosys.beckhoff.com/english.php?content=../content/1033/ethercatsystem/1072509067.html&id=
https://knowledge.ni.com/KnowledgeArticleDetails?id=kA00Z000000kHwESAU

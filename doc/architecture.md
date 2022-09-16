# Global Architecture 






This document presents the goal of the main classes of the KickCAT project. It serves as a first level guidelines when adding new features to the project.







#### Frame:

**Goal:** Representation of an EtherCAT frame on the network.

**Actions:** 

- Add / Get an EtherCAT datagram to/from the frame.
- encapsulate the network accesses (read/write frame) without owning the access. 



#### Link:

**Goal:** Interface between the client and the network to only expose logical data (the EtherCAT datagrams)

**Actions:** 

- Own the socket(s).
- Encapsulate the access type (redundancy or not).
- Handle the on-fly frames:
  - Send multiple frames in a row without waiting for each reading.
  - Associate the read datagrams to the callbacks (process and error).
  - Handle the loss of packets (corrupted or lost). 
- Call the callbacks associated to the datagrams.



#### Mailbox:

**Goal:** Handle the messages.

**Actions:** 

- Handle the message factory (one for each supported protocol).
- Handle the sending queue.
- Handle the parsing of the reception.



#### Slave:

**Goal:** Represents a slave on the network (ie: address of the slave on the bus, PI memory address, etc).

**Actions:**

- Handle the parsing of the SII data (does not handle the access though !).
- Own a mailbox (it is not necessarily active, it depends on the slave).



#### Bus:

**Goal:** Configure the physical bus and provide abstraction for the applicative accesses (PI, mailbox...).

**Actions:**

- Handle the EtherCAT state machine (init, pre-op, safe-op, op).
- Organize the PI access.
- Own the slaves and feed them with data (status, error counters, eeprom contents, etc).
- Own the link to access to the network.
- Sequence the slaves mailboxes.

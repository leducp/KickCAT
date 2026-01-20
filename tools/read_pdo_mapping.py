import kickcat
import argparse
import struct
from kickcat import State


def read_pdo_assignment(bus, slave, assignment_index, pdo_type_name, timeout=0.1):
    """
    Read and display PDO assignment and mapping for a slave.

    Args:
        bus: The EtherCAT bus object
        slave: The slave to read from
        assignment_index: The PDO assignment index (0x1C12 for RxPDO, 0x1C13 for TxPDO)
        pdo_type_name: Name for display ("RxPDO" or "TxPDO")
        timeout: SDO read timeout in seconds

    Returns:
        List of dicts containing PDO mapping information
    """
    print(f"\n=== Reading {pdo_type_name} Configuration (0x{assignment_index:04X}) ===")

    pdo_mappings = []

    try:
        # Read number of assigned PDOs
        data = bus.read_sdo(
            slave, assignment_index, 0, kickcat.Access.PARTIAL, 1, timeout
        )
        num_pdos = struct.unpack("B", data)[0]
        print(f"Number of {pdo_type_name}s: {num_pdos}")

        byte_offset = 0

        # Iterate through each assigned PDO
        for i in range(1, num_pdos + 1):
            # Read PDO index
            data = bus.read_sdo(
                slave, assignment_index, i, kickcat.Access.PARTIAL, 2, timeout
            )
            pdo_index = struct.unpack("<H", data)[0]
            print(f"  {pdo_type_name} {i}: 0x{pdo_index:04X}")

            # Read number of mapped objects in this PDO
            data = bus.read_sdo(slave, pdo_index, 0, kickcat.Access.PARTIAL, 1, timeout)
            num_objects = struct.unpack("B", data)[0]
            print(f"    Number of mapped objects: {num_objects}")

            # Read each mapped object
            for j in range(1, num_objects + 1):
                # Read 32-bit mapping entry
                data = bus.read_sdo(
                    slave, pdo_index, j, kickcat.Access.PARTIAL, 4, timeout
                )
                mapping_entry = struct.unpack("<I", data)[0]

                # Decode mapping entry: [Index:16][SubIndex:8][BitLength:8]
                obj_index = (mapping_entry >> 16) & 0xFFFF
                obj_subindex = (mapping_entry >> 8) & 0xFF
                bit_length = mapping_entry & 0xFF

                try:
                    name, _ = bus.read_entry_description(slave, obj_index, obj_subindex)
                    if not name:
                        # Fallback to object name if the entry name is not populated
                        name, _ = bus.read_object_description(slave, obj_index)
                    if not name:
                        # Fallback to default value to emphasize that no name was found at all
                        name = "empty name"
                except Exception as e:
                    print(e)
                    name = "empty name"

                print(
                    f"      [{j}] : 0x{obj_index:04X}:{obj_subindex} {name}, "
                    f"Bits: {bit_length}, Offset: {byte_offset}"
                )

                # Store mapping info
                pdo_mappings.append(
                    {
                        "pdo_index": pdo_index,
                        "pdo_number": i,
                        "object_number": j,
                        "index": obj_index,
                        "subindex": obj_subindex,
                        "bit_length": bit_length,
                        "byte_offset": byte_offset,
                        "byte_length": (bit_length + 7) // 8,  # Round up to bytes
                    }
                )

                # Update offset
                byte_offset += bit_length

        # Convert total bits to bytes
        total_bytes = (byte_offset + 7) // 8
        print(f"\nTotal {pdo_type_name} size: {byte_offset} bits ({total_bytes} bytes)")

    except Exception as e:
        print(f"Error reading {pdo_type_name}: {e}")
        raise

    return pdo_mappings


# Example usage:
def read_all_pdo_mappings(bus, slave):
    """Read both RxPDO and TxPDO mappings for a slave."""

    # Read RxPDO (outputs from master to slave)
    rx_mappings = read_pdo_assignment(bus, slave, 0x1C12, "RxPDO")

    # Read TxPDO (inputs from slave to master)
    tx_mappings = read_pdo_assignment(bus, slave, 0x1C13, "TxPDO")

    return {"rx": rx_mappings, "tx": tx_mappings}


# Usage in your main code:
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="EtherCAT master for Freedom K64F using EasyCAT"
    )

    parser.add_argument(
        "-i",
        "--interface",
        help="Primary network interface (e.g., eth0)",
        required=True,
    )
    parser.add_argument(
        "-r",
        "--redundancy",
        help="Redundancy network interface (e.g., eth1)",
        default="",
    )

    args = parser.parse_args()

    nom_interface = args.interface
    red_interface = args.redundancy

    # Create link
    link = kickcat.create_link(nom_interface, red_interface)

    # Create bus
    bus = kickcat.Bus(link)

    print("Initializing Bus...")
    bus.init(0.1)  # 100ms
    print("Bus Init done")

    bus.create_mapping()

    # Print slave info
    for slave in bus.slaves():
        print(
            f"Slave {slave.address}: input={slave.input_size} output={slave.output_size}"
        )

    slave = bus.slaves()[0]

    print("Going to SAFE_OP")
    bus.request_state(State.SAFE_OP)
    bus.wait_for_state(State.SAFE_OP, 1.0)
    print("SAFE_OP reached")

    # Read all PDO mappings
    mappings = read_all_pdo_mappings(bus, slave)

    # Access the mapping information
    print("\n=== Summary ===")
    print(f"RxPDO objects: {len(mappings['rx'])}")
    print(f"TxPDO objects: {len(mappings['tx'])}")

    # Example: Access specific mapping details
    for mapping in mappings["rx"]:
        print(
            f"RxPDO 0x{mapping['index']:04X}:{mapping['subindex']} "
            f"at byte offset {mapping['byte_offset']}"
        )

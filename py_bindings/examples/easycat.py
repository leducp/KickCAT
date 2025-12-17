import kickcat
from kickcat import State
from kickcat.mailbox.request import MessageStatus
import time
import argparse


def main():
    parser = argparse.ArgumentParser(description="EtherCAT master using EasyCAT")
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
    bus.init(0.1)
    print(f"Detected slaves: {len(bus.slaves())}")
    for s in bus.slaves():
        print(f" - Slave {s.address}")

    bus.create_mapping()

    easycat = bus.slaves()[0]

    def cyclic_process_data():
        bus.process_data_noop()

    print("Switching to SAFE_OP...")
    bus.request_state(State.SAFE_OP)
    bus.wait_for_state(State.SAFE_OP, 1.0)

    easycat.set_output_bytes(b"\xbb" * easycat.output_size)

    cyclic_process_data()

    print("Switching to OPERATIONAL...")

    bus.request_state(State.OPERATIONAL)
    bus.wait_for_state(State.OPERATIONAL, 1.0, cyclic_process_data)

    print("After OPERATIONAL - Slave info:")
    for s in bus.slaves():
        print(f" - Slave {s.address} input: {s.input_size} output: {s.output_size}")

    # Read serial via mailbox SDO
    msg = easycat.mailbox.read_sdo(0x1018, 4, False)
    while msg.status() == MessageStatus.RUNNING:
        bus.process_mailboxes()

    if msg.status() == MessageStatus.SUCCESS:
        serial = int.from_bytes(msg.data[: msg.size], "little")
        print(f"Serial: {hex(serial)}")
    else:
        print(f"Error reading serial: {msg.status()}")

    # print("Running loop...")
    # while True:
    #     try:
    #         bus.process_data()
    #         print(f"input: {easycat.input_data.hex()}", end="\r", flush=True)
    #         time.sleep(0.004)
    #     except Exception as e:
    #         print(f"Error in loop iteration: {e}")


if __name__ == "__main__":
    main()

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

    slaves = bus.slaves()
    print(f"Detected slaves: {len(slaves)}")
    for s in slaves:
        print(f" - Slave {s.address}")

    bus.create_mapping()

    def cyclic_process_data():
        bus.process_data_no_check()

    print("Switching to SAFE_OP...")
    bus.request_state(State.SAFE_OP)
    bus.wait_for_state(State.SAFE_OP, 1.0)

    # Set output for all slaves
    for slave in slaves:
        if slave.output_size > 0:
            slave.set_output_bytes(b"\xbb" * slave.output_size)

    cyclic_process_data()

    print("Switching to OPERATIONAL...")

    bus.request_state(State.OPERATIONAL)
    bus.wait_for_state(State.OPERATIONAL, 1.0, cyclic_process_data)

    print("After OPERATIONAL - Slave info:")
    for s in slaves:
        print(f" - Slave {s.address} input: {s.input_size} output: {s.output_size}")

    # Read serial via mailbox SDO for first slave
    if len(slaves) > 0:
        msg = slaves[0].mailbox.read_sdo(0x1018, 4, False)
        while msg.status() == MessageStatus.RUNNING:
            bus.process_mailboxes()

        if msg.status() == MessageStatus.SUCCESS:
            serial = int.from_bytes(msg.data[: msg.size], "little")
            print(f"Serial: {hex(serial)}")
        else:
            print(f"Error reading serial: {msg.status()}")

    print("Running loop...")
    while True:
        try:
            bus.process_data()

            print("\033[K", end="")  # Clear line
            for idx, slave in enumerate(slaves):
                print(f"input_slave_{idx}: {slave.input_data.hex()}")
            print(f"\033[{len(slaves)}A", end="", flush=True)  # Move cursor up

            time.sleep(0.004)
        except Exception as e:
            print(f"\nError in loop iteration: {e}\n")


if __name__ == "__main__":
    main()

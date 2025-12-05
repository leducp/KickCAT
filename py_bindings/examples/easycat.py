import kickcat
from kickcat import State
from kickcat.mailbox.request import MessageStatus
import time

link = kickcat.create_link("eth0", "")

bus = kickcat.Bus(link)

print("Initializing...")
bus.init(0.1)

print(f"Detected slaves: {len(bus.slaves())}")
for s in bus.slaves():
    print(f" - Slave {s.address}")

bus.create_mapping()

easycat = bus.slaves()[0]

print("Switching to SAFE_OP...")
bus.request_state(State.SAFE_OP)
bus.wait_for_state(State.SAFE_OP, 1.0)

easycat.set_output_bytes(b"\xBB" * easycat.output_size)

print("Switching to OPERATIONAL...")
bus.request_state(State.OPERATIONAL)

def cyclic_callback():
    bus.process_data()

bus.wait_for_state(State.OPERATIONAL, 1.0, cyclic_callback)

print("After OPERATIONAL - Slave info:")
for s in bus.slaves():
    print(f" - Slave {s.address} input: {s.input_size} output: {s.output_size}")

msg = easycat.mailbox.read_sdo(0x1018, 4, False)
while msg.status() == MessageStatus.RUNNING:
    bus.process_mailboxes()
if msg.status() == MessageStatus.SUCCESS:
    serial = int.from_bytes(msg.data[:msg.size], 'little')
    print(f"serial: {hex(serial)}")
else:
    print(f"Error: {msg.status()}")

print("Running loop...")
while True:
    try:
        bus.process_data()

        print(f"input: {easycat.input_data.hex()}", end='\r', flush=True)

        time.sleep(0.004)
    except Exception as e:
        print(f"Error in loop iteration {i}: {e}")

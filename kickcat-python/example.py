import kickcat
from kickcat import State
import time

link = kickcat.create_link("enp8s0", "")

bus = kickcat.Bus(link)

print("Initializing...")
bus.init(0.1)

print("Slaves:")
for s in bus.slaves():
    print(f" - Slave {s.address} input: {s.input_size} output: {s.output_size}")

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

easycat = bus.slaves()[0]

print("Running loop...")
while True:
    try:
        bus.process_data()
        bus.process_frames()

        print(f"input: {easycat.input_data.hex()}", end='\r', flush=True)
        
        time.sleep(0.004)
    except Exception as e:
        print(f"Error in loop iteration {i}: {e}")
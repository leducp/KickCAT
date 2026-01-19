import struct
from PySide6.QtCore import QObject, Signal, QTimer
import kickcat

def read_pdo_assignment(bus, slave, assignment_index, timeout=0.1):
    """Read and return PDO assignment and mapping for a slave."""
    pdo_mappings = []

    try:
        # Read number of assigned PDOs
        data = bus.read_sdo(
            slave, assignment_index, 0, kickcat.Access.PARTIAL, 1, timeout
        )
        num_pdos = struct.unpack("B", data)[0]

        byte_offset = 0

        # Iterate through each assigned PDO
        for i in range(1, num_pdos + 1):
            # Read PDO index
            data = bus.read_sdo(
                slave, assignment_index, i, kickcat.Access.PARTIAL, 2, timeout
            )
            pdo_index = struct.unpack("<H", data)[0]

            # Read number of mapped objects in this PDO
            data = bus.read_sdo(slave, pdo_index, 0, kickcat.Access.PARTIAL, 1, timeout)
            num_objects = struct.unpack("B", data)[0]

            # Read each mapped object
            for j in range(1, num_objects + 1):
                # Read 32-bit mapping entry
                data = bus.read_sdo(
                    slave, pdo_index, j, kickcat.Access.PARTIAL, 4, timeout
                )
                mapping_entry = struct.unpack("<I", data)[0]

                # Decode mapping entry
                obj_index = (mapping_entry >> 16) & 0xFFFF
                obj_subindex = (mapping_entry >> 8) & 0xFF
                bit_length = mapping_entry & 0xFF

                pdo_mappings.append(
                    {
                        "pdo_index": pdo_index,
                        "index": obj_index,
                        "subindex": obj_subindex,
                        "bit_length": bit_length,
                        "byte_offset": byte_offset,
                    }
                )

                byte_offset += bit_length

    except Exception as e:
        print(f"Error reading PDO: {e}")

    return pdo_mappings


class EtherCATBackend(QObject):
    """
    Manages EtherCAT bus interactions, state changes, and cyclic data processing.
    """
    data_received = Signal(str)  # Emits hex string of input data
    state_changed = Signal(str)  # Emits new state name
    error_occurred = Signal(str)

    STATE_MAP_STR_TO_ENUM = {
        "INIT": kickcat.State.INIT,
        "PRE-OP": kickcat.State.PREOP,
        "SAFE-OP": kickcat.State.SAFE_OP,
        "OP": kickcat.State.OPERATIONAL,
    }
    
    STATE_MAP_ENUM_TO_STR = {v: k for k, v in STATE_MAP_STR_TO_ENUM.items()}

    def __init__(self, bus, slaves, parent=None):
        super().__init__(parent)
        self.bus = bus
        self.slaves = slaves
        self.current_slave = slaves[0] if slaves else None
        
        self.cyclic_timer = QTimer(self)
        self.cyclic_timer.timeout.connect(self._process_cyclic_data)
        self.is_operational = False

    def set_current_slave(self, index):
        if 0 <= index < len(self.slaves):
            self.current_slave = self.slaves[index]

    def get_current_state_str(self):
        if not self.current_slave:
            return "UNKNOWN"
        try:
            state = self.bus.get_state(self.current_slave)
            return self.STATE_MAP_ENUM_TO_STR.get(state, "UNKNOWN")
        except Exception as e:
            self.error_occurred.emit(f"Error getting state: {e}")
            return "UNKNOWN"

    def request_state(self, state_str):
        if state_str not in self.STATE_MAP_STR_TO_ENUM:
            return

        target_state = self.STATE_MAP_STR_TO_ENUM[state_str]

        try:
            # Stop cyclic processing if transitioning away from OP
            if state_str != "OP" and self.is_operational:
                self.stop_cyclic_processing()

            print(f"Transitioning to state: {state_str}")
            self.bus.request_state(target_state)

            if state_str == "OP":
                # Wait for OP state with callback
                self.bus.wait_for_state(target_state, 1.0, self._cyclic_callback)
                self.start_cyclic_processing()
            else:
                self.bus.wait_for_state(target_state, 1.0)
            
            self.state_changed.emit(state_str)

        except Exception as e:
            self.error_occurred.emit(f"Error requesting state {state_str}: {e}")
            self.stop_cyclic_processing()

    def _cyclic_callback(self):
        """Callback used during wait_for_state to keep bus alive."""
        self.bus.process_data()

    def _process_cyclic_data(self):
        """Called periodically by QTimer when in OP mode."""
        try:
            self.bus.process_data()
            if self.current_slave:
                input_hex = self.current_slave.input_data.hex()
                self.data_received.emit(input_hex)
        except Exception as e:
            self.error_occurred.emit(f"Error processing data: {e}")
            self.stop_cyclic_processing()

    def start_cyclic_processing(self, interval_ms=40):
        if not self.cyclic_timer.isActive():
            self.is_operational = True
            self.cyclic_timer.start(interval_ms)
            print("Started cyclic data processing...")

    def stop_cyclic_processing(self):
        if self.cyclic_timer.isActive():
            self.cyclic_timer.stop()
            self.is_operational = False
            print("Stopped cyclic data processing")

    def read_pdos(self, pdo_type):
        """
        Read PDO mappings for the current slave.
        pdo_type: 0x1C12 (Rx) or 0x1C13 (Tx)
        """
        if not self.current_slave:
            return []
        return read_pdo_assignment(self.bus, self.current_slave, pdo_type)

import sys
import struct
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLabel,
    QComboBox,
    QGroupBox,
    QScrollArea,
    QFrame,
)
from PySide6.QtCore import Qt, QTimer, Signal

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


class StateButton(QPushButton):
    """Custom styled button for EtherCAT states."""

    STATE_COLORS = {
        "INIT": "#6B7280",
        "PRE-OP": "#EAB308",
        "SAFE-OP": "#F97316",
        "OP": "#22C55E",
    }

    def __init__(self, state_name, parent=None):
        super().__init__(state_name, parent)
        self.state_name = state_name
        self.setFixedSize(100, 100)
        self.setCheckable(True)
        self.update_style(False)

    def update_style(self, is_active):
        color = self.STATE_COLORS[self.state_name]
        if is_active:
            self.setStyleSheet(
                f"""
                QPushButton {{
                    background-color: {color};
                    color: white;
                    border: 3px solid white;
                    border-radius: 50px;
                    font-size: 12px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background-color: {color};
                    border: 3px solid #60A5FA;
                }}
            """
            )
        else:
            self.setStyleSheet(
                f"""
                QPushButton {{
                    background-color: #374151;
                    color: #9CA3AF;
                    border: 2px solid #4B5563;
                    border-radius: 50px;
                    font-size: 12px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background-color: #4B5563;
                    color: white;
                }}
            """
            )


class PDOItemWidget(QFrame):
    """Widget to display a single PDO mapping entry."""

    def __init__(self, mapping, parent=None):
        super().__init__(parent)
        self.setFrameStyle(QFrame.StyledPanel)
        self.setStyleSheet(
            """
            QFrame {
                background-color: #1E293B;
                border: 1px solid #475569;
                border-radius: 8px;
                padding: 8px;
            }
        """
        )

        layout = QVBoxLayout()
        layout.setSpacing(4)

        # Top row: Index and PDO
        top_layout = QHBoxLayout()
        index_label = QLabel(f"0x{mapping['index']:04X}:{mapping['subindex']}")
        index_label.setStyleSheet(
            "color: #22D3EE; font-family: monospace; font-weight: bold;"
        )
        pdo_label = QLabel(f"PDO: 0x{mapping['pdo_index']:04X}")
        pdo_label.setStyleSheet("color: #94A3B8; font-size: 11px;")
        top_layout.addWidget(index_label)
        top_layout.addStretch()
        top_layout.addWidget(pdo_label)

        # Bottom row: Bits and Offset
        bottom_layout = QHBoxLayout()
        bits_label = QLabel(f"{mapping['bit_length']} bits")
        bits_label.setStyleSheet("color: #94A3B8; font-size: 11px;")
        offset_label = QLabel(f"Offset: {mapping['byte_offset']} bytes")
        offset_label.setStyleSheet("color: #94A3B8; font-size: 11px;")
        bottom_layout.addWidget(bits_label)
        bottom_layout.addStretch()
        bottom_layout.addWidget(offset_label)

        layout.addLayout(top_layout)
        layout.addLayout(bottom_layout)
        self.setLayout(layout)


class EtherCATControlGUI(QMainWindow):
    data_received = Signal(str)

    def __init__(self, bus, slaves):
        super().__init__()
        self.bus = bus
        self.slaves = slaves
        self.current_slave = slaves[0] if slaves else None
        self.current_state = "INIT"
        self.states = ["INIT", "PRE-OP", "SAFE-OP", "OP"]
        self.state_buttons = {}

        self.setWindowTitle("EtherCAT State Control")
        self.setMinimumSize(1000, 800)

        # Create main widget
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setSpacing(20)
        main_layout.setContentsMargins(20, 20, 20, 20)

        # Title
        title = QLabel("EtherCAT State Control")
        title.setAlignment(Qt.AlignCenter)
        title.setStyleSheet(
            "color: #60A5FA; font-size: 32px; font-weight: bold; margin-bottom: 10px;"
        )
        main_layout.addWidget(title)

        subtitle = QLabel("Manage your EtherCAT bus states and PDO mappings")
        subtitle.setAlignment(Qt.AlignCenter)
        subtitle.setStyleSheet("color: #94A3B8; font-size: 14px; margin-bottom: 20px;")
        main_layout.addWidget(subtitle)

        # Slave selection
        main_layout.addWidget(self.create_slave_selection())

        # State control section
        main_layout.addWidget(self.create_state_control())

        # PDO mapping section
        main_layout.addWidget(self.create_pdo_section())

        # Data display section
        main_layout.addWidget(self.create_data_display())

        self.cyclic_timer = QTimer()
        self.cyclic_timer.timeout.connect(self.process_cyclic_data)
        self.is_operational = False

        # Connect signal to update display
        self.data_received.connect(self.update_data_display)

    def create_slave_selection(self):
        group = QGroupBox("Slave Device")
        layout = QVBoxLayout()

        self.slave_combo = QComboBox()
        for i, slave in enumerate(self.slaves):
            self.slave_combo.addItem(f"Slave {i + 1}", slave)
        self.slave_combo.currentIndexChanged.connect(self.on_slave_changed)

        layout.addWidget(self.slave_combo)
        group.setLayout(layout)
        return group

    def create_state_control(self):
        group = QGroupBox("State Control")
        layout = QVBoxLayout()
        layout.setSpacing(20)

        # Current state label
        self.state_label = QLabel(f"Current State: {self.current_state}")
        self.state_label.setStyleSheet(
            "color: white; font-size: 18px; font-weight: bold;"
        )
        self.state_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.state_label)

        # State buttons
        buttons_layout = QHBoxLayout()
        buttons_layout.setSpacing(20)

        for state in self.states:
            btn = StateButton(state)
            btn.clicked.connect(lambda checked, s=state: self.request_state(s))
            self.state_buttons[state] = btn
            buttons_layout.addWidget(btn)

        layout.addLayout(buttons_layout)

        # Navigation buttons
        nav_layout = QHBoxLayout()
        nav_layout.setSpacing(10)

        self.prev_btn = QPushButton("← Previous State")
        self.prev_btn.clicked.connect(self.go_to_previous_state)

        self.next_btn = QPushButton("Next State →")
        self.next_btn.clicked.connect(self.go_to_next_state)

        nav_layout.addStretch()
        nav_layout.addWidget(self.prev_btn)
        nav_layout.addWidget(self.next_btn)
        nav_layout.addStretch()

        layout.addLayout(nav_layout)
        group.setLayout(layout)
        return group

    def create_pdo_section(self):
        group = QGroupBox("PDO Mappings")
        layout = QVBoxLayout()

        # Read button
        read_btn = QPushButton("🔄 Read PDO Mappings")
        read_btn.setStyleSheet(
            """
            QPushButton {
                background-color: #06B6D4;
                font-size: 14px;
                padding: 12px;
            }
            QPushButton:hover {
                background-color: #0891B2;
            }
        """
        )
        read_btn.clicked.connect(self.read_pdo_mappings)
        layout.addWidget(read_btn)

        # PDO display area
        pdo_layout = QHBoxLayout()
        pdo_layout.setSpacing(20)

        # RxPDO
        rx_group = QGroupBox("RxPDO (0x1C12) - Master → Slave")
        rx_group.setStyleSheet("QGroupBox { color: #FB923C; }")
        rx_layout = QVBoxLayout()
        self.rx_scroll = QScrollArea()
        self.rx_scroll.setWidgetResizable(True)
        self.rx_scroll.setMinimumHeight(300)
        self.rx_content = QWidget()
        self.rx_content_layout = QVBoxLayout(self.rx_content)
        self.rx_content_layout.setSpacing(8)
        self.rx_scroll.setWidget(self.rx_content)
        rx_layout.addWidget(self.rx_scroll)
        rx_group.setLayout(rx_layout)

        # TxPDO
        tx_group = QGroupBox("TxPDO (0x1C13) - Slave → Master")
        tx_group.setStyleSheet("QGroupBox { color: #4ADE80; }")
        tx_layout = QVBoxLayout()
        self.tx_scroll = QScrollArea()
        self.tx_scroll.setWidgetResizable(True)
        self.tx_scroll.setMinimumHeight(300)
        self.tx_content = QWidget()
        self.tx_content_layout = QVBoxLayout(self.tx_content)
        self.tx_content_layout.setSpacing(8)
        self.tx_scroll.setWidget(self.tx_content)
        tx_layout.addWidget(self.tx_scroll)
        tx_group.setLayout(tx_layout)

        pdo_layout.addWidget(rx_group)
        pdo_layout.addWidget(tx_group)

        layout.addLayout(pdo_layout)
        group.setLayout(layout)
        return group

    def create_data_display(self):
        """Create a compact data display section."""
        group = QGroupBox("Cyclic Data Monitor")
        layout = QVBoxLayout()

        # Data label with monospace font for hex display
        self.data_label = QLabel("No data (waiting for OP state)")
        self.data_label.setStyleSheet(
            """
            QLabel {
                background-color: #1E293B;
                border: 1px solid #475569;
                border-radius: 8px;
                padding: 12px;
                font-family: 'Courier New', monospace;
                font-size: 13px;
                color: #10B981;
            }
        """
        )
        self.data_label.setWordWrap(True)
        layout.addWidget(self.data_label)

        group.setLayout(layout)
        return group

    def update_data_display(self, data_hex):
        """Update the data display label with new hex data."""
        self.data_label.setText(f"Input Data: {data_hex}")

    def update_current_state(self):
        """Poll the bus for current state and update UI."""
        try:
            state = self.bus.get_state(self.current_slave)
            # Map state enum to string
            state_map = {
                kickcat.State.INIT: "INIT",
                kickcat.State.PREOP: "PRE-OP",
                kickcat.State.SAFE_OP: "SAFE-OP",
                kickcat.State.OPERATIONAL: "OP",
            }
            self.current_state = state_map.get(state, "UNKNOWN")
            self.state_label.setText(f"Current State: {self.current_state}")

            # Update button styles
            for state_name, btn in self.state_buttons.items():
                btn.update_style(state_name == self.current_state)

            # Update navigation buttons
            current_index = (
                self.states.index(self.current_state)
                if self.current_state in self.states
                else -1
            )
            self.prev_btn.setEnabled(current_index > 0)
            self.next_btn.setEnabled(current_index < len(self.states) - 1)

        except Exception as e:
            print(f"Error updating state: {e}")

    def process_cyclic_data(self):
        """Called periodically by QTimer when in OP mode."""
        try:
            self.bus.process_data()
            # Display or emit the data
            input_hex = self.current_slave.input_data.hex()
            self.data_received.emit(input_hex)
        except Exception as e:
            print(f"Error processing data: {e}")
            self.stop_cyclic_processing()

    def start_cyclic_processing(self, interval_ms=40):
        """Start periodic data processing."""
        if not self.cyclic_timer.isActive():
            self.is_operational = True
            self.cyclic_timer.start(interval_ms)
            print("Started cyclic data processing...")

    def stop_cyclic_processing(self):
        """Stop periodic data processing."""
        if self.cyclic_timer.isActive():
            self.cyclic_timer.stop()
            self.is_operational = False
            print("Stopped cyclic data processing")

    def request_state(self, state):
        """Request a state change."""
        try:

            def cyclic_callback():
                self.bus.process_data()

            # Map string to State enum
            state_map = {
                "INIT": kickcat.State.INIT,
                "PRE-OP": kickcat.State.PREOP,
                "SAFE-OP": kickcat.State.SAFE_OP,
                "OP": kickcat.State.OPERATIONAL,
            }

            # Stop cyclic processing if transitioning away from OP
            if state != "OP" and self.is_operational:
                self.stop_cyclic_processing()

            print(f"Transitioning to state: {state}")
            self.bus.request_state(state_map[state])

            if state == "OP":
                # Wait for OP state with callback
                self.bus.wait_for_state(state_map[state], 1.0, cyclic_callback)
                # Start timer-based cyclic processing
                self.start_cyclic_processing()
            else:
                self.bus.wait_for_state(state_map[state], 1.0)

            self.update_current_state()

        except Exception as e:
            print(f"Error requesting state {state}: {e}")
            self.stop_cyclic_processing()

    def go_to_previous_state(self):
        current_index = self.states.index(self.current_state)
        if current_index > 0:
            self.request_state(self.states[current_index - 1])

    def go_to_next_state(self):
        current_index = self.states.index(self.current_state)
        if current_index < len(self.states) - 1:
            self.request_state(self.states[current_index + 1])

    def on_slave_changed(self, index):
        self.current_slave = self.slave_combo.itemData(index)

    def read_pdo_mappings(self):
        """Read and display PDO mappings."""
        if not self.current_slave:
            return

        try:
            # Clear existing widgets
            self.clear_layout(self.rx_content_layout)
            self.clear_layout(self.tx_content_layout)

            # Read RxPDO
            rx_mappings = read_pdo_assignment(self.bus, self.current_slave, 0x1C12)
            for mapping in rx_mappings:
                widget = PDOItemWidget(mapping)
                self.rx_content_layout.addWidget(widget)
            self.rx_content_layout.addStretch()

            # Read TxPDO
            tx_mappings = read_pdo_assignment(self.bus, self.current_slave, 0x1C13)
            for mapping in tx_mappings:
                widget = PDOItemWidget(mapping)
                self.tx_content_layout.addWidget(widget)
            self.tx_content_layout.addStretch()

            print(
                f"Read {len(rx_mappings)} RxPDO and {len(tx_mappings)} TxPDO mappings"
            )

        except Exception as e:
            print(f"Error reading PDO mappings: {e}")

    def clear_layout(self, layout):
        """Clear all widgets from a layout."""
        while layout.count():
            item = layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()


def main():
    app = QApplication(sys.argv)

    try:
        link = kickcat.create_link("enp8s0", "")
        bus = kickcat.Bus(link)
        bus.init(0.1)
        bus.create_mapping()
        slaves = bus.slaves()

        window = EtherCATControlGUI(bus, slaves)
        window.show()

        sys.exit(app.exec())
    except Exception as e:
        print(f"Error initializing EtherCAT: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()

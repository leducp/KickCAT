from PySide6.QtWidgets import (
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QLabel,
    QComboBox,
    QGroupBox,
    QScrollArea,
)
from PySide6.QtCore import Qt

from .widgets import StateButton, PDOItemWidget
from .backend import EtherCATBackend

class EtherCATControlGUI(QMainWindow):
    def __init__(self, bus, slaves):
        super().__init__()
        
        # Initialize Backend
        self.backend = EtherCATBackend(bus, slaves)
        
        # Connect signals
        self.backend.data_received.connect(self.update_data_display)
        self.backend.state_changed.connect(self.on_state_changed)
        self.backend.error_occurred.connect(self.on_error)

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

        # Initial State Update
        self.update_ui_state(self.backend.get_current_state_str())

    def create_slave_selection(self):
        group = QGroupBox("Slave Device")
        layout = QVBoxLayout()

        self.slave_combo = QComboBox()
        for i, slave in enumerate(self.backend.slaves):
            self.slave_combo.addItem(f"Slave {i + 1}", i) # Store index as data
        
        self.slave_combo.currentIndexChanged.connect(self.on_slave_changed)

        layout.addWidget(self.slave_combo)
        group.setLayout(layout)
        return group

    def create_state_control(self):
        group = QGroupBox("State Control")
        layout = QVBoxLayout()
        layout.setSpacing(20)

        # Current state label
        self.state_label = QLabel("Current State: UNKNOWN")
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
            btn.clicked.connect(lambda checked, s=state: self.backend.request_state(s))
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
    
    def on_error(self, message):
        print(f"Error: {message}") # Could be a QMessageBox

    def on_state_changed(self, new_state_str):
        self.update_ui_state(new_state_str)

    def update_ui_state(self, current_state):
        """Update UI elements based on current state."""
        self.state_label.setText(f"Current State: {current_state}")

        # Update button styles
        for state_name, btn in self.state_buttons.items():
            btn.update_style(state_name == current_state)

        # Update navigation buttons
        current_index = (
            self.states.index(current_state)
            if current_state in self.states
            else -1
        )
        self.prev_btn.setEnabled(current_index > 0)
        self.next_btn.setEnabled(current_index < len(self.states) - 1)

    def go_to_previous_state(self):
        current_state = self.backend.get_current_state_str()
        if current_state in self.states:
            current_index = self.states.index(current_state)
            if current_index > 0:
                self.backend.request_state(self.states[current_index - 1])

    def go_to_next_state(self):
        current_state = self.backend.get_current_state_str()
        if current_state in self.states:
            current_index = self.states.index(current_state)
            if current_index < len(self.states) - 1:
                self.backend.request_state(self.states[current_index + 1])

    def on_slave_changed(self, index):
        # index is the combo box index, but we stored real index as data
        real_index = self.slave_combo.itemData(index)
        self.backend.set_current_slave(real_index)
        # Refresh state display for new slave
        self.update_ui_state(self.backend.get_current_state_str())

    def read_pdo_mappings(self):
        """Read and display PDO mappings."""
        try:
            # Clear existing widgets
            self.clear_layout(self.rx_content_layout)
            self.clear_layout(self.tx_content_layout)

            # Read RxPDO
            rx_mappings = self.backend.read_pdos(0x1C12)
            for mapping in rx_mappings:
                widget = PDOItemWidget(mapping)
                self.rx_content_layout.addWidget(widget)
            self.rx_content_layout.addStretch()

            # Read TxPDO
            tx_mappings = self.backend.read_pdos(0x1C13)
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

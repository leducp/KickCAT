from PySide6.QtWidgets import (
    QPushButton,
    QLabel,
    QFrame,
    QVBoxLayout,
    QHBoxLayout,
)

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
        color = self.STATE_COLORS.get(self.state_name, "#6B7280")
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

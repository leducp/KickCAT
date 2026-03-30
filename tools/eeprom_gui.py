"""
EtherCAT EEPROM Generator - PySide6 POC
Generates EEPROM binary for EtherCAT slaves.
Based on the SOES EEPROM generator (Victor Sluiter / Kuba Buda).
"""

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


# ---------------------------------------------------------------------------
# EEPROM generation logic (ported from EEPROM.js)
# ---------------------------------------------------------------------------

ESC_TYPES = ["ET1100", "LAN9252", "AX58100", "LAN9253 Beckhoff", "LAN9253 Direct", "LAN9253 Indirect"]
PORT_TYPES = ["Y (MII)", "K (EBUS)", "  (None)"]
PORT_MAP = {"Y (MII)": 0x01, "K (EBUS)": 0x03, "  (None)": 0x00}

EEPROM_SIZES = [128, 256, 512, 1024, 2048, 4096]


@dataclass
class EEPROMConfig:
    # Identity
    vendor_id: int = 0x000
    product_code: int = 0x00AB123
    revision: int = 0x002
    serial: int = 0x001

    # ESC
    esc: str = "ET1100"
    spi_mode: int = 3

    # Memory layout
    eeprom_size: int = 2048
    rx_mbx_offset: int = 0x1000
    tx_mbx_offset: int = 0x1200
    mbx_size: int = 512
    sm2_offset: int = 0x1400
    sm3_offset: int = 0x1A00

    # Strings
    device_type: str = "DigIn2000"
    group_type: str = "DigIn"
    image_name: str = "IMGCBY"
    device_name: str = "My EtherCAT Slave"

    # Ports (index into PORT_TYPES)
    port0: str = "Y (MII)"
    port1: str = "Y (MII)"
    port2: str = "  (None)"
    port3: str = "  (None)"

    # Protocols
    enable_coe: bool = True
    enable_foe: bool = False
    enable_eoe: bool = False

    # CoE details
    coe_sdo: bool = True
    coe_sdo_info: bool = True
    coe_pdo_assign: bool = False
    coe_pdo_config: bool = False
    coe_upload_startup: bool = True
    coe_complete_access: bool = False


def _find_crc(data: bytearray, length: int) -> int:
    crc = 0xFF
    gen_poly = 0x07
    for j in range(length):
        crc ^= data[j]
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ gen_poly
            else:
                crc <<= 1
            crc &= 0xFF
    return crc


def _wb(record: bytearray, byte_addr: int, value: int) -> None:
    record[byte_addr] = value & 0xFF


def _ww(record: bytearray, word_addr: int, value: int) -> None:
    record[word_addr * 2]     = value & 0xFF
    record[word_addr * 2 + 1] = (value >> 8) & 0xFF


def _wd(record: bytearray, word_addr: int, value: int) -> None:
    record[word_addr * 2]     = value & 0xFF
    record[word_addr * 2 + 1] = (value >> 8) & 0xFF
    record[word_addr * 2 + 2] = (value >> 16) & 0xFF
    record[word_addr * 2 + 3] = (value >> 24) & 0xFF


def generate_eeprom(cfg: EEPROMConfig) -> bytes:
    record = bytearray(cfg.eeprom_size)
    record[:] = bytes([0xFF] * cfg.eeprom_size)

    # --- PDI control / ESC-specific ---
    pdi_control = 0x05
    reserved_0x05 = 0x0000
    esc = cfg.esc
    if esc == "LAN9252":
        pdi_control = 0x80
    elif esc == "AX58100":
        reserved_0x05 = 0x001A
    elif esc == "LAN9253 Beckhoff":
        reserved_0x05 = 0xC040
    elif esc == "LAN9253 Direct":
        pdi_control = 0x82
        reserved_0x05 = 0xC040
    elif esc == "LAN9253 Indirect":
        pdi_control = 0x80
        reserved_0x05 = 0xC040

    # Word addresses 0-7
    _wb(record, 0, pdi_control)  # PDI control
    _wb(record, 1, 0x06)         # ESC config: DC sync/latch enabled
    _wb(record, 2, cfg.spi_mode) # SPI mode
    _wb(record, 3, 0x44)         # SYNC/LATCH config
    _ww(record, 2, 0x0064)       # Sync pulse length (10ns units)
    _ww(record, 3, 0x0000)       # Extended PDI config
    _ww(record, 4, 0x0000)       # Station alias
    _ww(record, 5, reserved_0x05)
    _ww(record, 6, 0x0000)
    crc = _find_crc(record, 14)
    _ww(record, 7, crc)

    # Word addresses 8-15: identity
    _wd(record, 8,  cfg.vendor_id)
    _wd(record, 10, cfg.product_code)
    _wd(record, 12, cfg.revision)
    _wd(record, 14, cfg.serial)

    # Word addresses 16-19
    _ww(record, 16, 0)
    _ww(record, 17, 0)
    _ww(record, 18, 0)
    _ww(record, 19, 0)

    # Bootstrap mailbox (only if FoE enabled)
    if cfg.enable_foe:
        _ww(record, 20, cfg.rx_mbx_offset)
        _ww(record, 21, cfg.mbx_size)
        _ww(record, 22, cfg.tx_mbx_offset)
        _ww(record, 23, cfg.mbx_size)
    else:
        _ww(record, 20, 0)
        _ww(record, 21, 0)
        _ww(record, 22, 0)
        _ww(record, 23, 0)

    # Standard mailbox
    _ww(record, 24, cfg.rx_mbx_offset)
    _ww(record, 25, cfg.mbx_size)
    _ww(record, 26, cfg.tx_mbx_offset)
    _ww(record, 27, cfg.mbx_size)

    # Protocol flags (ETG1000.6 Table 18)
    protocols = 0
    if cfg.enable_eoe: protocols |= 0x02
    if cfg.enable_coe: protocols |= 0x04
    if cfg.enable_foe: protocols |= 0x08
    _ww(record, 28, protocols)

    # Reserved area 29-61
    for i in range(29, 62):
        _ww(record, i, 0)

    eeprom_size_word = (cfg.eeprom_size // 128) - 1
    _ww(record, 62, eeprom_size_word)
    _ww(record, 63, 1)  # version

    # --- Vendor-specific info starting at 0x80 ---
    offset = 0x80

    # Strings category (type 0x000A)
    strings = [cfg.device_type, cfg.group_type, cfg.image_name, cfg.device_name]
    offset = _write_strings(record, offset, strings)

    # General settings category (type 0x001E)
    offset = _write_general(record, offset, cfg)

    # FMMU category (type 0x0028)
    offset = _write_fmmu(record, offset)

    # SyncManagers category (type 0x0029)
    offset = _write_syncmanagers(record, offset, cfg)

    return bytes(record)


def _write_strings(record: bytearray, offset: int, strings: list[str]) -> int:
    total_len = sum(len(s) for s in strings) + len(strings) + 1
    padded = total_len % 2 != 0

    _ww(record, offset // 2, 0x000A)  # category type STRING
    _ww(record, offset // 2 + 1, (total_len + 1) // 2)  # word length (rounded up)
    offset += 4

    _wb(record, offset, len(strings))
    offset += 1
    for s in strings:
        _wb(record, offset, len(s))
        offset += 1
        for ch in s:
            _wb(record, offset, ord(ch))
            offset += 1

    if padded:
        _wb(record, offset, 0)
        offset += 1

    return offset


def _coe_details(cfg: EEPROMConfig) -> int:
    v = 0
    if cfg.coe_sdo:            v |= 0x01
    if cfg.coe_sdo_info:       v |= 0x02
    if cfg.coe_pdo_assign:     v |= 0x04
    if cfg.coe_pdo_config:     v |= 0x08
    if cfg.coe_upload_startup: v |= 0x10
    if cfg.coe_complete_access:v |= 0x20
    return v


def _physical_port_word(cfg: EEPROMConfig) -> int:
    ports = [cfg.port3, cfg.port2, cfg.port1, cfg.port0]
    val = 0
    for p in ports:
        val = (val << 4) | PORT_MAP.get(p, 0)
    return val


def _write_general(record: bytearray, offset: int, cfg: EEPROMConfig) -> int:
    category_size = 0x10  # 16 words
    # Clear the region first
    for i in range(category_size + 2):
        _ww(record, offset // 2 + i, 0)

    _ww(record, offset // 2, 0x001E)        # type: General
    _ww(record, offset // 2 + 1, category_size)
    offset += 4

    _wb(record, offset, 2); offset += 1     # group info string index
    _wb(record, offset, 3); offset += 1     # image name string index
    _wb(record, offset, 1); offset += 1     # device order number string index
    _wb(record, offset, 4); offset += 1     # device name string index
    _wb(record, offset, 0); offset += 1     # reserved
    _wb(record, offset, _coe_details(cfg)); offset += 1
    _wb(record, offset, 1 if cfg.enable_foe else 0); offset += 1  # Enable FoE
    _wb(record, offset, 1 if cfg.enable_eoe else 0); offset += 1  # Enable EoE
    _wb(record, offset, 0); offset += 1     # reserved
    _wb(record, offset, 0); offset += 1     # reserved
    _wb(record, offset, 0); offset += 1     # reserved
    _wb(record, offset, 0); offset += 1     # flags
    _ww(record, offset // 2, 0x0000); offset += 2  # current consumption mA
    _ww(record, offset // 2, 0x0000); offset += 2  # pad
    _ww(record, offset // 2, _physical_port_word(cfg)); offset += 2
    offset += 14  # 14 pad bytes

    return offset


def _write_fmmu(record: bytearray, offset: int) -> int:
    _ww(record, offset // 2, 0x0028)  # type FMMU
    offset += 2
    _ww(record, offset // 2, 2)       # length: 2 words
    offset += 2
    _wb(record, offset, 1); offset += 1  # FMMU0: outputs
    _wb(record, offset, 2); offset += 1  # FMMU1: inputs
    _wb(record, offset, 3); offset += 1  # FMMU2: mailbox state
    _wb(record, offset, 0); offset += 1  # padding
    return offset


def _write_syncmanagers(record: bytearray, offset: int, cfg: EEPROMConfig) -> int:
    _ww(record, offset // 2, 0x0029)  # type SyncManager
    offset += 2
    _ww(record, offset // 2, 0x10)    # 16 words (4 SMs × 4 words)
    offset += 2

    # SM0 – Rx mailbox
    _ww(record, offset // 2, cfg.rx_mbx_offset); offset += 2
    _ww(record, offset // 2, cfg.mbx_size);      offset += 2
    _wb(record, offset, 0x26); offset += 1
    _wb(record, offset, 0x00); offset += 1
    _wb(record, offset, 0x01); offset += 1  # enabled
    _wb(record, offset, 0x01); offset += 1  # Mbx out

    # SM1 – Tx mailbox
    _ww(record, offset // 2, cfg.tx_mbx_offset); offset += 2
    _ww(record, offset // 2, cfg.mbx_size);      offset += 2
    _wb(record, offset, 0x22); offset += 1
    _wb(record, offset, 0x00); offset += 1
    _wb(record, offset, 0x01); offset += 1  # enabled
    _wb(record, offset, 0x02); offset += 1  # Mbx in

    # SM2 – Rx PDO
    _ww(record, offset // 2, cfg.sm2_offset); offset += 2
    _ww(record, offset // 2, 0);              offset += 2
    _wb(record, offset, 0x24); offset += 1
    _wb(record, offset, 0x00); offset += 1
    _wb(record, offset, 0x01); offset += 1
    _wb(record, offset, 0x03); offset += 1  # PDO

    # SM3 – Tx PDO
    _ww(record, offset // 2, cfg.sm3_offset); offset += 2
    _ww(record, offset // 2, 0);              offset += 2
    _wb(record, offset, 0x20); offset += 1
    _wb(record, offset, 0x00); offset += 1
    _wb(record, offset, 0x01); offset += 1
    _wb(record, offset, 0x04); offset += 1  # PDI

    return offset


# ---------------------------------------------------------------------------
# PySide6 GUI
# ---------------------------------------------------------------------------

class HexSpinBox(QLineEdit):
    """Line edit that accepts hex integers (e.g. 0x1000)."""

    def __init__(self, default: int = 0, parent=None):
        super().__init__(parent)
        self.setText(f"0x{default:X}")
        self.setPlaceholderText("0x0000")

    def value(self) -> int:
        try:
            return int(self.text(), 16)
        except ValueError:
            return 0


def _group(title: str, layout) -> QGroupBox:
    gb = QGroupBox(title)
    gb.setLayout(layout)
    return gb


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("EtherCAT EEPROM Generator")
        self.setMinimumWidth(560)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        container = QWidget()
        root = QVBoxLayout(container)
        root.setSpacing(8)

        # ---- Identity ----
        id_form = QFormLayout()
        self.vendor_id   = HexSpinBox(0x000)
        self.product_code= HexSpinBox(0x00AB123)
        self.revision    = HexSpinBox(0x002)
        self.serial      = HexSpinBox(0x001)
        self.device_name = QLineEdit("My EtherCAT Slave")
        id_form.addRow("Vendor ID:",      self.vendor_id)
        id_form.addRow("Product Code:",   self.product_code)
        id_form.addRow("Revision:",       self.revision)
        id_form.addRow("Serial Number:",  self.serial)
        id_form.addRow("Device Name:",    self.device_name)
        root.addWidget(_group("Device Identity", id_form))

        # ---- Protocols ----
        proto_layout = QVBoxLayout()
        self.chk_coe = QCheckBox("CoE  (CANopen over EtherCAT)")
        self.chk_foe = QCheckBox("FoE  (File over EtherCAT – firmware update)")
        self.chk_eoe = QCheckBox("EoE  (Ethernet over EtherCAT)")
        self.chk_coe.setChecked(True)
        proto_layout.addWidget(self.chk_coe)
        proto_layout.addWidget(self.chk_foe)
        proto_layout.addWidget(self.chk_eoe)

        coe_form = QFormLayout()
        self.chk_sdo       = QCheckBox(); self.chk_sdo.setChecked(True)
        self.chk_sdo_info  = QCheckBox(); self.chk_sdo_info.setChecked(True)
        self.chk_pdo_assign= QCheckBox()
        self.chk_pdo_conf  = QCheckBox()
        self.chk_upload    = QCheckBox(); self.chk_upload.setChecked(True)
        self.chk_complete  = QCheckBox()
        coe_form.addRow("SDO:",                self.chk_sdo)
        coe_form.addRow("SDO Info:",           self.chk_sdo_info)
        coe_form.addRow("PDO Assign:",         self.chk_pdo_assign)
        coe_form.addRow("PDO Configuration:",  self.chk_pdo_conf)
        coe_form.addRow("Upload at Startup:",  self.chk_upload)
        coe_form.addRow("SDO Complete Access:",self.chk_complete)

        self.coe_details_group = _group("CoE Details", coe_form)
        self.chk_coe.toggled.connect(self.coe_details_group.setEnabled)

        proto_layout.addWidget(self.coe_details_group)
        root.addWidget(_group("Protocols", proto_layout))

        # ---- ESC ----
        esc_form = QFormLayout()
        self.esc_combo = QComboBox()
        self.esc_combo.addItems(ESC_TYPES)
        self.spi_mode = QSpinBox()
        self.spi_mode.setRange(0, 3)
        self.spi_mode.setValue(3)
        esc_form.addRow("ESC Type:", self.esc_combo)
        esc_form.addRow("SPI Mode:", self.spi_mode)
        root.addWidget(_group("ESC Configuration", esc_form))

        # ---- Memory Layout ----
        mem_form = QFormLayout()
        self.eeprom_size  = QComboBox()
        self.eeprom_size.addItems([str(s) for s in EEPROM_SIZES])
        self.eeprom_size.setCurrentText("2048")
        self.rx_mbx = HexSpinBox(0x1000)
        self.tx_mbx = HexSpinBox(0x1200)
        self.mbx_sz = QSpinBox()
        self.mbx_sz.setRange(64, 4096)
        self.mbx_sz.setValue(512)
        self.sm2    = HexSpinBox(0x1400)
        self.sm3    = HexSpinBox(0x1A00)
        mem_form.addRow("EEPROM Size (bytes):", self.eeprom_size)
        mem_form.addRow("Rx Mailbox Offset:",   self.rx_mbx)
        mem_form.addRow("Tx Mailbox Offset:",   self.tx_mbx)
        mem_form.addRow("Mailbox Size:",        self.mbx_sz)
        mem_form.addRow("SM2 Offset (Rx PDO):", self.sm2)
        mem_form.addRow("SM3 Offset (Tx PDO):", self.sm3)
        root.addWidget(_group("Memory Layout", mem_form))

        # ---- Strings ----
        str_form = QFormLayout()
        self.device_type = QLineEdit("DigIn2000")
        self.group_type  = QLineEdit("DigIn")
        self.image_name  = QLineEdit("IMGCBY")
        str_form.addRow("Device Type:", self.device_type)
        str_form.addRow("Group Type:",  self.group_type)
        str_form.addRow("Image Name:",  self.image_name)
        root.addWidget(_group("Strings", str_form))

        # ---- Ports ----
        port_form = QFormLayout()
        self.ports = []
        for i in range(4):
            cb = QComboBox()
            cb.addItems(PORT_TYPES)
            cb.setCurrentText("Y (MII)" if i < 2 else "  (None)")
            self.ports.append(cb)
            port_form.addRow(f"Port {i}:", cb)
        root.addWidget(_group("Physical Ports", port_form))

        # ---- Generate button ----
        btn_row = QHBoxLayout()
        self.status_label = QLabel("")
        self.status_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        btn = QPushButton("Generate & Save EEPROM…")
        btn.setFixedHeight(36)
        btn.clicked.connect(self._generate)
        btn_row.addWidget(self.status_label)
        btn_row.addWidget(btn)
        root.addLayout(btn_row)

        root.addStretch()
        scroll.setWidget(container)
        self.setCentralWidget(scroll)

    # ------------------------------------------------------------------
    def _build_config(self) -> EEPROMConfig:
        return EEPROMConfig(
            vendor_id    = self.vendor_id.value(),
            product_code = self.product_code.value(),
            revision     = self.revision.value(),
            serial       = self.serial.value(),
            device_name  = self.device_name.text(),
            esc          = self.esc_combo.currentText(),
            spi_mode     = self.spi_mode.value(),
            eeprom_size  = int(self.eeprom_size.currentText()),
            rx_mbx_offset= self.rx_mbx.value(),
            tx_mbx_offset= self.tx_mbx.value(),
            mbx_size     = self.mbx_sz.value(),
            sm2_offset   = self.sm2.value(),
            sm3_offset   = self.sm3.value(),
            device_type  = self.device_type.text(),
            group_type   = self.group_type.text(),
            image_name   = self.image_name.text(),
            port0        = self.ports[0].currentText(),
            port1        = self.ports[1].currentText(),
            port2        = self.ports[2].currentText(),
            port3        = self.ports[3].currentText(),
            enable_coe   = self.chk_coe.isChecked(),
            enable_foe   = self.chk_foe.isChecked(),
            enable_eoe   = self.chk_eoe.isChecked(),
            coe_sdo          = self.chk_sdo.isChecked(),
            coe_sdo_info     = self.chk_sdo_info.isChecked(),
            coe_pdo_assign   = self.chk_pdo_assign.isChecked(),
            coe_pdo_config   = self.chk_pdo_conf.isChecked(),
            coe_upload_startup= self.chk_upload.isChecked(),
            coe_complete_access= self.chk_complete.isChecked(),
        )

    def _generate(self) -> None:
        cfg = self._build_config()

        # Basic validation
        if cfg.eeprom_size < 128:
            QMessageBox.warning(self, "Error", "EEPROM size must be at least 128 bytes.")
            return

        data = generate_eeprom(cfg)

        path, _ = QFileDialog.getSaveFileName(
            self, "Save EEPROM binary", "slave.bin",
            "Binary files (*.bin);;All files (*)"
        )
        if not path:
            return

        Path(path).write_bytes(data)
        self.status_label.setText(f"Saved {len(data)} bytes → {Path(path).name}")


def main():
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()

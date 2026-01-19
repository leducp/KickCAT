import sys
import argparse
from PySide6.QtWidgets import QApplication
import kickcat
from .mainwindow import EtherCATControlGUI

def main():
    parser = argparse.ArgumentParser(description="KickCAT EtherCAT GUI")
    parser.add_argument(
        "-i", "--interface", 
        default="enp8s0", 
        help="Network interface name (default: enp8s0)"
    )
    args = parser.parse_args()

    app = QApplication(sys.argv)

    try:
        print(f"Initializing EtherCAT on {args.interface}...")
        link = kickcat.create_link(args.interface, "")
        bus = kickcat.Bus(link)
        bus.init(0.1)
        bus.create_mapping()
        slaves = bus.slaves()
        print(f"Found {len(slaves)} slaves.")

        window = EtherCATControlGUI(bus, slaves)
        window.show()

        sys.exit(app.exec())
    except Exception as e:
        print(f"Error initializing EtherCAT: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

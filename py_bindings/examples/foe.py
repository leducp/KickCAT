#!/usr/bin/env python3

import argparse
import os
import sys

import kickcat


def main():
    parser = argparse.ArgumentParser(description="Read or write a file on a slave over FoE")
    parser.add_argument("-i", "--interface", help="Primary network interface (e.g., eth0)", required=True)
    parser.add_argument("-r", "--redundancy", help="Redundancy network interface (e.g., eth1)", default="")
    parser.add_argument("-s", "--slave", help="Slave index on the bus", type=int, default=0)
    parser.add_argument("-c", "--command", help="Transfer direction", choices=["read", "write"], required=True)
    parser.add_argument("-f", "--file", help="Local file", required=True)
    parser.add_argument("-n", "--name", help="Remote file name (default: local file name)", default="")
    parser.add_argument("-p", "--password", help="FoE password", type=lambda x: int(x, 0), default=0)
    args = parser.parse_args()

    name = args.name or os.path.basename(args.file)

    link = kickcat.create_link(args.interface, args.redundancy)
    bus = kickcat.Bus(link)
    bus.init(0.1)

    slaves = bus.slaves()
    if args.slave >= len(slaves):
        print(f"No slave {args.slave}: {len(slaves)} slave(s) on the bus", file=sys.stderr)
        return 1
    slave = slaves[args.slave]

    def progress(transferred):
        print(f"\r{transferred} bytes", end="", flush=True)

    try:
        if args.command == "read":
            data = bus.read_foe(slave, name, args.password, progress=progress)
            with open(args.file, "wb") as f:
                f.write(data)
        else:
            with open(args.file, "rb") as f:
                data = f.read()
            bus.write_foe(slave, name, data, args.password, progress=progress)
    except kickcat.ErrorFoE as e:
        print(f"\nFoE transfer failed: {e}", file=sys.stderr)
        return 1

    print(f"\n{args.command}: {len(data)} bytes, remote name '{name}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())

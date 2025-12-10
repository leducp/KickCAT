import kickcat
import time
import struct
import argparse
from dataclasses import dataclass
from kickcat import State


@dataclass
class FXOS8700CQ:
    accelerometer_x: int
    accelerometer_y: int
    accelerometer_z: int
    magnetometer_x: int
    magnetometer_y: int
    magnetometer_z: int

    @classmethod
    def from_bytes(cls, data: bytes):
        """Parse sensor data from bytes"""
        values = struct.unpack("<6h", data[:12])
        return cls(*values)


@dataclass
class LEDOutput:
    led_r: int = 0
    led_g: int = 0
    led_b: int = 0

    def to_bytes(self) -> bytes:
        return bytes([self.led_r, self.led_g, self.led_b])


def main():
    parser = argparse.ArgumentParser(
        description="EtherCAT master for Freedom K64F using EasyCAT"
    )

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
    bus.init(0.1)  # 100ms
    print("Bus Init done")

    bus.create_mapping()

    # Print slave info
    for slave in bus.slaves():
        print(
            f"Slave {slave.address}: input={slave.input_size} output={slave.output_size}"
        )

    easycat = bus.slaves()[0]

    print("Going to SAFE_OP")
    bus.request_state(State.SAFE_OP)
    bus.wait_for_state(State.SAFE_OP, 1.0)

    # Set valid output to exit safe op
    easycat.set_output_bytes(b"\xbb" * easycat.output_size)

    print("Going to OPERATIONAL")

    def cyclic_callback():
        bus.process_data()

    bus.request_state(State.OPERATIONAL)
    bus.wait_for_state(State.OPERATIONAL, 1.0, cyclic_callback)

    # Thresholds
    THRESHOLD_ACCEL = 1000
    THRESHOLD_MAG = 1000

    print("Running loop...")
    while True:
        try:
            bus.process_data()

            # Parse input data
            if len(easycat.input_data) >= 12:
                sensor = FXOS8700CQ.from_bytes(easycat.input_data)

                ax = sensor.accelerometer_x
                ay = sensor.accelerometer_y
                az = sensor.accelerometer_z

                mx = sensor.magnetometer_x
                my = sensor.magnetometer_y
                mz = sensor.magnetometer_z

                # LED toggle logic
                output = LEDOutput()
                output.led_r = (
                    1 if (ax > THRESHOLD_ACCEL or ax < -THRESHOLD_ACCEL) else 0
                )
                output.led_g = (
                    1 if (ay > THRESHOLD_ACCEL or ay < -THRESHOLD_ACCEL) else 0
                )
                output.led_b = 1 if (mz > THRESHOLD_MAG or mz < -THRESHOLD_MAG) else 0

                # Set output
                easycat.set_output_bytes(output.to_bytes())

                # Print status
                print(
                    f"Accel [X:{ax:5d} Y:{ay:5d} Z:{az:5d}] | "
                    f"Mag [X:{mx:5d} Y:{my:5d} Z:{mz:5d}] | "
                    f"LED [R:{output.led_r} G:{output.led_g} B:{output.led_b}]"
                )

            time.sleep(0.004)
        except Exception as e:
            print(f"Error in an iteration: {e}")


if __name__ == "__main__":
    main()

### Setup Nuttx project:

https://nuttx.apache.org/docs/latest/quickstart/install.html


### Build and flash

- Checkout nuttx and apps repository to `nuttx-12.x.y` tag.
- Copy the defconfig file into your Nuttx installation in `boards/arm/kinetis/freedom-k64f/configs/kickcat/defconfig` (you need to create the folder)
- In nuttx folder: `./tools/configure.sh -l freedom-k64f:kickcat`
- Download and add to your path a gcc > to 12.0. https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads

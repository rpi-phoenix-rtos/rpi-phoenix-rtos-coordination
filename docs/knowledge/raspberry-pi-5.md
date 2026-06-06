# Raspberry Pi 5 Notes

## 1. Why Pi 5 Is Second

Pi 5 is not just "Pi 4 but newer".

Major difference:

- BCM2712 application processor
- most board I/O is moved into RP1
- RP1 is connected over PCIe 2.0 x4

This changes the engineering problem from "port board peripherals" to "port a small southbridge-like subsystem behind PCIe".

## 2. Official Boot/Configuration Facts

Important official configuration facts:

- Pi 5 requires `config.txt` to be present for the partition to be treated as bootable.
- `enable_rp1_uart=1` can keep RP1 UART0 alive for early debug.
- `pciex4_reset=0` can preserve RP1 PCIe state across OS handoff for debugging.
- `os_check=0` disables DT compatibility checks for bare-metal development.
- `boot_load_flags` is not relevant on Pi 5 in the same way as on Pi 4 because there is no `start.elf`.

Implication:

- Pi 5 firmware has useful debug knobs, but they should not become the final architectural dependency.

## 3. SoC and DT Facts

Important Linux DT paths:

- `arch/arm64/boot/dts/broadcom/bcm2712.dtsi`
- `arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi`
- `arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts`
- `arch/arm64/boot/dts/broadcom/rp1.dtsi`

Important observations:

- CPU enable method is `psci`
- GIC is still `arm,gic-400`
- Broadcom PCIe instances exist in the AP
- RP1 is described as a PCIe-connected block with its own internal peripherals
- RP1 provides:
  - UARTs
  - SPI
  - I2C
  - GPIO
  - DMA
  - xHCI
  - Ethernet MAC
  - MMC
  - display/camera blocks
  - firmware mailbox relationships

## 4. RP1 Facts That Matter

Official RP1 documentation describes it as:

- connected to BCM2712 over PCIe 2.0 x4
- containing a dual-core Cortex-M3 management subsystem
- exposing many peripherals to the AP
- using MSI-X and mailbox/firmware-assisted functions in some cases

Key practical implication:

- Pi 5 support is partly a PCIe problem, partly an RP1 subsystem problem

## 5. Pi 5 Bring-Up Strategy

### Early debug strategy

Acceptable first-stage debug assists:

- `enable_rp1_uart=1`
- `pciex4_reset=0`
- `os_check=0`

These can make early bring-up easier, especially before RP1 reset/clock/init is fully native.

### Final strategy

The final Pi 5 port must not depend on preserved firmware state for core functions.

Long-term goals:

- native AP low-level bring-up
- native PCIe enumeration/management
- native RP1 interrupt routing
- native RP1 peripheral initialization

## 6. Driver Priority Order

### 6.1 UART

Two UART concerns exist:

- AP-side early console path
- RP1 UARTs for runtime and debug

Use firmware-preserved RP1 UART only as a temporary aid.

### 6.2 PCIe to RP1

This is the central enabler.

Required work:

- AP PCIe root support
- config space handling
- BAR mapping
- MSI/MSI-X handling
- stable RP1 interrupt domain treatment

### 6.3 RP1 wrapper / MFD layer

Linux uses RP1 wrapper logic and firmware support:

- `drivers/mfd/rp1.c`
- `drivers/firmware/rp1-fw.c`

Phoenix likely needs an equivalent concept:

- RP1 discovery
- BAR/base mapping
- interrupt demultiplexing
- shared firmware/mailbox interactions where unavoidable

### 6.4 RP1 low-speed peripherals

Next drivers should likely be:

1. GPIO/pinctrl
2. UART
3. I2C
4. SPI
5. MMC
6. Ethernet
7. xHCI

### 6.5 RP1 Ethernet

RP1 Ethernet is based on a Cadence GEM-class MAC:

- DT compatibility includes `raspberrypi,rp1-gem`, `cdns,macb`

This suggests the Ethernet driver can likely be developed around the Cadence MAC/GEM family rather than a Pi-unique network design.

### 6.6 RP1 USB host

Official RP1 docs state:

- two independent xHCI controllers
- each with USB 3 and USB 2 resources
- no device-mode support

Implication:

- once Phoenix has a generic xHCI HCD, Pi 5 USB should be a platform integration problem rather than a new USB architecture problem

## 7. Filesystem/Storage Notes

Pi 5 can use SD and other media, but early automation should keep the same discipline as Pi 4:

- firmware partition is separate
- Phoenix runtime rootfs is separate
- do not entangle runtime filesystem design with the firmware boot partition

## 8. Main Pi 5 Risks

### RP1 complexity

RP1 is effectively a second chip with firmware, interrupts, clocks, DMA, and many peripherals. Underestimating this will stall the port.

### False confidence from firmware-preserved state

Using `enable_rp1_uart` and `pciex4_reset=0` can get early output quickly, but can also hide missing native initialization work.

### Missing comparative BSD coverage

NetBSD notes Pi 5 support but still reports gaps such as Ethernet not working in some paths. This is evidence that Pi 5 is still a moving target, not a routine board port.

## 9. Pi 5 Entry Criteria

Do not begin full Pi 5 implementation unless Pi 4 already has:

- stable build artifacts
- stable real-hardware regression loop
- working PCIe and xHCI foundation
- clear AArch64 common code separation

## 10. Pi 5 Success Criteria

### Phase 1

- `plo` boots
- UART works
- kernel boots single-core

### Phase 2

- RP1 GPIO/UART/I2C/SPI/MMC work

### Phase 3

- RP1 Ethernet and xHCI work

### Phase 4

- display/audio/camera work

### Phase 5

- stable long-run regression and board automation

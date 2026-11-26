# USB drivers for MorphOS

These two drivers come from AROS and let `aos-phone` on MorphOS take
pictures from a USB camera:

- `uvc.class` drives USB video cameras (USB Video Class) under Poseidon,
  the USB stack of MorphOS. It publishes each camera as a VHI library,
  `uvc0.vhi` for the first one, which `aos-phone` offers as the camera
  `VHI: <camera name>` (option `CAMERA=VHI`). It delivers uncompressed
  pictures (YUY2) only; MJPEG, which MorphOS has no decoder library for,
  and USB 3 cameras are left out.
- `pcixhci.device` drives xHCI (USB 3) host controllers. MorphOS has no
  driver of its own for them, so this one takes over a controller that
  nothing else uses. The camera needs it because the EHCI driver of
  MorphOS does not carry the isochronous streams of high-speed cameras:
  on EHCI a camera binds and starts a stream, but no picture arrives.

## Status

Both drivers are experimental. They were tried only in QEMU, on its
emulated Pegasos II (`pegasos2`) with QEMU's xHCI controller and an
A4tech HD 720P camera passed through from the host: the camera enumerates,
`uvc.class` delivers 320x240 pictures, and in a call the other side
receives them. The installation below was tried the same way, also with
the drivers loaded from `S:user-startup` at boot. The emulated processor
is too slow for a steady picture, so a whole frame takes from five seconds
to half a minute there. Neither driver has run on a real machine yet.

## Installing

The archive holds the files under the paths they take below `SYS:`:

| File | Goes to |
|---|---|
| `MorphOS/Classes/USB/uvc.class` | `SYS:MorphOS/Classes/USB/` |
| `Devs/USBHardware/pcixhci.device` | `SYS:Devs/USBHardware/` (create the drawer) |

The class goes next to the USB classes of the system, because
`AddUSBClasses` reads `SYS:MorphOS/Classes/USB` only; it does not find a
class in `SYS:Classes/USB`. Then, in a Shell:

```
AddUSBHardware USBHardware/pcixhci.device 0
AddUSBClasses
```

`AddUSBHardware` hands the first xHCI controller (unit 0) to Poseidon, which
then finds the devices on it. `AddUSBClasses` adds `uvc.class`, and
Poseidon binds it to the camera. Both last until the next restart; to load
the drivers at every start, add the two lines, with `QUIET`, to the end of
`S:user-startup`:

```
AddUSBHardware USBHardware/pcixhci.device 0 QUIET
AddUSBClasses QUIET
```

`aos-phone` lists the cameras present when it starts, so start it after
Poseidon has found the camera; under emulation that takes about 20 seconds
after the drivers are loaded.

## Building

Each driver is a CMake project of its own, built with the MorphOS cross
toolchain (see `scripts/build-morphos-toolchain.sh`):

```sh
cmake -S drivers/uvc -B build-uvc -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/morphos-ppc.cmake \
      -DMORPHOS_TOOLCHAIN_DIR=<toolchain>
cmake --build build-uvc
```

`drivers/xhci` builds the same way into `pcixhci.device`.
`scripts/build-release.sh` builds both for a MorphOS release and packs them
into `aos-linphone-drivers-<version>-morphos-ppc.lha` and `.zip`.

## Licence

Both drivers keep the licence of the AROS code they come from, the AROS
Public License, version 1.1: `LICENSE` in the archive, `drivers/uvc/LICENSE`
and `drivers/xhci/LICENSE` in the source. They are not under the AGPL of
the rest of aos-linphone.

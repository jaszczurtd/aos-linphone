# aos-linphone

Author: Marcin "Jaszczur" Kielesiński.

A SIP phone for AmigaOS-like systems, with voice and video calls and text chat in a native window interface. It is built on
[liblinphone](https://gitlab.linphone.org/BC/public/linphone-sdk), the SIP
library behind the Linphone apps on other platforms.

Development happens on AROS, where the phone runs today. The goal is to run
on every Amiga system: AmigaOS, AmigaOS 4 and MorphOS.

## What works

The project is young, but the application, `aos-phone`, already has full sound
and video calls. It:

- registers a SIP account, makes calls and answers them, or calls another
  phone directly by its address, without a server;
- sends your voice from the microphone and your picture from a USB camera,
  and plays the other side's voice and shows their picture;
- turns the video of a call on and off;
- sends and receives text messages;
- records calls and plays sound files into them.

Sound uses Opus, G.722, Speex or G.711, video uses VP8. For tests without
devices, prepared sound and video files can stand in for the microphone
and the camera.

## Tested setup

Sound and video calls were tested on AROS in a virtual machine:

- AROS x86_64 built from the AROS `master` branch (revision `9c39558b`,
  November 2026) and installed on the disk of the virtual machine.
- The virtual machine: QEMU/KVM under libvirt on a Linux host, one CPU
  core, 2 GB of memory, an Intel HD Audio card (the AROS `hdaudio.audio`
  driver under AHI), an e1000 network card and a USB 2 controller (EHCI
  with UHCI).
- The camera: an A4tech HD 720P PC Camera (USB `09da:2697`) passed through
  to the virtual machine. AROS drives it with `uvc.class`, and the phone
  takes its pictures through the VHI library that `uvc.class` publishes
  (`uvc0.vhi`), uncompressed (YUY2) at 640x480, about 15 pictures per
  second in calls.
- The microphone: the one built into that camera (USB `1b3f:2008`). AROS's
  `usbaudio.class` makes no AHI mode for a USB device that only records,
  so the microphone stayed on the Linux host and reached the sound input
  of the virtual machine through the host's sound system. The phone records
  it through AHI like any other input.
- SIP over UDP, with the media over RTP on UDP.

The other side of the test calls was:

- `linphonec` (linphone-cli 5.2.0) on Linux Mint 22.3 in a second virtual
  machine: calls with sound and video directly between the two machines
  (sound also with SRTP encryption), and calls with sound through
  Asterisk;
- Linphone on an Android phone, through Asterisk: sound and VP8 video in
  both directions, with standard echo cancelation.

### Asterisk

The tests used Asterisk 20 with PJSIP. The endpoints allowed G.711
(`allow = ulaw,alaw`) for sound and VP8 (`allow = vp8`) for video.
Asterisk does not convert video, so both phones need a video codec the
endpoint allows, and `aos-phone` has only VP8. Opus or G.722 can be added
for better sound where the Asterisk build supports them; that was not
tested. The phone in the test sat behind NAT, so its endpoint also had
`rewrite_contact = yes`, `force_rport = yes`, `rtp_symmetric = yes` and
`direct_media = no`.

### Calls without a server

Calls and messages also work without a SIP server or an account: call
`sip:<name>@<IP address>:<port>` of the other phone. Both phoneapps must
reach each other's IP address directly, in one local network or VPN, or
across a router that forwards the SIP and RTP ports. `aos-phone` does not
use ICE or STUN (yet), so it does not get through NAT on its own. Start it with
a fixed `PORT` (for example 5060) so the other side knows where to call.

## Getting started

For now the phone is cross-compiled for AROS x86_64:

1. Build the linphone SDK and the libraries it needs, as described in
   [sdk/README.md](sdk/README.md).
2. Build, install and run the phone with the help of
   [apps/aos-phone/README.md](apps/aos-phone/README.md).

`aos-register` is a small console program that only registers a SIP
account. It comes in handy for checking the network and the SIP server
before you start the phone; see
[apps/aos-register/README.md](apps/aos-register/README.md).

## Technical details

[ARCHITECTURE.md](ARCHITECTURE.md) explains how the project fits together,
how to build and test its parts and the known limits of the system.
[CONTRIBUTING.md](CONTRIBUTING.md) covers the git hooks, the code style and
the form of commit messages.

## Licence

aos-linphone is free software under the GNU Affero General Public License,
version 3 or later, the same licence as liblinphone. See
[LICENCE.md](LICENCE.md) for the terms and the licences of the libraries
it uses.

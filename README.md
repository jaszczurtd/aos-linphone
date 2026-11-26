# aos-linphone

Author: Marcin "Jaszczur" Kielesiński.

A SIP phone for AmigaOS-like systems, with voice and video calls and text chat in a native window interface. It is built on
[liblinphone](https://gitlab.linphone.org/BC/public/linphone-sdk), the SIP
library behind the Linphone apps on other platforms.

The phone runs on AROS x86_64 and on MorphOS. AROS is where development
started and where it gets the most testing; MorphOS has so far been tried
under emulation only. AmigaOS 3.x and AmigaOS 4 come next, and
[PORTS.md](PORTS.md) describes where those ports stand.

## What works

The project is young, but the application, `aos-phone`, already has full sound
and video calls. It:

- registers SIP accounts, also on Linphone's free sip.linphone.org
  service, from just a server, a user name and a password, keeps them until
  you remove them, and shows the state of the one it calls through with a
  lamp (green when registered, yellow while registering, red otherwise);
  wrong account details or a server out of reach show up in a message
  window;
- makes calls and answers them, or calls another phone directly by its
  address or host name, without a server;
- finds a way for the media through NAT with ICE and a STUN server;
- sends your voice from the microphone and your picture from a USB camera,
  and plays the other side's voice and shows their picture;
- turns the video of a call on and off;
- sends and receives text messages;
- records calls and plays sound files into them;
- comes for AROS as LHA and ZIP archives with icons, ready to unpack.

Sound uses Opus, G.722, Speex or G.711, video uses VP8. HTTPS with
certificate checks works too, which creating linphone.org accounts from the
application will need. For tests without devices, prepared sound and video
files can stand in for the microphone and the camera.

## Tested setup

### AROS

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

Registration with sip.linphone.org works over UDP, with the server found
through DNS (SRV records). A call through sip.linphone.org between the AROS
machine and `linphonec` connected with sound and video, with ICE and the
STUN server of linphone.org (option `STUN`): the phone learned its public
address, and the server offered its own media relay as a fallback.

### MorphOS

The MorphOS build was tested in QEMU, without a real PowerPC machine:

- MorphOS 3.21 on QEMU's `pegasos2` machine (an emulated Pegasos II),
  installed on the disk of the virtual machine. QEMU emulates the PowerPC
  processor in software, without hardware assistance.
- The programs built with GCC 15.2 and SDK 3.20, cross-compiled on Linux.
- The camera: the same A4tech camera, on QEMU's xHCI controller, driven by
  the xHCI driver and `uvc.class` that the project ports from AROS
  (`drivers/`).

`aos-register` registers an account on a test registrar, and the phone
opens its MUI window and ARexx port. A video call with the phone on AROS
shows each side the other's picture. Calls with `linphonec` on Linux carry
video and sound in both directions, with prepared sound files standing in
for the microphone. Under emulation the Opus encoder cannot keep up, so
sound sent from MorphOS has gaps; with G.711 it is clean. The camera's
picture reaches the other side, but only every few seconds, because the
emulated processor cannot take in the camera's stream fast enough.

Sound cards were not tested, because the emulated Pegasos II has no AHI
driver. The network stack of MorphOS does not report the DNS servers it
got by DHCP, so the phone finds a SIP server through DNS SRV records only
when a DNS server is set by hand in the network preferences; without it, it
uses the address of the domain itself. [PORTS.md](PORTS.md#morphos) has the
details.

### Asterisk

The tests used Asterisk 20 with PJSIP. The endpoints allowed G.711
(`allow = ulaw,alaw`) for sound and VP8 (`allow = vp8`) for video.
Asterisk does not convert video, so both phones need a video codec the
endpoint allows, and `aos-phone` has only VP8. Opus or G.722 can be added
for better sound where the Asterisk build supports them; that was not
tested. The phone in the test sat behind NAT, so its endpoint also had
`rewrite_contact = yes`, `force_rport = yes`, `rtp_symmetric = yes` and
`direct_media = no`.

### Calling mobile phones

Android and iOS put a phone app to sleep when it is not on the screen, and
a sleeping app holds no SIP connection, so an incoming call never reaches
it. To make the phone ring, the SIP server has to wake the app with a push
notification (Firebase on Android, Apple's push service on iOS) and send the
call when the app has registered again. The Linphone apps announce their
push token when they register; sip.linphone.org has the push backend that
uses it, so calls from `aos-phone` registered there wake a Linphone app on
the same service. Asterisk has no such backend of its own: through it, the
mobile app answers only while it is open or kept running in the background
(on Android, for example as a foreground service with battery optimisation
turned off). In our test calls through Asterisk, the Android app was open.

### Calls without a server

Calls and messages also work without a SIP server or an account: call
`sip:<name>@<IP address>:<port>` of the other phone. The SIP messages go
straight to that address, so both phones must reach each other's IP address
directly, in one local network or VPN, or across a router that forwards the
SIP and RTP ports. ICE (option `STUN`) helps only the media find a way, not
the call itself. Start the phone with a fixed `PORT` (for example 5060) so
the other side knows where to call.

## Getting started

The releases of the GitHub repository carry ready archives, as `.lha` and
`.zip`: `aos-linphone-<version>-aros-x86_64` for AROS and, from the release
after 0.1, `aos-linphone-<version>-morphos-ppc` for MorphOS. Unpack one
where the drawer should live, for example to `Work:`; it holds the drawer
`aos-linphone` with its icon. Double-click `aos-phone` inside, or start it
from a shell (see [apps/aos-phone/README.md](apps/aos-phone/README.md)).

On MorphOS, a USB camera also needs the xHCI driver and `uvc.class` ported
from AROS. They come in an archive of their own,
`aos-linphone-drivers-<version>-morphos-ppc`, and are experimental; see
[drivers/README.md](drivers/README.md).

To build the phone yourself, cross-compile it for AROS x86_64 or MorphOS:

1. Build the linphone SDK and the libraries it needs, as described in
   [sdk/README.md](sdk/README.md).
2. Build, install and run the phone with the help of
   [apps/aos-phone/README.md](apps/aos-phone/README.md).
3. Optionally build and pack everything like a release with
   `scripts/build-release.sh`, see [ARCHITECTURE.md](ARCHITECTURE.md#packing).

`aos-register` is a small console program that only registers a SIP
account. It comes in handy for checking the network and the SIP server
before you start the phone; see
[apps/aos-register/README.md](apps/aos-register/README.md).

## Technical details

[ARCHITECTURE.md](ARCHITECTURE.md) explains how the project fits together,
how to build and test its parts and the known limits of the system.
[CONTRIBUTING.md](CONTRIBUTING.md) covers the git hooks, the code style and
the form of commit messages. [PORTS.md](PORTS.md) describes the MorphOS
port and collects what we know about ports to AmigaOS 3.x and AmigaOS 4.

## Licence

aos-linphone is free software under the GNU Affero General Public License,
version 3 or later, the same licence as liblinphone. The MorphOS drivers
in `drivers/`, ported from AROS, keep the AROS Public License. See
[LICENCE.md](LICENCE.md) for the terms and the licences of the libraries
it uses.

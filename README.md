# aos-linphone

Author: Marcin "Jaszczur" Kielesiński.

A SIP phone for AmigaOS-like systems, with voice and video calls and text chat in a native window interface. It is built on
[liblinphone](https://gitlab.linphone.org/BC/public/linphone-sdk), the SIP
library behind the Linphone apps on other platforms.

Development happens on AROS, where the phone runs today. The goal is to run
on every Amiga system: AmigaOS, AmigaOS 4 and MorphOS.

## What works

The project is young, but you can already make real calls. The phone,
`aos-phone`:

- registers a SIP account, makes calls and answers them;
- plays the other side's voice and ring tones, and shows their picture;
- sends and receives text messages;
- records calls and plays sound files into them.

It does not use a microphone or a camera yet, so the other side hears
silence (or the sound file you play) and sees a test picture. For tests,
prepared sound and video files can stand in for the microphone and the
camera. Real devices come next.

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

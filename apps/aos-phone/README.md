# aos-phone

A SIP phone for AROS and MorphOS, built on liblinphone, with a Zune
interface on AROS and MUI on MorphOS. It
registers an account, makes and answers calls with sound and video, and
exchanges text messages. You hear the other side and the ring tone through
AHI and see its picture in the call window. Your voice comes from an AHI
input, and your picture from a USB camera that `uvc.class` drives (on
MorphOS, the port in [drivers/](../../drivers/README.md)).
For tests, prepared raw files can stand in for the microphone and the
camera (see [Test files](#test-files)). Calls can also be recorded to
files.

```
aos-phone [DATA=<directory>] [PORT=<number>] [RECORD=<directory>]
          [CAMERA=<name>] [VERBOSE]
          [TESTVIDEO=<file>] [TESTVIDEOSIZE=<width>x<height>] [TESTVIDEOFPS=<n>]
          [TESTSOUND=<file>] [TESTSOUNDRATE=<n>] [MICUNIT=<n>]
          [STUN=<server>]
```

- `DATA`: where the program keeps its settings, including the account and
  its password; default `PROGDIR:aos-phone-data`. Give a directory, not the
  root of a volume.
- `PORT`: the UDP port for SIP; by default a random free one. A fixed port
  helps when others call this computer by its IP address, without a SIP
  server.
- `RECORD`: records every call to a WAV file in this directory, both sides
  mixed, from the moment the call connects; by default nothing is recorded.
  The program creates the directory when it is missing and names the files
  `call-<date>-<time>-<n>.wav`.
- `CAMERA`: the camera video comes from, given by a part of its name:
  `VHI` a USB camera of `uvc.class`, `Mire` a moving test picture,
  `StaticImage` a still picture and `File` the pictures of `TESTVIDEO`.
  Without it, calls use the `TESTVIDEO` file when there is one, else the
  first USB camera, else the still picture. A name no camera has keeps the
  default and says so in the status line.
- `VERBOSE`: prints all messages of liblinphone, not only warnings and
  errors. They tell, among other things, which camera, codecs and picture
  sizes a call uses.
- `TESTVIDEO`, `TESTVIDEOSIZE`, `TESTVIDEOFPS`: add a camera that plays a
  file of raw pictures; `TESTSOUND`, `TESTSOUNDRATE`: let the microphone
  of the AHI card play a file of raw sound. See [Test files](#test-files).
- `MICUNIT`: the unit of `ahi.device` (0 to 3) the microphone is recorded
  from; default 0. Its input (microphone, line) is the one set for that
  unit in the AHI preferences.
- `STUN`: a STUN server, for example `stun.linphone.org`, which turns on
  ICE: calls then learn their public address and try several ways for the
  media, which helps when one or both sides sit behind NAT. By default
  calls use no ICE.

The program needs the network stack running (AROSTCP on AROS) and the belr grammars in
`share/belr/grammars/` next to it, as `aos-register` does (see
[its README](../aos-register/README.md#running)). It prints what happens
to its standard output.

Sound goes to the default unit of `ahi.device`, which you choose in the AHI
preferences, and the microphone is recorded from the unit `MICUNIT`, with
echo cancellation, so a call through loudspeakers does not send the other
side back its own voice. The ring tone and the tone you hear while the other side
rings are files that the SDK build does not install; copy them from the
submodule next to the program:

| From `third_party/linphone-sdk/liblinphone/share/` | To |
|---|---|
| `rings/oldphone-mono.wav` | `share/sounds/linphone/rings/oldphone-mono.wav` |
| `ringback.wav` | `share/sounds/linphone/ringback.wav` |

Without them the calls work, but nothing rings. The still picture comes
from `share/images/nowebcamCIF.jpg`, which the SDK installs into
`<prefix>/share/images/`; copy it next to the program too, or the camera
sends a black picture.

Started from its icon, `aos-phone` takes the same settings from tool types
of the same names. Its output then
goes nowhere, so no window opens for it. The tool type `DEBUG` shows the
output in a console window, which stays open after the program ends until
you close it. The icon comes with these tool types switched off, in
parentheses: remove the parentheses in the icon information to switch one
on. When the program cannot start
from its icon (no network, for one), it says why in a requester.

## Test files

For tests without a microphone or a camera, two options feed calls from
files instead, through the same devices: the camera appears among the
others, and the AHI card records the file instead of `MICUNIT`. The files are raw,
without a header, so their format comes with the options.

`TESTVIDEO=<file>` adds the camera `File: <file name>`. The file holds
pictures one after another, 3 bytes per pixel in the order red, green,
blue, rows from the top. `TESTVIDEOSIZE` gives their size (default
`320x240`) and `TESTVIDEOFPS` how many play per second (default 15, at
most 60). Calls send video from this camera unless `CAMERA` names another
one.

`TESTSOUND=<file>` makes the AHI card capture the file: signed 16-bit mono
samples in the byte order of the processor, `TESTSOUNDRATE` of them per
second (default 16000). The program resamples them for the codec of the
call, and it turns echo cancellation off, since a file holds no echo of
the loudspeaker.

Both files play in a loop, at the pace of their format, for as long as the
call runs. The program checks them at start and refuses to start when one
cannot be opened or is shorter than one picture of the given size.

On Linux, ffmpeg makes such files from any video or sound file, or from
its own test pattern:

```sh
ffmpeg -f lavfi -i testsrc2=size=320x240:rate=15 -t 3 \
       -f rawvideo -pix_fmt rgb24 clip.rgb
ffmpeg -i speech.wav -ac 1 -ar 16000 -f s16le voice.raw
```

The first command makes 3 seconds of a moving test pattern with a frame
counter, 45 pictures of 230400 bytes each; the second turns a sound file
into 16 kHz mono samples. `s16le` is the byte order of x86 processors; for
a big-endian Amiga use `s16be`. Then start the program with, for example,
`TESTVIDEO=RAM:clip.rgb TESTSOUND=RAM:voice.raw`.

## Using it

The main window shows the account, an address field and the list of
calls. The lamp left of the account shows its registration: green when the
server has registered it, yellow while the program registers it, red when
it is not registered (no account, no network, a server out of reach or
refusing the password). Holding the mouse over the lamp names the state.

- **Account...** opens the account window with the list of accounts. The
  fields go Server, Identity, Password, and `sip:` already stands in front
  of the first two. Enter the server, e.g. `sip.linphone.org` or
  `10.8.0.1:5060`, then the user name: the identity takes its domain from
  the server, port included, and shows it after the field, e.g.
  `sip: alice @sip.linphone.org`. An account in another domain than its
  server takes `alice@example.org`. Holding the mouse over a field explains
  it. **Register** adds the account, or updates the one with that identity
  (an empty password keeps the saved one), and registers it; pressed again,
  it tries once more, e.g. after a failure. A missing field, an invalid
  identity, a refused password or a server out of reach opens a message
  window; the status line says it too. Every account stays saved and registers again at each start until
  **Unregister**, and stays in the list until **Remove...**, which asks
  first. Calls go out through one account, shown in bold in the list and on
  the main window; **Use for calls** picks another, and a new account takes
  over when it is added. Picking an account in the list fills the fields;
  **New** empties them for another account. Calls to any registered account
  come in.
- **Call** calls the address in the field: a full SIP address, or a user
  name in the account's domain. The call window opens with the other party,
  the state, the duration and the sound levels. While a call comes in, its
  state shows in bold next to a green light with spreading waves. **Answer** and **Hang up**
  act on the call shown there; selecting a call in the list shows it.
- **Chat** opens a chat window for the address. Messages that arrive open
  the window of their sender by themselves. Windows are matched by the user
  name of the address, so `alice` and `sip:alice@example.org` share one.

Zune shows 8-bit text in the code page of the system. Received messages are
converted from UTF-8, and characters the code page lacks show as `?`; what
you type is sent as UTF-8. Letters beyond Latin-1, Polish ones for example,
also need a system code page that has them (set by the Locale preferences)
and an outline font with their glyphs in the Zune preferences: the bitmap
fonts AROS installs cover Latin-1 only.

Every call offers video and accepts it when the other side offers it. The
call window then shows the picture of the other side and, next to it, the
picture this side sends; both keep their proportions when you resize the
window. Video uses VP8, so the other client has to support it; a client or
SIP server without video leaves the call with sound only. The program
announces a download bandwidth of 512 kbit/s, so the other side sends
pictures of 640x480 or smaller, small enough to decode in time; `download_bw`
in the `[net]` section of `linphonerc` sets another limit.

**Video** in the call window turns the video of a connected call on or off.
The program asks the other side with a new offer (a re-INVITE), and the
button follows what the call ends up with: when the other side refuses the
picture, it pops back out. Without video, the call window shrinks to the
sound levels and the buttons.

Calls use G.711, G.722, Speex or Opus for sound, whichever both sides
support. To encrypt the media with SRTP, add `media_encryption=srtp` to the `[sip]`
section of `linphonerc` in the `DATA` directory while the program is not
running; the other client then has to support SRTP as well.

## ARexx port

The port `AOSPHONE.1` takes these commands; they do what the buttons do:

| Command | Effect |
|---|---|
| `REGISTER <identity> <password> [<server>]` | adds the account or updates it, registers it and calls through it; the identity and server work as in the account window, e.g. `REGISTER alice secret sip.linphone.org` or `REGISTER sip:alice@example.org secret` |
| `UNREGISTER [<identity>]` | unregisters that account, by default the one calls go through |
| `REMOVEACCOUNT <identity>` | removes the account and its password, without asking |
| `USEACCOUNT <identity>` | makes calls go out through that account |
| `ACCOUNTS` | returns the accounts, e.g. `sip:a@x.org registered default\|sip:b@y.org not registered` |
| `CALL <address>` | calls the address |
| `ANSWER` | answers the call in the call window, or the first incoming call |
| `HANGUP` | declines or ends the call in the call window, or the current call |
| `PLAY <file>` | plays a WAV file to the other side of the connected call in the call window, or of the current call |
| `SNAPSHOT <file>` | saves the next picture received in that call as a JPEG file |
| `VIDEO [ON\|OFF]` | turns the video of that call on or off; without an argument, switches it |
| `SEND <address> <text>` | sends a text message and shows it in its chat window |
| `STATUS` | returns the state of the account calls go through, the SIP port and the calls, e.g. `registered\|port 5060\|call 1 connected sip:bob@example.org video` |
| `QUIT` | ends the program |

A command returns 0, or 10 when it could not be passed on. Its `RESULT` is
the state for `STATUS` and `OK` or `ERROR` for the others.

`PLAY` mixes the file into what the call sends; a new `PLAY` replaces the
file still playing. The file may have any sample rate. After the call has
been paused, resumed or renegotiated, `PLAY` refuses with a message in the
status line: liblinphone keeps the player of the first sound stream of the
call, which such a change replaces.

`SNAPSHOT` works in a call with video. The status line names the file when
the request is passed to the call; the picture is written a moment later,
when the next one arrives.

`aos-phone-cmd` sends one command to the port and prints the result,
without an ARexx interpreter:

```
aos-phone-cmd [PORT=<name>] <command>
```

For example `aos-phone-cmd CALL sip:bob@example.org` or
`aos-phone-cmd STATUS`. Its exit code is the command's, or 20 when the port
does not exist.

## Building

Build and install the SDK first, see [sdk/README.md](../../sdk/README.md).
Then configure with the same prefix, toolchain file and initial cache:

```sh
cmake -S apps/aos-phone -B build-phone -G Ninja \
      -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
      -DAROS_TOOLCHAIN_DIR=<toolchain> \
      -DAOS_ILBMTOICON=<AROS build>/bin/<host>/tools/ilbmtoicon
cmake --build build-phone
```

For MorphOS: `-C sdk/morphos-sdk.cmake`,
`-DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/morphos-ppc.cmake` and
`-DMORPHOS_TOOLCHAIN_DIR=<toolchain>`.

The build gives `aos-phone`, `aos-phone.info`, `aos-phone-cmd`,
`aos-linphone.info` and the tests `test_queue`, `test_video_display`,
`test_capture`, `test_hw_sources` (AROS only), `test_dns` and `test_https`. `aos-phone.info` is the
Workbench icon of the program; copy it next to the program.
`aos-linphone.info` is the icon of the drawer that holds it: copy it next to
the drawer, e.g. to `Work:aos-linphone.info` for `Work:aos-linphone`.
`ilbmtoicon` is a host tool built with AROS itself (its source,
`tools/ilbmtoicon/ilbmtoicon.c` in the AROS tree, also builds on its own
with `gcc ilbmtoicon.c -lpng -lz`); without `AOS_ILBMTOICON` (or
`ilbmtoicon` in the `PATH`) the build skips the icons. Both icons are made
from `icons/aos-phone.png`; `icons/aos-phone.info.src` also holds the stack
size and tool types of the program, `icons/aos-linphone.info.src` the size
of the drawer window. With
`-DAOS_TEST_RUNNER=...`, as for the tests of the compatibility layer (see
[ARCHITECTURE.md](../../ARCHITECTURE.md#running-the-tests)), CTest runs the
tests on AROS.

## Structure

| File | Contents |
|---|---|
| `src/core.c`, `src/core.h` | the linphone core in its own thread; the only code that calls liblinphone |
| `src/queue.c`, `src/queue.h` | the fixed-size queues for commands and events between the two threads |
| `src/gui.c`, `src/gui.h` | the Zune windows, the ARexx port and the handling of core events |
| `src/text.c`, `src/text.h` | conversion between UTF-8 and the system code page |
| `src/video.c`, `src/video.h` | the Zune area that draws one picture of a call |
| `src/lamp.c`, `src/lamp.h` | the Zune area that draws the account lamp |
| `src/main.c` | arguments, tool types, test files and start-up |
| `src/cmd.c` | `aos-phone-cmd` |
| `icons/` | the image and descriptions of the Workbench icons |
| `tests/test_queue.c` | tests of the queues, run on AROS and MorphOS |
| `tests/test_dns.c` | tests of name resolution with `getaddrinfo()` and the resolver of belle-sip, run on AROS and MorphOS |
| `tests/test_https.c` | tests of HTTPS through belle-sip's HTTP client (TCP, TLS, certificate check), run on AROS and MorphOS |
| `../../devices/source.h` | the interface of the sources a camera or microphone takes its data from |
| `../../devices/ahi/` | the AHI sound card for mediastreamer2, shared with other programs |
| `../../devices/video/` | the video display, the camera that shows a source and the Mire test camera |
| `../../devices/files/` | sources that play raw picture and sound files |
| `../../devices/tests/` | tests of the video display and of the capture devices, run on AROS and MorphOS (the hardware sources on AROS only) |

The core thread creates the linphone core and makes every liblinphone call,
including the callbacks; each such function checks that it runs in that
thread and stops the program otherwise. The interface copies commands into
one queue and takes events from the other. After each event the core
thread sends an Exec signal, which the interface handles as a Zune input
handler, so events also arrive while a nested Zune loop runs.

The core thread registers the AHI card with mediastreamer2 right after it
creates the core. Each sound that plays, the call or the ring tone, gets
its own filter with a writer thread, which opens `ahi.device` and keeps
two linked 40 ms writes going. The filter only queues the samples, so the
mediastreamer2 ticker does not wait for the device.

Pictures take another way. The core thread registers the video display
filter of the device layer and gives liblinphone, as the video window, a
surface that the interface created. The filter runs in the mediastreamer2
ticker: it scales each picture to the size of the area that shows it,
converts it to ARGB and leaves it on the surface, then wakes the interface
with a second Exec signal. The interface draws the newest picture with
`WritePixelArray()`. A hidden area, in a closed call window for one, has
size zero, and the filter drops its pictures before converting them.

# Comic Chat XR — de-xr native port

The native Linux client uses FLTK 1.4.5, OpenGL, Wayland, GIO TLS and OpenXR.
Its reference is Microsoft's latest archived source commit
[`48a1622`](https://github.com/microsoft/comic-chat/commit/48a162249484ab8d116c243e8203b0956d350c09),
using `v2.5-beta-1-modern` and its Art Pack 1. Historical source remains intact.
Upstream and reused de-xr source pins are in `upstream.json`.

The original 31 characters, seven backdrops, separate head/torso poses,
registration points, monochrome/color masks, white outlines, expression indices,
intensities and gestures are read from the actual `.avb` / `.bgb` files.
No replacement character art or scenery is synthesized. Microsoft and other
source/license notices accompany the installed assets.

## Build, run and stage

Use the de-xr patched FLTK toolkit, CMake 3.21+, C++17, OpenGL/EGL, OpenXR,
libpng, Pango/Cairo, Fontconfig, GIO (including its TLS backend), zlib,
nlohmann-json, Wayland/X11 development files, `wayland-scanner`, Python 3 and
Pillow. `COMIC_CHAT_XR=OFF` explicitly omits OpenXR presentation; FLTK remains
the UI toolkit in both configurations. No Qt frontend is used.

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLTK_DIR=/opt/fltk-vr/toolkit/share/fltk
cmake --build build-native -j4
ctest --test-dir build-native --output-on-failure
./build-native/comic-chat-xr --demo
DESTDIR="$PWD/stage-native" cmake --install build-native
```

Build and stage as the regular user. Installation into system paths is a
separate administrative operation. The staged executable retains the selected
FLTK toolkit's library search paths. Staging does not change running sessions.
Art is located through `--art-dir`, `COMIC_CHAT_ART`, a path relative to the
executable, or the configured installation/build directories.

## Desktop and chat

The comic page retains the original character artwork, panel presentation,
head/body registration, black outlines, white speech balloons and left/right
orientation. The native shell exposes Connect, character and backdrop selectors,
member list, character preview, original eight-sector emotion wheel and text
composer. Panels are grouped by measured balloon space rather than a fixed
message count. Long Unicode messages continue across panels without losing
text, and expression changes from the same speaker start a new panel. Normal
balloons use Microsoft's Woodring beta-spline coefficients; thought, shout,
whisper and action modes have distinct presentation. DomeD receives the universal menu; other desktops get a local menu.
DomeD appearance and bitmap fonts update live. Wayland is the native path; X11
is available as a fallback. Bitmap image uploads are cached as OpenGL textures.

**Text view** is a live server/channel output stream: registration and MOTD,
notices, errors, joins/parts, topics, and incoming/outgoing chat. It remains
independent of comic pagination and rendering changes, with up to 2 MiB of
session output retained. Selecting Text view directly from 3D mode opens the
stream without a separate mode-switch step. Comic transcript files still
contain comic history rather than the full server log.

Choose **Connect**, enter a server and nickname, and optionally a channel. Leave the channel blank
to register with the server alone, then use `/join #channel` when ready. TLS with
certificate verification defaults to port 6697. Plain IRC is an explicit option
for servers that require it. DNS, TLS handshakes and socket I/O stay off the
FLTK thread. The app does not connect to any server automatically.

Click or drag the expression wheel; distance from its centre controls intensity.
The centre selects Neutral. The Pose selector also exposes wave, pointing,
shrug and walking poses when supplied by the chosen character. Auto infers basic
expressions from emoticons and laughter. Enter or Send transmits a message.
Commands: `/me`, `/think`, `/nick`, `/quit`.

The original `# Appears as`, `(#G…E…RM…)` annotations, direct messages,
ACTION, NAMES, JOIN/PART/QUIT/NICK/KICK and basic operator MODE changes are
handled. Text-only IRC participants receive stable original characters. Unknown
character names fall back locally; arbitrary asset URLs are not fetched.
Incoming backdrop changes require a known channel operator. Local backdrop
selection remains available. Transcripts preserve expression/pose metadata and
can be saved/reopened using File.

## glTF and the dome

Each selected character pose becomes a genuine closed silhouette mesh:
textured front and rear faces, contour sidewalls and **35 mm depth**. Black and
white source pixels are preserved with unlit materials and nearest sampling.
Character GLBs use metres, stand 1.65 m high, and include the original face and
torso indices. Neutral models for all 31 characters, layered parallax models for all
seven backdrops, and their provenance manifest are generated under
`build-native/gltf-assets` and installed under `share/comic-chat-xr/gltf`.

```sh
./build-native/comic-chat-xr --export-assets build-native/gltf-assets
./build-native/comic-chat-xr --character anna --expression 8 --intensity 1 \
  --export-character build-native/anna-laugh.glb
./build-native/comic-chat-xr --demo --export build-native/chat.glb
./build-native/comic-chat-xr --demo --render build-native/panel.png
./build-native/comic-chat-xr --demo --capture-scene build-native/dome.ppm
```

Expression codes are 1 Happy, 2 Coy, 3 Bored, 4 Scared, 5 Sad, 6 Angry,
7 Shout, 8 Laugh, 9 Neutral, 10 Wave, 11 Point other, 12 Point self,
13 Double point, 14 Shrug, 15 Rear walk, 16 Side walk, 17 Front walk.
Intensity is 0 through 1; absent poses use the original available art.

A normal launch inherits the caller's validated DomeD Wayland socket. A
client launched by the desktop launcher opens in the desktop compositor; a
client launched by the XR launcher opens in the XR compositor. It does not
silently move an inherited XR window into the desktop realm or vice versa.
Explicit commands select a sibling socket belonging to the same compositor:

```sh
./build-native/comic-chat-xr --realm desktop --demo
./build-native/comic-chat-xr --realm xr --demo
./build-native/comic-chat-xr --session-status
```

The desktop entry also offers **Launch on desktop** and **Launch in XR** actions.
DomeD windows use native Wayland, including when an inherited toolkit preference
still names X11. Other desktop sessions retain the normal FLTK fallback.

The application window replaces its comic/text view with a native OpenGL 3D
viewport when DomeD reports a visible XR session and `input_realm=xr`.
Returning to desktop restores the previously selected comic/text view. Unknown
or disconnected session state restores the desktop view. History and unsent
input survive switches. **2D view / 3D view** switches the chat rendering locally
and keeps that choice across session mode changes. **View → Reset 3D camera**
resets drag parallax without changing the selected rendering mode.
The normal GUI never launches a separate OpenXR presenter.

The 3D viewport uses the selected panel's original character poses, cutout
textures and speech balloons. It fits the square composition within the content
rectangle and the active lane's height at DomeD's pull-out distance. The front
plane tracks that distance; depth ratios are preserved so background layers stay
registered at the reference viewpoint. The compositor places the application's
surface, including the viewport, with the window. The application retains the
standard dome curvature, lane spacing and focus outset. Each eye's 3D chat image
is reprojected through the actual cylindrical content rectangle. Controls and
text keep the compositor's original surface mapping; no whole-window flattening
or extra world anchor is applied. Drag inside the viewport to inspect depth parallax.
The selected backdrop is rendered even before the first chat message, in both
desktop and XR views.

XR speech balloons are rasterized at 3× density and use a dedicated linear,
mipmapped sampler; the original character and backdrop textures retain their
pixel-art sampling. Balloon type grows toward 24 logical pixels when the
measured panel has room, retaining the configured font and falling back toward
the original size for dense panels so enlarged messages stay within the existing
balloon band.
Desktop balloon layout and rendering are unchanged. Headset eye resolution and
window texture density still limit the final angular text detail.


The window uses DomeD's squeezed side-by-side transport under the explicit
`org.de_xr.ComicChat.Sbs` identity. Each eye gets a separate off-axis projection;
all controls are duplicated at zero disparity. Rendering and packing use GPU
framebuffers with no CPU readback. Modal dialogs retain the mono application
identity and normal input coordinates. The original desktop composition returns
when switching to desktop mode.

DomeD advertises support through `/api/integrations/comic-chat`; an older running
compositor falls back to a mono 3D view until the updated compositor is started.
The updated XR renderer publishes validated runtime eye poses, which the app
transforms into the window's local coordinates for stereo and head parallax.
If an older XR renderer supplies no eye poses, the baseline defaults to 64 mm.
The session monitor samples outside the UI thread at approximately 50 Hz in XR;
these views are not late-latched at the headset's predicted display time.

Each original backdrop is split into **far (7 m), middle (4.6 m), and near
(3.2 m)** image planes. Editable polygons/ellipses in `backdrop-layers.json`
assign complete sky, landscape, architecture, furniture and foreground
objects to these depths. See [authored backdrop depth](BACKDROP_DEPTH.md) for
the per-scene decisions and semantic regression landmarks. The planes align to the original square composition
at nominal eye height (1.6 m), with overscan for head movement. Their different
depths produce parallax. There is no spherical wrap or rear panorama seam.

Layer compositing reproduces every original backdrop pixel at the reference
viewpoint. Newly exposed pixels behind a foreground region come from nearest
pixels of that same source layer, preventing a second copy of the foreground
from showing through. This is source-pixel extrapolation rather than new painted
scenery. Large head movements can expose that extrapolation; region boundaries
and hidden-area art can be refined in the layer configuration.

The explicit `--xr scene.glb` command remains a standalone diagnostic renderer.
It requires `XR_KHR_opengl_enable`, `XR_MNDX_egl_enable` and `XR_EXTX_overlay`,
uses tracked per-eye views, and aligns to DomeD's shared pull-out anchor.
It is not used by the application window. Pose/message/backdrop updates replace
a self-contained GLB atomically and reload asynchronously in the viewport.

## Validation and current fidelity boundary

Checks exercise 1,581 character/expression/intensity selections, independent
masked-monochrome conversion fixtures, bounded decompression, original wire
annotation round trips, GLB loading/depth, TCP registration and fragmented Unicode,
untrusted TLS rejection, real FLTK connect/send/expression gestures, native
Wayland/universal menus on a private DomeD, and stereo EGL framebuffers through
an isolated OpenXR API trace. A separate real-compositor test verifies desktop
and XR launch origins, explicit sibling socket selection, repeated presentation
mode changes, in-window 3D rendering without presenter processes, lane-height
fitting, live pull-out distance changes in the diagnostic renderer,
and alignment to a translated/rotated shared pull-out anchor. Its XR layer trace confirms
that desktop mode submits no spatial layers. Screenshots and reports stay in `build-native`.
No test restarts or changes the active desktop or XR service.

**This is a working native port, not yet a pixel-identical replacement for the
entire original client.** Avatar placement uses the original `LayoutAvatars`
size/zoom equations and head/body alignment, but some automatic panel composition,
balloon routing and the full original expert-system rules are still
simplified. Native wrapping, Unicode continuation, measured panel grouping and
the original normal-balloon spline basis are implemented. DomeD's configured font intentionally replaces the historical font.
The original Windows renderer has not been run in a controlled transcript
comparison. The native client currently opens one IRC server/channel at a time;
OLE scripting, DCC transfer, legacy IRCX extensions and the full administration
UI have not been ported.

The isolated stereo test validates API use and actual GPU draws, not physical
headset comfort or controller ergonomics. A live desktop-mode check against
WiVRn `v26.9.3-pyrowave.3` produced 92 XR frame cycles with **zero spatial layers**
submitted, even with the headset connected. Parallax quality and physical
controller ergonomics still need a headset-user review. Composition layers put
all DomeD windows above this scene; they do not provide per-pixel depth occlusion
between independent clients. Hand-refined hidden backdrop regions and the
complete original comic-layout expert system remain further work.

### IRC input commands

Input supports `/help`, `/join #channel [key]`, `/part [#channel] [reason]`
(`/leave`), `/msg nick text`, `/notice target text`, `/query nick`, `/nick`,
`/me` (`/action`), `/think`, `/say`, `/topic [#channel] [text]`, `/mode`,
`/names`, `/list`, `/who`, `/whois`, `/whowas`, `/invite nick [#channel]`,
`/kick [#channel] nick [reason]`, `/away [text]`, `/back`, `/raw` (`/quote`),
`/quit [reason]`, `/disconnect`, `/clear`, and `/connect` (`/server`). Command
names are case-insensitive. `//text` sends a literal leading slash. `/query nick`
switches outgoing messages to that user and opens Text view; `/query` returns
to the selected channel. `/msg` sends once without changing the active target.
`/server hostname` opens the connection dialog with that host filled in; choose
port, TLS and credentials there. Raw commands follow IRC parameter syntax.
The client maintains one selected channel and one optional private target;
server replies and messages from all channels remain visible in Text view.

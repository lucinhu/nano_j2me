# Mobile - J2ME (nano_j2me)

!!! note
    Local upstream documentation draft. Intended destination:
    `libretro/docs/docs/library/nano_j2me.md`. Core Downloader availability and
    upstream acceptance have not been established. Notice packaging and final
    candidate validation are tracked in the project's submission kit; complete
    them before publishing this page.

## Background

nano_j2me is a native C99 runtime for J2ME MIDlets. The libretro core embeds the
runtime and opens the game's JAR itself; it needs no separately installed JVM
or helper process. Its target API includes MIDP 2.0 and selected handset APIs.
Compatibility and performance vary by title and host.

The nano_j2me core is developed by nano_j2me contributors.

Runtime implementation code is MIT licensed, with the selected third-party MIT
notices retained. Built-in AMR numeric data are separately Apache-2.0 licensed;
bundled font-derived data are SIL OFL 1.1. The complete package includes these
separate data licenses and notices. The project's provenance records describe
the source replacements and bounded implementation reviews.

- [Project source](https://github.com/lucinhu/nano_j2me)
- [Runtime license](https://github.com/lucinhu/nano_j2me/blob/main/LICENSE)
- [Third-party review and submission status](https://github.com/lucinhu/nano_j2me/blob/main/docs/LIBRETRO_SUBMISSION.md)
- [Local build guide](https://github.com/lucinhu/nano_j2me/blob/main/docs/LIBRETRO.md)

## Requirements and content

No BIOS, firmware, external JVM or system files are required. Load a local
**`.jar`** file whose manifest contains a valid `MIDlet-1` entry. JAD descriptors,
JAD download redirects and selecting another MIDlet from a suite are not
implemented. The JAR must remain available while the content runs.

The core library must match the frontend's OS, architecture and ABI. Android
arm64-v8a requires a 64-bit frontend; armeabi-v7a requires a 32-bit frontend.
Cross-build checks are available for Linux ARM and three Android ABIs; device
runtime compatibility is not established by those checks. macOS is unverified.

## Features

| Feature | Support |
| --- | --- |
| Restart | Yes; reloads the content |
| In-game RecordStore / FileConnection saves | Yes; core-managed files |
| Save states | Basic manual save/load; see limitations below |
| Rewind | Not advertised or validated |
| Runahead | Not advertised or validated |
| Netplay | Not advertised or validated |
| Deterministic states | No claim |
| Core options | Yes, v2 with legacy fallback; content reload required |
| RetroPad | One controller |
| Keyboard | Yes, handset keypad callback |
| Pointer | Yes, touch/mouse canvas input |
| Hardware rendering | No; software RGB565 |
| Cheats / achievements / disc control | No |
| libretro save RAM / exposed memory | No; persistent files are managed by the core |
| Network / Bluetooth / sensors / rumble | Not provided by this adapter |

### Save-state limitations

The VM worker services save/load requests at a safe boundary. Java execution
state, front/back framebuffers and virtual clock are included. Restore validates
a fresh candidate VM before replacing the live VM; incompatible or damaged
states are refused.

States are **architecture, ABI, endianness and build dependent**. Use the same
core build and JAR bytes with matching heap/device/rotation/sound/pointer options.
They are not a portable archival format. Decoded heap PCM and its media
cursor are restored without reopening a codec. Native audio handles and device
queues are cleared, so encoded/native-only playback may stop. RecordStore
files, FileConnection contents and other external resources are not rewound.
A load can therefore combine earlier Java state with later files. A VM unable
to reach a safe point can refuse a request. State size scales with the heap
budget. These basic manual states do not establish determinism or support for
rewind, netplay or runahead.

## Directories

In-game saves use the frontend's save directory:

| Path beneath the save directory | Purpose |
| --- | --- |
| `nano_j2me/<normalized JAR stem>/` | RecordStore files |
| `nano_j2me/<normalized JAR stem>/files/` | FileConnection root |

If no save directory is supplied, the core falls back to the system directory,
then the current directory. The stem stops at the first dot, lowercases ASCII
and replaces runs of non-alphanumeric characters with underscores. Filenames
that normalize identically can share data; use distinct stems or separate save
directories. Frontend save-state files are separate from these in-game files.

## Geometry and timing

| Property | Value |
| --- | --- |
| MIDlet canvas | 240×320 |
| Output at 0° / 180° | 240×320, aspect ratio 3:4 |
| Output at 90° / 270° | 320×240, aspect ratio 4:3 |
| Video format | RGB565 |
| Reported refresh rate | 60 Hz |
| Audio | 44.1 kHz stereo; 735 sample frames per frontend frame |

Rotation occurs inside the core; do not add the same rotation in the frontend.
The handset canvas coordinates remain unchanged. A game's own animation rate
can be below the reported frontend rate. Pause/fast-forward/slow motion affect
virtual time; threaded execution and external resources prevent a deterministic
execution guarantee.

## Core options

All options take effect on **content reload**. Defaults are bold.

| Option (key) | Values | Description |
| --- | --- | --- |
| Device profile (`nano_j2me_device`) | **generic**, nokia6230, sonyericsson-k750, motorola-e398, samsung-sgh-e700, siemens-s65 | handset system properties and key conventions |
| Screen rotation (`nano_j2me_rotation`) | **0**, 90, 180, 270 | rotate output, including reported geometry |
| Java heap (`nano_j2me_heap`) | **auto**, 16, 32, 64, 128 | MB; auto selects host RAM / 16, clamped to 32–128 MB in 8 MB steps; unknown RAM uses 32 MB |
| Font (`nano_j2me_font`) | **auto**, system, builtin | external TrueType font selection, see below |
| Audio output (`nano_j2me_sound`) | **enabled**, disabled | PCM and tone output |
| Touchscreen pointer (`nano_j2me_pointer`) | **enabled**, disabled | canvas pointer events |
| Button A (`nano_j2me_btn_a`) | **FIRE**, any binding below | A key |
| Button B (`nano_j2me_btn_b`) | **SOFT_RIGHT**, any binding below | B key |
| Button X (`nano_j2me_btn_x`) | **SOFT_LEFT**, any binding below | X key |
| Button Y (`nano_j2me_btn_y`) | **CLEAR**, any binding below | Y key |
| Button L (`nano_j2me_btn_l`) | **GAME_A**, any binding below | L key |
| Button R (`nano_j2me_btn_r`) | **GAME_B**, any binding below | R key |
| Button Select (`nano_j2me_btn_select`) | **0**, any binding below | Select key |
| Button Start (`nano_j2me_btn_start`) | **5**, any binding below | Start key |

Each button option accepts `NONE`, `FIRE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `0`–`9`,
`STAR`, `POUND`, `SOFT_LEFT`, `SOFT_RIGHT`, `CLEAR`, `GAME_A`–`GAME_D`, `VOLUP`,
`VOLDOWN` and `POWER`.

## Fonts

With Font set to **auto**, the core tries `nano_j2me/font.ttf` then
`nano_j2me/font.ttc` under the frontend system directory, followed by common OS
font paths. **system** restricts lookup to the frontend system directory;
**builtin** uses bundled bitmap fonts. Reload content after changing the option.

TrueType `glyf` outlines and supported TTC collections are accepted. CFF/OTTO
fonts are not supported. Use a font with Chinese coverage for CJK games; missing
glyphs and unsupported fonts fall back to the bundled faces. The core logs the
selected path and does not redistribute system fonts. Bundled fallback data
retains its separate OFL license even when an external primary font is selected.

States require the same accepted font bytes after reload. Their envelope version
2 rejects the previous version-1 format. Loaded font bytes remain pinned for the
current session, including state restore.

## Controllers

One RetroPad controls the handset. D-pad directions are fixed; other bindings
can be adjusted in Core Options after which the content must be reloaded.

| RetroPad | Default J2ME input |
| --- | --- |
| D-pad | UP / DOWN / LEFT / RIGHT |
| A | FIRE |
| B | SOFT_RIGHT |
| X | SOFT_LEFT |
| Y | CLEAR |
| L | GAME_A |
| R | GAME_B |
| Select | 0 |
| Start | 5 |

The keyboard callback maps `0`–`9` and keypad digits, `*`/`#`, Enter/Space
(FIRE), arrows, Backspace (SOFT_RIGHT), Tab (SOFT_LEFT) and Escape (CLEAR).
Frontend hotkeys can intercept these keys. Pointer input maps touch/mouse
coordinates and presses back through the selected presentation rotation.

## Compatibility

The default build routes built-in WAV, MIDI, tone, MP3 Layer III, AMR narrowband
and supported H.263/MPEG-4 Part 2 decoding through the core. This is not a claim
of complete JSR-135 format/profile support. Features depend on compile-time
switches. AMR validates the complete input before trying the host audio hook
and available system/custom providers; only a complete 8 kHz mono PCM16 result
is accepted. Failure, unsupported format or partial/stalled output falls back
to the built-in decoder using the original input. System PCM can differ from
the built-in decoder. See [system AMR configuration and evidence](https://github.com/lucinhu/nano_j2me/blob/main/docs/SYSTEM_AMR.md).

The native backend defaults to `NJ_ENABLE_SYSTEM_AMR=AUTO` on supported hosted
Android, Apple and Windows configurations. `0` disables that automatic backend
but leaves explicitly installed port/custom providers eligible. Android's
existing Java audio hook retains priority; a VM-less Android CLI falls back
immediately. Linux uses the built-in decoder unless a port/custom provider is
installed. The native service has actual API 21 x86_64 Android app evidence
(nine cases); Apple evidence is API mocks and arm64/x86_64 compile checks.
Windows has SDK fault-path tests and Wine checks, where an actual AMR codec
was unavailable. These are distinct from full core/device validation and do
not establish Apple device or native Windows codec success.

The core uses the JAR's first manifest MIDlet entry, runs one game at a time and
provides no network services. Games requiring online servers, Bluetooth,
sensors or vibration may fail. A successful boot or cross-build does not prove
a title is playable to completion. Refer to project verification records for
specific tested content and environment details; no commercial JARs are supplied
by this page.

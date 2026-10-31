# ytmusic3ds v3.11

YouTube Music-style audio client prototype for the (Old) Nintendo 3DS.
Search YouTube Music from the software keyboard, resolve an Opus/WebM
audio stream, download it, and play it through libopus + ndsp.

## What changed in v3.11

- **Self-update**: Settings -> アップデート checks a JSON manifest
  at the URL in `config.txt` (`update_url=...`). When a newer
  version exists, the app downloads it, verifies size + SHA-256,
  swaps it in place of its own .3dsx (with a .bak rollback), and
  asks for a restart. The check is manual only; nothing is
  fetched at startup. Release assets for hosting: a plain
  `ytmusic3ds.3dsx` plus `version.json`
  ({"version","url","sha256","size"}) - e.g. as GitHub Release
  assets, where `.../releases/latest/download/version.json`
  gives a stable manifest URL.

## What changed in v3.10

- **Texture channel-order fix** (icons AND thumbnails): GPU_RGBA8
  stores texels as A,B,G,R bytes, but the uploads packed R first,
  so the GPU read red as alpha and alpha as red - icons rendered
  cyan and thumbnail colours were rotated. The user's Azahar
  screenshot was colour-sampled to prove it (pure (0,255,255)
  squares). Packing corrected in `icons.c`/`thumb.c` and verified
  with a host simulation of the GPU sampler.

## What changed in v3.9

- **Instant play start**: the HQ jacket and the lyrics are no
  longer fetched before playback. Pressing play goes straight to
  resolving + downloading; both assets load from the playback
  hook while the song downloads (the player screen shows the
  list thumbnail until the HQ art lands).
- **Lyrics reliability**: LRCLIB intermittently returns 503
  ("server busy") - that was behind the "never found" reports.
  Requests now retry on 5xx, try more query shapes (artist-
  narrowed search, cleaned titles), and the lyrics screen shows
  the actual reason when nothing is found (server busy / not
  registered / network error).

## What changed in v3.8

- **Icon fix**: v3.7's tinted icon drawing rendered solid squares
  (the tint path lost the glyph alpha, reproduced in Azahar).
  Icons now bake one texture per UI colour and are drawn with the
  same untinted path as thumbnails.
- **Responsive thumbnail loading**: search/library thumbnails now
  fetch one per main-loop iteration with an on-screen counter, so
  scrolling, selecting and playing all work while they load.

## What changed in v3.7

- **Real icons**: the UI now draws Google's **Material icons** -
  the same icon set the YouTube Music app uses - converted
  offline to tintable 3DS textures (`icons.c`, `icons_data.h`,
  generator in `../tools/png2icons.py`, sources cached in
  `../icon-cache/`). Tab bar, mini player, player controls and
  pills all use them.
- **Colours matched to the app**: #030303 background, #212121
  surfaces (chips/pills a touch lighter), #AAAAAA secondary text,
  **white** progress bars; red remains only as the brand accent.
- **Mini player** on the normal screens, exactly where the app
  has it: above the tab bar, showing the current/last song's
  jacket, title and artist plus a play/pause button, with the
  **thin progress bar between the mini player and the tabs**.
  Tap the mini player to open the player screen; the button
  pauses/resumes live playback or replays the song when idle.
- **Jacket margins fixed**: thumbnail letterbox bars (solid or
  blurred) are detected and trimmed before the square crop, so
  artwork fills the frame like in the app (validated on synthetic
  test images on the host).

## What changed in v3.6

- **Player screen** (modelled on the app's now-playing screen):
  selecting a song shows the big jacket artwork on the top screen
  (hqdefault thumbnail at 128px) and controls on the bottom -
  title, Like / Bad / Lyrics pills, progress bar with times, and
  shuffle / previous / play-pause / next / repeat buttons, all
  touchable. **Y** toggles between the player screen and the list
  screens at any time.
- **Real pause** (ndsp channel pause), **prev/next**, **auto-advance**
  to the next song when one finishes, **shuffle** and
  **repeat (off / all / one)**, persisted in config.txt.
- **Like** toggles the favourite (Library). **Bad** records the
  song in `dislikes.txt` and skips to the next song.
- **Lyrics** are fetched from the free LRCLIB API before playback
  starts (best effort; shown on the top screen, D-pad scroll).
- v3.5 data (favourites/config) is imported from the old folder
  on first run.

## What changed in v3.5

- **Layout modelled on the YouTube Music app** (per the user's
  screenshot): top screen is the song list - logo header, chips
  row, section title, big rows with **real thumbnails** fetched
  from i.ytimg.com and decoded with libjpeg-turbo - and the bottom
  screen shows the now-playing song above a 4-tab bar
  (Home / Search / Library / Settings). Tabs are tappable on the
  touch screen; L/R switch too.
- **Library = favourites on the SD card**
  (`sdmc:/3ds/ytmusic3ds-v3.5/favorites.txt`, up to 40 songs).
  X toggles the selected song; favourites play like search results,
  including mid-playback switching.
- **Settings tab**: sound quality (low/standard/high - this sets
  the itag preference order in both resolvers) and thumbnails
  on/off, persisted to `config.txt` next to the favourites file.
- Playback engine unchanged from v3.3/v3.4.

## What changed in v3.4

- **YouTube Music app look**: near-black background, red brand
  accent, play-badge logo header, chunky song rows (thumbnail block,
  title, artist line) with windowed scrolling and a highlighted
  selection, an equalizer mark on the playing row, thin red progress
  bars, and a pill-shaped play button on the details pane.
- **Switch songs mid-playback**: while a song downloads or plays,
  the top-screen list stays browsable - move with the D-pad and
  press A on another song to stop the current track and play that
  one right away. B still stops and returns to the list.
- The playback engine (VISIONOS direct route, Range downloader,
  Invidious fallback) is unchanged from v3.3, whose full download +
  playback was verified on real hardware on 2026-10-31.

## What changed in v3.3

- **Root cause of the v3.2 mid-download 403 found**: the v3 recipe's
  stream tokens only authorize a ~512 KiB prefix of the file (a
  `vprv=1` preview window). The v3.2 hardware download stopped at
  exactly 512 KiB, and VM probes showed every downloader shape hits
  the same wall - one persistent connection, fresh connections, any
  chunk size, the `&range=` parameter form - and a fresh token's
  first request past the window is refused too, so re-resolution
  could never resume past it. The IP-binding hypothesis was tested
  and rejected; the limit is a property of the token itself.
- **Direct resolution now leads with the VISIONOS client context**
  (FourthTube's alternative `/player` shape, used verbatim). Its
  tokens download completely: VM-verified full files for 3 songs x
  itags 249/250/251, and the hardware-test song's file also decodes
  cleanly end-to-end through this project's own WebM demuxer +
  libopus (9,218 packets, 0 errors, 184.35 s). The old recipe
  context remains as a secondary direct attempt.
- **One persistent connection per download**: all routes now share a
  single downloader that runs every 256 KiB Range chunk over one
  curl handle (HTTP/1.1, connection reused; the post-redirect URL is
  adopted like FourthTube does). This replaces both the per-chunk
  reconnect of v3.2 and the fallback's plain GET (no Range, no
  resume, no HTTP code in its error - the v3.2 fallback did run, and
  died in that downloader).
- **Visible, per-instance fallback with a diagnosable error**: if a
  download fails, the UI walks the built-in Invidious instances one
  by one, showing `Route: Invidious via <host>` for each attempt.
  A final failure prints a compact attempt log (route, bytes
  reached, HTTP code - e.g. `dir@512KB H403 nadeko res H403`), so
  one photo of the bottom screen is enough to diagnose.

## What changed in v3.2

- **Repackaging / crash-investigation build** (no feature changes vs v3):
  the v3 hardware test crashed in the loader with "undefined
  instruction". Binary audit of v1/v2/v3 found **no** ARMv7-only
  instructions anywhere (see `../report.md` section 5), and the fault
  offset holds a plain branch in the built binary - the executed file
  on the SD card evidently differed from the built artifact (all three
  versions shipped under the identical filename `ytmusic3ds.3dsx`,
  which made mix-ups likely). v3.2 therefore ships with a
  version-identifiable name and title: file `ytmusic3ds-v3.2.3dsx`
  (same name as its folder, so the HBL pairs it with its SMDH),
  SMDH title `ytmusic3ds v3.2`, plus size/SHA-256 recorded in
  `../report.md` so a copied file can be verified before launching.

## What changed in v3

- **Direct stream resolution first**: an InnerTube `/player` request
  whose shape follows the FourthTube recipe (ANDROID client context
  with iPhone device fields, fresh `visitorData`, `signatureTimestamp`
  `"0"`) makes YouTube return plain, unsigned stream URLs - no
  Invidious instance needed on the happy path.
- **Invidious fallback kept**: if the direct resolution fails, the v2
  Invidious route is tried; if a direct *download* fails part-way, the
  UI retries once through Invidious. The playback screen shows which
  route is in use (`Route: direct YouTube` / `Invidious ...`).
- **Range-chunked downloader** for the direct route (256 KiB chunks,
  resume, URL re-issue on mid-download HTTP 403, max 5 re-resolutions).
  A plain GET is refused by googlevideo, and oversized range requests
  are refused for some formats - both measured, see `../report.md`.

Everything else is as in v2: swkbd search (up to 20 results), the
hand-written WebM/EBML demuxer, libopus decode, ndsp output, B stops,
32 MB download cap, no New-3DS-only features.

Build: `source ../env.sh && make` -> `ytmusic3ds-v3.11.3dsx`.
Install/test steps: see the v3.11 section of `../report.md`.
First run on hardware requires a one-time DSP firmware dump (DSP1).

## Credits / license note

The direct stream-resolution approach was **informed by the published
sources** of two GPLv3 projects:

- **ThirdTube** by **windows-server-2003**
  (https://github.com/windows-server-2003/ThirdTube)
- **FourthTube** by **erievs**
  (https://github.com/erievs/FourthTube)

ytmusic3ds is an **independent reimplementation** in C (libctru +
curl + jansson) based on an analysis of how those clients talk to
YouTube's InnerTube API; no FourthTube/ThirdTube code is copied into
this project. The full analysis is in
`../fourthtube-analysis/ANALYSIS.md`. If code from those projects is
ever incorporated directly, this project becomes a GPLv3 derivative
and must then be distributed with the GPLv3 license text, copyright
notices, and corresponding source.

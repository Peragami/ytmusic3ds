# Azahar test environment (on muze's VM)

Setup for checking ytmusic3ds builds on muze's side before handing them
over: boot, screens, colours, key/touch operation — everything that does
not need the network.

**Networking does not work in this environment.** Muze's VM can only
reach the internet through an authenticated egress proxy, and the
emulator's socket layer cannot use it (direct connections are blocked;
a proxychains experiment got its CONNECTs denied by the proxy). Search,
thumbnail downloads and playback therefore still have to be verified on
real hardware or on the home-PC Azahar. Visual/functional checks are
done here.

## Restore after an environment reset

```sh
bash tools/azahar/setup.sh   # from a clone of this repo, or run the copy in ~/workspace/ytmusic3ds-git
```

The script reinstalls the apt packages (they do not survive a VM
replacement) and downloads/extracts Azahar 2126.2 into
`~/workspace/tools/azahar/` if missing. The extracted AppImage itself
lives under `~`, so it normally survives; the packages are the part
that vanishes.

## Run

```sh
# virtual display (skip if already running)
(Xvfb :99 -screen 0 1280x1024x24 &)

cd ~/workspace/tools/azahar
DISPLAY=:99 nohup ./squashfs-root/AppRun /path/to/ytmusic3ds-vX.XX.3dsx > /tmp/azahar.log 2>&1 &

# screenshot
ffmpeg -loglevel error -f x11grab -video_size 1280x1024 -i :99 -frames:v 1 shots/check.png -y
```

Stop it afterwards (VM RAM is tight):
`pkill -f 'AppRun[.]wrapped'` — write the pattern with `[.]` so the
pkill does not match your own shell's command line.

## Driving it (xdotool, DISPLAY=:99)

- There is no window manager, so `windowactivate` fails. Find the main
  window with `xdotool search --name "Azahar 2126"` and use
  `xdotool windowfocus --sync <id>`.
- **Hold every tap and key for ~150 ms** (`mousedown; sleep 0.15;
  mouseup`, `keydown; sleep 0.12; keyup`). Instant clicks are shorter
  than the emulator's 60 fps input polling and get dropped.
- Default key mapping (see `[Controls]` in
  `~/.config/azahar-emu/qt-config.ini`): A=a, B=s, L=q, R=w,
  D-Pad up=t left=f down=g right=h, START=m, SELECT=n.
- Layout at 1280x1024: window at (214,231), 853x532. Top screen
  roughly x 440-840, y 255-480; bottom screen x 480-800, y 480-710.
- Flick keyboard key centres: あ (575,545), か (638,545), さ (702,545);
  search button (765,700). Tab bar: Home (520,690), Search (600,690),
  Library (680,690), Settings (760,690).

## Notes

- Azahar's virtual SD card is `~/.local/share/azahar-emu/sdmc/` — the
  app's config/favourites land there, separate from real hardware.
- First finding from this setup (2026-10-31): the in-app version label
  in ui.c still said "v3.16" in the v3.17–v3.19 builds (APP_VER itself
  was correct). To be fixed by tying the label to APP_VER.

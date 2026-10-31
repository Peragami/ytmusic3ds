# Azahar テスト環境（むぜのVM内）

ytmusic3ds の画面・起動・操作をむぜ側で先行確認するための Azahar 2126.2。
通信（検索・再生）はこの環境では不可（VMの出口が認証プロキシ専用で、エミュ側ソケットが通れない。proxychains 実験も CONNECT denied で不可だった）。通信系は実機/自宅Azaharで確認する。

## 起動手順
1. Xvfb: `(Xvfb :99 -screen 0 1280x1024x24 &)`（既に動いていれば不要）
2. `cd ~/workspace/tools/azahar && DISPLAY=:99 nohup ./squashfs-root/AppRun <対象.3dsx> > /tmp/azahar.log 2>&1 &`
3. 足りない共有ライブラリが出たら: libopengl0 / libegl1（apt導入済みのはず。deb直抜きは libs/ にもある）
4. スクショ: `ffmpeg -loglevel error -f x11grab -video_size 1280x1024 -i :99 -frames:v 1 shots/xxx.png -y`

## 入力（xdotool、DISPLAY=:99）
- メインウィンドウは `xdotool search --name "Azahar 2126"` で探し `windowfocus --sync <id>`（WMが無いので windowactivate は効かない）
- **タップもキーも約150msホールドする**（mousedown→sleep 0.15→mouseup / keydown→sleep 0.12→keyup）。一瞬だと60fpsポーリングに拾われない
- キー割当（既定）: A=a, B=s, X=d?, Y=?, L=q, R=w, 十字キー 上=t 左=f 下=g 右=h, START=m, SELECT=n（詳細は ~/.config/azahar-emu/qt-config.ini の [Controls]）
- 画面配置（1280x1024時）: ウィンドウ (214,231) 853x532。上画面 x≈440-840 y≈255-480、下画面 x≈480-800 y≈480-710
- フリックキーボードのキー中心: あ(575,545) か(638,545) さ(702,545)、検索ボタン(765,700)、タブバー: ホーム(520,690) 検索(600,690) ライブラリ(680,690) 設定(760,690)

## 注意
- Azahar の仮想SD: ~/.local/share/azahar-emu/sdmc/（アプリの config/favorites はここに入る。実機とは別物）
- 終了: `pkill -f 'AppRun[.]wrapped'`（パターンは自分のコマンドラインにマッチしない形で書くこと。通常の 'AppRun.wrapped' だと自分のシェルも巻き込む）
- RAMが逼迫しがちなVMなので、テストが終わったら Azahar は落とす


> ⚠️ Launch Azahar with cwd = this directory (~/workspace/tools/azahar). Azahar switches to portable mode when a `user/` folder exists in the launch cwd (e.g. $HOME), silently using a fresh SD card at ~/user/sdmc with none of the seeded data - this once faked a "favourites import is broken" bug during v3.22 testing.

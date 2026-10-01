#!/bin/sh
# Full regression M1..M6: engines, WS protocol, auth, POST, HLS, UI smoke.
set -e
cd "$(dirname "$0")/.."
export QT_QPA_PLATFORM=offscreen NEATDM_WS_PORT=10008
ND=/tmp/ndmtest
pkill -9 -x neatdm 2>/dev/null || true

mkdir -p $ND
[ -f $ND/testfile.bin ] || head -c 31457280 /dev/urandom > $ND/testfile.bin
md5sum $ND/testfile.bin | cut -d' ' -f1 > $ND/orig.md5
head -c 5242880 $ND/testfile.bin > $ND/small.bin
md5sum $ND/small.bin | cut -d' ' -f1 > $ND/small.md5

python3 tests/ensure_servers.py >/dev/null 2>&1 &
SRV=$!
sleep 1.5

PASS=0; FAIL=0
ok() { echo "PASS $1"; PASS=$((PASS+1)); }
bad() { echo "FAIL $1"; FAIL=$((FAIL+1)); }
isovars() { printf 'XDG_CONFIG_HOME=%s/.config XDG_DATA_HOME=%s/.local/share XDG_CACHE_HOME=%s/.cache' "$1" "$1" "$1"; }
apprun() { # name home timeout args...
  name=$1; home=$2; tmo=$3; shift 3
  rm -rf "$home"; mkdir -p "$home"
  HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share \
    XDG_CACHE_HOME=$home/.cache timeout $tmo ./build/neatdm "$@" >/tmp/ra_$name.log 2>&1
}
EXP=$(cat $ND/orig.md5); SEXP=$(cat $ND/small.md5)
md5ok() { md5sum "$home"/Downloads/*/* 2>/dev/null | grep -v neatpart | grep -q "$1"; }

# 1. engine unit (pause+resume+range)
rm -rf /tmp/neatdm-test
./build/neatdm-tests "http://127.0.0.1:8766/file.bin" "$EXP" >/dev/null 2>&1 && ok engine-unit || bad engine-unit

# 2. multi-connection / redirect / chunked / throttle
apprun multi /tmp/ra1 120 --wait-downloads --lifetime 110000 http://127.0.0.1:8766/file.bin && home=/tmp/ra1 md5ok $EXP && ok multi || bad multi
apprun redir /tmp/ra2 120 --wait-downloads --lifetime 110000 http://127.0.0.1:8766/redir && home=/tmp/ra2 md5ok $EXP && ok redir || bad redir
apprun chunked /tmp/ra3 120 --wait-downloads --lifetime 110000 http://127.0.0.1:8766/chunked && home=/tmp/ra3 md5ok $EXP && ok chunked || bad chunked
T0=$(date +%s.%N)
apprun throttle /tmp/ra4 120 --wait-downloads --lifetime 110000 --limit-kbps 512 http://127.0.0.1:8767/small.bin
T1=$(date +%s.%N); DUR=$(echo "$T1 $T0" | awk '{printf "%d", ($1-$2)*10}')
home=/tmp/ra4; md5ok $SEXP && [ $DUR -ge 80 ] && ok throttle || bad throttle

# 3. auth basic/digest (preseed credential)
for MODE in basic digest; do
  home=/tmp/ra_auth_$MODE; rm -rf $home; mkdir -p $home/.local/share/neatdm/NeatDM
  python3 -c "
import sqlite3
c=sqlite3.connect('$home/.local/share/neatdm/NeatDM/NeatDB.db')
c.execute('CREATE TABLE IF NOT EXISTS auths (id INTEGER PRIMARY KEY AUTOINCREMENT, target TEXT, protocol TEXT, user TEXT, pass TEXT)')
c.execute(\"INSERT INTO auths(target,protocol,user,pass) VALUES('127.0.0.1','http','alice','secret123')\")
c.commit()"
  HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share timeout 120 ./build/neatdm --wait-downloads --lifetime 110000 http://127.0.0.1:8766/auth-$MODE >/dev/null 2>&1 \
    && md5ok $EXP && ok auth-$MODE || bad auth-$MODE
done

# 4. WS: fake-extension GET + POST replay + HLS x2
wstest() { # name url fname mode
  home=/tmp/ra_ws_$1; rm -rf $home; mkdir -p $home
  HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share ./build/neatdm --lifetime 45000 >/dev/null 2>&1 &
  W=$!; sleep 2
  python3 tests/fake_ext.py "$2" "$3" "$4" >/dev/null 2>&1
  sleep 10
  if [ "$4" = hls ]; then RES=$(md5sum $home/Downloads/*/*.ts 2>/dev/null | grep "$EXP"); else RES=$(md5sum $home/Downloads/*/* 2>/dev/null | grep -v neatpart | grep "$EXP"); fi
  kill $W 2>/dev/null
  [ -n "$RES" ] && ok ws-$1 || bad ws-$1
}
wstest get    http://127.0.0.1:8766/file.bin           ext-get.bin    ""
wstest post   http://127.0.0.1:8766/file.bin           ext-post.bin   post
wstest hls    http://127.0.0.1:8766/hls/playlist.m3u8  video-pl       hls
wstest master http://127.0.0.1:8766/hls/master.m3u8    video-mst      hls

# 5. kill -9 + resume
home=/tmp/ra_kill; rm -rf $home; mkdir -p $home
HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share ./build/neatdm --wait-downloads --lifetime 60000 http://127.0.0.1:8766/file.bin >/dev/null 2>&1 &
K=$!; sleep 1.1; kill -9 $K 2>/dev/null; wait $K 2>/dev/null || true
HOME=$home XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share timeout 120 ./build/neatdm --wait-downloads --lifetime 110000 http://127.0.0.1:8766/file.bin >/dev/null 2>&1 \
  && md5ok $EXP && ok kill-resume || bad kill-resume

# 6. UI smoke
rm -rf /tmp/ra_ui; mkdir -p /tmp/ra_ui
HOME=/tmp/ra_ui XDG_CONFIG_HOME=/tmp/ra_ui/.config XDG_DATA_HOME=/tmp/ra_ui/.local/share \
  timeout 60 ./build/neatdm --selftest-ui >/tmp/ra_ui.log 2>&1 && ok ui-smoke || bad ui-smoke

# 7. proxy semantics
rm -rf /tmp/ra_px; mkdir -p /tmp/ra_px
HOME=/tmp/ra_px XDG_CONFIG_HOME=/tmp/ra_px/.config XDG_DATA_HOME=/tmp/ra_px/.local/share \
  timeout 60 ./build/neatdm-proxy-tests >/tmp/ra_px.log 2>&1 && ok proxy-semantics || bad proxy-semantics

kill $SRV 2>/dev/null || true
pkill -9 -x neatdm 2>/dev/null || true
echo "REGRESSION: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]

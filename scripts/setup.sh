#!/usr/bin/env bash
# ibus-unikey — one-shot installer + auto setup
# Ubuntu/Debian (tested 22.04/24.04/26.04). Idempotent, có thể chạy lại nhiều lần.
# Usage:
#   ./scripts/setup.sh                 # build + install + setup (hỏi sudo khi cần)
#   ./scripts/setup.sh --build-only    # chỉ build
#   ./scripts/setup.sh --setup-only    # chỉ setup ibus/gsettings (không build)
#   ./scripts/setup.sh --uninstall     # gỡ preload khỏi ibus
#   ./scripts/setup.sh -y              # không hỏi y/n
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
PREFIX="${PREFIX:-/usr}"
LIBEXECDIR="${LIBEXECDIR:-/usr/lib/ibus}"
GSCHEMA_ID="org.freedesktop.ibus.engine.unikey"

# ── flags ──
DO_BUILD=1
DO_SETUP=1
DO_UNINSTALL=0
ASSUME_YES=0
for a in "$@"; do case "$a" in
  --build-only) DO_SETUP=0 ;;
  --setup-only) DO_BUILD=0 ;;
  --uninstall)  DO_UNINSTALL=1; DO_BUILD=0 ;;
  -y|--yes)     ASSUME_YES=1 ;;
  -h|--help)    sed -n '2,12p' "$0"; exit 0 ;;
  *) echo "Unknown arg: $a (try --help)"; exit 1 ;;
esac; done

# ── helpers ──
info(){ echo -e "\033[1;34m==>\033[0m $*"; }
ok(){   echo -e "\033[1;32m ✓ \033[0m $*"; }
warn(){ echo -e "\033[1;33m !  \033[0m $*"; }
die(){  echo -e "\033[1;31m ✗ \033[0m $*" >&2; exit 1; }
have(){ command -v "$1" >/dev/null 2>&1; }
ask(){
  [[ $ASSUME_YES -eq 1 ]] && return 0
  read -rp "$1 [Y/n] " ans; [[ "${ans:-Y}" =~ ^[Yy]$|^$ ]]
}
need_sudo(){
  if [[ $EUID -ne 0 ]] && ! sudo -n true 2>/dev/null; then
    echo "Cần sudo cho bước này."
  fi
}
run_sudo(){
  if [[ $EUID -eq 0 ]]; then "$@"; else sudo "$@"; fi
}

detect_os(){
  if [[ -f /etc/os-release ]]; then . /etc/os-release; echo "$ID $VERSION_ID"; else echo "unknown"; fi
}

ensure_deps(){
  local pkgs=(cmake g++ make pkg-config libibus-1.0-dev libgtk-3-dev gettext)
  local missing=()
  for p in "${pkgs[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
  if [[ ${#missing[@]} -eq 0 ]]; then ok "Dependencies đủ."; return 0; fi
  info "Cài dependencies: ${missing[*]}"
  need_sudo
  if have apt-get; then
    run_sudo apt-get update -qq
    run_sudo apt-get install -y "${missing[@]}"
  elif have apt; then
    run_sudo apt update -qq
    run_sudo apt install -y "${missing[@]}"
  else
    die "Không tìm thấy apt. Hãy cài thủ công: ${missing[*]}"
  fi
  ok "Đã cài dependencies."
}

do_build(){
  info "Build ibus-unikey (${PREFIX}, ${LIBEXECDIR})..."
  ensure_deps
  # build dir trong repo dễ bị root chiếm sau lần sudo trước -> tự fix
  if [[ -d "$BUILD_DIR" ]] && ! touch "$BUILD_DIR/.writable" 2>/dev/null; then
    warn "Thư mục $BUILD_DIR không ghi được (do root). Đang fix quyền..."
    run_sudo chown -R "$USER":"$USER" "$BUILD_DIR" 2>/dev/null || {
      warn "Không chown được, xóa và tạo lại $BUILD_DIR"
      run_sudo rm -rf "$BUILD_DIR"
      mkdir -p "$BUILD_DIR"
    }
  else
    mkdir -p "$BUILD_DIR"
    rm -f "$BUILD_DIR/.writable" 2>/dev/null || true
  fi
  # cmake configure
  cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLIBEXECDIR="$LIBEXECDIR" \
        -B "$BUILD_DIR" "$REPO_DIR"
  if ! cmake --build "$BUILD_DIR" -j"$(nproc)"; then
    warn "Build thất bại — thử clean build..."
    run_sudo rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" \
          -DCMAKE_BUILD_TYPE=Release \
          -DLIBEXECDIR="$LIBEXECDIR" \
          -B "$BUILD_DIR" "$REPO_DIR"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
  fi
  info "Install..."
  need_sudo
  run_sudo cmake --install "$BUILD_DIR"
  # compile schemas (cả /usr/share và /usr/local)
  for d in /usr/share/glib-2.0/schemas /usr/local/share/glib-2.0/schemas; do
    [[ -d "$d" ]] && run_sudo glib-compile-schemas "$d" 2>/dev/null || true
  done
  ok "Build & install xong."
  # verify
  if [[ -x "$LIBEXECDIR/ibus-engine-unikey" ]]; then
    ok "Engine: $LIBEXECDIR/ibus-engine-unikey"
  else
    warn "Không thấy $LIBEXECDIR/ibus-engine-unikey — kiểm tra PREFIX/LIBEXECDIR."
  fi
  if [[ -f /usr/share/ibus/component/unikey.xml ]] || [[ -f /usr/local/share/ibus/component/unikey.xml ]]; then
    ok "Component unikey.xml đã cài."
  fi
}

ibus_running(){
  pgrep -x ibus-daemon >/dev/null 2>&1 || pgrep -x ibus-x11 >/dev/null 2>&1
}

ensure_ibus_daemon(){
  if ibus_running; then ok "ibus-daemon đang chạy."; return 0; fi
  info "Khởi ibus-daemon..."
  if have ibus-daemon; then
    # --daemonize --xim --address etc. để tương thích X/Wayland
    ibus-daemon -d -x -r 2>/dev/null || ibus-daemon -d 2>/dev/null || true
    sleep 1
    ibus_running && ok "ibus-daemon đã khởi." || warn "Không khởi được ibus-daemon tự động — thử 'ibus-daemon -d -x &' thủ công."
  else
    warn "Chưa có ibus-daemon. Cài gói 'ibus' trước."
  fi
}

gsettings_set_if_needed(){
  local schema="$1" key="$2" value="$3" type="$4"
  # type: s/b/as
  local cur
  cur="$(gsettings get "$schema" "$key" 2>/dev/null || echo "__missing__")"
  if [[ "$cur" == "$value" ]]; then return 0; fi
  if [[ "$type" == "s" ]]; then
    # value already quoted like "'Unikey'" or "'telex'"
    gsettings set "$schema" "$key" "$value"
  elif [[ "$type" == "b" ]]; then
    gsettings set "$schema" "$key" "$value"
  elif [[ "$type" == "as" ]]; then
    gsettings set "$schema" "$key" "$value"
  fi
}

add_unikey_to_preload(){
  # preload-engines là list string, vd ['Unikey']
  local cur
  cur="$(gsettings get org.freedesktop.ibus.general preload-engines 2>/dev/null || echo "@as []")"
  if echo "$cur" | grep -q "Unikey"; then
    ok "Unikey đã có trong preload-engines: $cur"
  else
    info "Thêm Unikey vào preload-engines..."
    # merge giữ nguyên các engine có sẵn
    if [[ "$cur" == "@as []" || "$cur" == "[]" ]]; then
      gsettings set org.freedesktop.ibus.general preload-engines "['Unikey']"
    else
      # cur dạng "['xkb:us::eng', 'Unikey']" — append nếu chưa có
      # đơn giản: python merge để không lỗi quote
      python3 - "$cur" <<'PY'
import ast, subprocess, sys
cur = sys.argv[1]
try:
    lst = ast.literal_eval(cur)
except: lst = []
if "Unikey" not in lst:
    lst.append("Unikey")
# gsettings expects GLib variant string
import json
# build gsettings array literal
lit = "['" + "', '".join(lst) + "']" if lst else "['Unikey']"
print(lit)
PY
      local new
      new="$(python3 - "$cur" <<'PY'
import ast, sys
cur=sys.argv[1]
try: lst=ast.literal_eval(cur)
except: lst=[]
if "Unikey" not in lst: lst.append("Unikey")
print("['" + "', '".join(lst) + "']" if lst else "['Unikey']")
PY
)"
      gsettings set org.freedesktop.ibus.general preload-engines "$new"
    fi
    ok "Đã thêm Unikey: $(gsettings get org.freedesktop.ibus.general preload-engines)"
  fi
}

setup_gsettings(){
  info "Setup GSettings cho Unikey..."
  # defaults tối ưu theo yêu cầu: không gạch chân + hotkey mượt
  gsettings set "$GSCHEMA_ID" show-preedit-underline false  2>/dev/null || warn "Không set được show-preedit-underline (schema chưa compile?)"
  gsettings set "$GSCHEMA_ID" viet-mode true                 2>/dev/null || true
  gsettings set "$GSCHEMA_ID" toggle-hotkey "'all'"          2>/dev/null || true
  # giữ nguyên input-method nếu user đã chọn, mặc định telex
  local im
  im="$(gsettings get "$GSCHEMA_ID" input-method 2>/dev/null || echo "'telex'")"
  if [[ "$im" == "@s "* || -z "$im" ]]; then gsettings set "$GSCHEMA_ID" input-method "'telex'"; fi
  # các key khác giữ default hợp lý
  gsettings set "$GSCHEMA_ID" spell-check true           2>/dev/null || true
  gsettings set "$GSCHEMA_ID" auto-restore-non-vn true   2>/dev/null || true
  gsettings set "$GSCHEMA_ID" free-marking true          2>/dev/null || true

  ok "Unikey GSettings:"
  gsettings get "$GSCHEMA_ID" input-method           2>/dev/null | sed 's/^/  input-method: /'
  gsettings get "$GSCHEMA_ID" show-preedit-underline 2>/dev/null | sed 's/^/  underline: /'
  gsettings get "$GSCHEMA_ID" viet-mode              2>/dev/null | sed 's/^/  viet-mode: /'
  gsettings get "$GSCHEMA_ID" toggle-hotkey          2>/dev/null | sed 's/^/  toggle-hotkey: /'
}

ensure_im_env(){
  # Đảm bảo ibus là IM mặc định cho GTK/QT/X
  local profile_add=0
  # im-config nếu có thì set ibus
  if have im-config; then
    local cur
    cur="$(im-config -l 2>/dev/null | head -1 || true)"
    # im-config cần chọn ibus cho user
    if ! grep -q "ibus" <<<"$cur" 2>/dev/null; then
      info "Đặt ibus làm input method mặc định (im-config)..."
      # im-config -n ibus yêu cầu tương tác; thử non-interactive
      echo "ibus" > "$HOME/.xinputrc" 2>/dev/null || true
      # also set via im-config if available non-interactively
      im-config -n ibus 2>/dev/null || true
      ok "~/.xinputrc -> ibus"
    else
      ok "im-config đã là ibus."
    fi
  fi
  # env vars cho session hiện tại
  export GTK_IM_MODULE=ibus
  export QT_IM_MODULE=ibus
  export XMODIFIERS=@im=ibus
  # gợi ý thêm vào ~/.profile nếu chưa có
  local rc="$HOME/.profile"
  if ! grep -q "GTK_IM_MODULE=ibus" "$rc" 2>/dev/null; then
    if ask "Thêm GTK_IM_MODULE/QT_IM_MODULE/XMODIFIERS vào ~/.profile?"; then
      {
        echo ""
        echo "# ibus-unikey (added by setup.sh)"
        echo "export GTK_IM_MODULE=ibus"
        echo "export QT_IM_MODULE=ibus"
        echo "export XMODIFIERS=@im=ibus"
      } >> "$rc"
      ok "Đã thêm vào $rc (cần relogin để có hiệu lực toàn hệ thống)."
    else
      warn "Bỏ qua ghi ~/.profile — env chỉ có hiệu lực trong terminal hiện tại."
    fi
  else
    ok "~/.profile đã có ibus env."
  fi
}

do_setup(){
  info "Setup IBus + Unikey..."
  ensure_ibus_daemon
  add_unikey_to_preload
  setup_gsettings
  ensure_im_env
  info "Restart IBus..."
  if have ibus; then
    ibus restart 2>/dev/null || ibus-daemon -d -x -r 2>/dev/null || true
    sleep 1
  fi
  # verify engine appears
  if ibus list-engine 2>/dev/null | grep -q "Unikey"; then
    ok "ibus list-engine thấy Unikey."
  else
    warn "Chưa thấy Unikey trong 'ibus list-engine' — thử 'ibus restart' rồi kiểm tra lại."
  fi
  echo ""
  ok "Setup xong!"
  echo ""
  echo "  • Gõ thử: Super+Space để chọn Unikey, hoặc 'ibus engine Unikey'"
  echo "  • Đổi EN<->VI: Shift+Shift (gõ nhanh 2 lần Shift), Shift+Space, hoặc Ctrl+Space"
  echo "    (click VI/EN trên panel cũng được). Đổi hotkey:"
  echo "      gsettings set $GSCHEMA_ID toggle-hotkey \"'shift-shift'\"  # chỉ Shift+Shift"
  echo "      gsettings set $GSCHEMA_ID toggle-hotkey \"'all'\"          # tất cả"
  echo "  • Tắt/bật gạch chân:"
  echo "      gsettings set $GSCHEMA_ID show-preedit-underline true/false"
  echo "  • Nếu chưa hiện Unikey trong Settings → Region & Language, logout/login lại."
}

do_uninstall(){
  info "Gỡ Unikey khỏi preload-engines..."
  local cur new
  cur="$(gsettings get org.freedesktop.ibus.general preload-engines 2>/dev/null || echo "[]")"
  new="$(python3 - "$cur" <<'PY'
import ast, sys
cur=sys.argv[1]
try: lst=ast.literal_eval(cur)
except: lst=[]
lst=[x for x in lst if x!="Unikey"]
print("['" + "', '".join(lst) + "']" if lst else "@as []")
PY
)"
  gsettings set org.freedesktop.ibus.general preload-engines "$new" 2>/dev/null || true
  ok "preload-engines: $(gsettings get org.freedesktop.ibus.general preload-engines 2>/dev/null)"
  warn "Chưa gỡ file nhị phân — chạy 'sudo cmake --build $BUILD_DIR --target uninstall' hoặc 'sudo ninja uninstall' nếu cần."
}

main(){
  echo "ibus-unikey setup — $(detect_os) — $REPO_DIR"
  echo ""
  if [[ $DO_UNINSTALL -eq 1 ]]; then do_uninstall; exit 0; fi
  if [[ $DO_BUILD -eq 1 ]]; then do_build; fi
  if [[ $DO_SETUP -eq 1 ]]; then do_setup; fi
}

main "$@"

#!/usr/bin/env bash
set -euo pipefail

readonly ARECA_REPO="xhkzeroone/ibus-areca"
readonly ARECA_PACKAGE="ibus-areca"
ARECA_TMP_DIR=""
ARECA_REBOOT_REQUIRED=0

log() { printf '[ibus-areca] %s\n' "$*"; }
die() { printf '[ibus-areca] Lỗi: %s\n' "$*" >&2; exit 1; }
cleanup() { [[ -z "$ARECA_TMP_DIR" || ! -d "$ARECA_TMP_DIR" ]] || rm -rf -- "$ARECA_TMP_DIR"; }
trap cleanup EXIT

ask_yes_no() {
  local prompt="$1" answer
  [[ -r /dev/tty ]] || return 1
  while true; do
    printf '%s [y/N] ' "$prompt" > /dev/tty
    IFS= read -r answer < /dev/tty || return 1
    case "${answer,,}" in
      y|yes|c|co|có) return 0 ;;
      ''|n|no|k|khong|không) return 1 ;;
      *) printf 'Vui lòng nhập y hoặc n.\n' > /dev/tty ;;
    esac
  done
}

run_root() {
  if [[ $EUID -eq 0 ]]; then "$@"; else
    command -v sudo >/dev/null 2>&1 || die "Cần cài sudo để cài gói hệ thống."
    sudo "$@"
  fi
}

# Chạy lệnh trong phiên desktop của người dùng, kể cả khi installer chạy bằng
# sudo, để gsettings và ibus nói chuyện đúng session bus.
run_as_desktop_user() {
  local target_user="${SUDO_USER:-${USER:-}}" target_uid
  if [[ -z "$target_user" || "$target_user" == root || "$(id -un)" == "$target_user" ]]; then
    "$@"
    return
  fi
  target_uid="$(id -u "$target_user")"
  sudo -H -u "$target_user" env \
    XDG_RUNTIME_DIR="/run/user/$target_uid" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$target_uid/bus" \
    "$@"
}

restart_ibus() {
  if ! command -v ibus >/dev/null 2>&1; then
    log "Không tìm thấy lệnh ibus. Hãy đăng xuất rồi đăng nhập lại."
    return
  fi
  # IBus chỉ thấy engine mới sau khi cache component được làm mới.
  run_as_desktop_user ibus write-cache >/dev/null 2>&1 || true
  if pgrep -x ibus-daemon >/dev/null 2>&1; then
    log "Đang khởi động lại IBus..."
    run_as_desktop_user ibus restart >/dev/null 2>&1 \
      || log "Không thể tự khởi động lại IBus; hãy đăng xuất rồi đăng nhập lại."
  else
    log "IBus chưa chạy. Hãy thiết lập desktop theo README rồi đăng nhập lại."
  fi
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Không tìm thấy '$1'. Hãy cài $1 rồi chạy lại."
}

log_existing_package() {
  local version=""
  case "$ARECA_PACKAGE_TYPE" in
    deb)
      if command -v dpkg-query >/dev/null 2>&1; then
        version="$(dpkg-query -W -f='${Version}' "$ARECA_PACKAGE" 2>/dev/null || true)"
      fi ;;
    rpm)
      if command -v rpm >/dev/null 2>&1; then
        version="$(rpm -q --qf '%{VERSION}-%{RELEASE}' "$ARECA_PACKAGE" 2>/dev/null || true)"
      fi ;;
    pacman)
      if command -v pacman >/dev/null 2>&1; then
        version="$(pacman -Q "$ARECA_PACKAGE" 2>/dev/null | awk '{print $2}' || true)"
      fi ;;
  esac

  if [[ -n "$version" ]]; then
    log "Đã phát hiện $ARECA_PACKAGE ${version}; sẽ cài gói release mới để nâng cấp."
  fi
}

# Bản cài từ mã nguồn bằng scripts/install.sh để lại file không thuộc gói nào;
# gỡ chúng để trình quản lý gói nhận quyền sở hữu.
remove_unowned_package_conflicts() {
  local file
  local -a stale_files=()
  case "$ARECA_PACKAGE_TYPE" in
    deb)
      require_command dpkg-deb
      require_command tar
      while IFS= read -r file; do
        file="/${file#./}"
        [[ "$file" == /* && ( -f "$file" || -L "$file" ) ]] || continue
        if ! dpkg-query -S "$file" >/dev/null 2>&1; then
          stale_files+=("$file")
        fi
      done < <(dpkg-deb --fsys-tarfile "$ARECA_PACKAGE_PATH" | tar -tf -) ;;
    rpm)
      require_command rpm
      while IFS= read -r file; do
        [[ "$file" == /* && ( -f "$file" || -L "$file" ) ]] || continue
        if ! rpm -qf "$file" >/dev/null 2>&1; then
          stale_files+=("$file")
        fi
      done < <(rpm -qp --qf '[%{FILENAMES}\n]' "$ARECA_PACKAGE_PATH") ;;
    *) return 0 ;;
  esac

  if (( ${#stale_files[@]} > 0 )); then
    log "Đang thay ${#stale_files[@]} file ibus-areca cũ chưa được trình quản lý gói theo dõi."
    run_root rm -f -- "${stale_files[@]}"
  fi
}

# Thêm 'areca' vào danh sách engine của IBus, và vào nguồn nhập của GNOME nếu
# đang dùng GNOME. Không đổi thứ tự các bộ gõ người dùng đã có.
add_engine_for_user() {
  local current updated

  if command -v gsettings >/dev/null 2>&1 && \
     gsettings list-schemas 2>/dev/null | grep -qx org.freedesktop.ibus.general; then
    current="$(gsettings get org.freedesktop.ibus.general preload-engines 2>/dev/null || echo "@as []")"
    if [[ "$current" != *"'areca'"* ]]; then
      # Danh sách rỗng in ra kèm kiểu, ví dụ "@as []".
      if [[ "$current" =~ ^(@[^[:space:]]+[[:space:]])?\[\]$ ]]; then
        updated="['areca']"
      else
        updated="${current%]}, 'areca']"
      fi
      gsettings set org.freedesktop.ibus.general preload-engines "$updated" \
        && printf '[ibus-areca] Đã thêm Areca vào danh sách bộ gõ của IBus.\n'
    else
      printf '[ibus-areca] Areca đã có trong danh sách bộ gõ của IBus.\n'
    fi
  fi

  if command -v gsettings >/dev/null 2>&1 && \
     gsettings list-schemas 2>/dev/null | grep -qx org.gnome.desktop.input-sources; then
    current="$(gsettings get org.gnome.desktop.input-sources sources 2>/dev/null || echo "@a(ss) []")"
    if [[ "$current" != *"('ibus', 'areca')"* ]]; then
      if [[ "$current" =~ ^(@[^[:space:]]+[[:space:]])?\[\]$ ]]; then
        updated="[('xkb', 'us'), ('ibus', 'areca')]"
      else
        updated="${current%]}, ('ibus', 'areca')]"
      fi
      gsettings set org.gnome.desktop.input-sources sources "$updated" \
        && printf '[ibus-areca] Đã thêm Areca vào nguồn nhập của GNOME.\n'
    fi
  fi
}

configure_ibus() {
  local target_user="${SUDO_USER:-${USER:-}}"
  if [[ $EUID -eq 0 ]] && [[ -z "$target_user" || "$target_user" == root ]]; then
    log "Không xác định được tài khoản desktop; bỏ qua tự thêm Areca vào IBus."
    return
  fi
  run_as_desktop_user bash -c "$(declare -f add_engine_for_user); add_engine_for_user" \
    || log "Không tự thêm được Areca vào IBus; hãy thêm thủ công theo README."
}

select_platform() {
  [[ "$(uname -s)" == Linux ]] || die "Installer này chỉ hỗ trợ Linux."
  case "$(uname -m)" in x86_64|amd64) ;; *) die "Release chỉ hỗ trợ x86_64, không hỗ trợ $(uname -m)." ;; esac
  [[ -r /etc/os-release ]] || die "Không đọc được /etc/os-release."
  # shellcheck disable=SC1091
  . /etc/os-release
  ARECA_OS_NAME="${PRETTY_NAME:-${ID:-Linux}}"
  case "${ID:-}" in
    ubuntu)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in
        24.04) ARECA_ASSET_MARKER=_ubuntu24_amd64.deb ;;
        25.10) ARECA_ASSET_MARKER=_ubuntu25_amd64.deb ;;
        26.04) ARECA_ASSET_MARKER=_ubuntu26_amd64.deb ;;
        *) die "Ubuntu ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;;
      esac ;;
    linuxmint)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 22|22.*) ARECA_ASSET_MARKER=_mint22_amd64.deb ;; *) die "Linux Mint ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    pop)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 24.04) ARECA_ASSET_MARKER=_popos24_amd64.deb ;; *) die "Pop!_OS ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    zorin)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 17|17.*) ARECA_ASSET_MARKER=_zorin17_amd64.deb ;; *) die "Zorin OS ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    arch) ARECA_PACKAGE_TYPE=pacman; ARECA_ASSET_MARKER=-x86_64.pkg.tar.zst ;;
    cachyos|manjaro|endeavouros|garuda) ARECA_PACKAGE_TYPE=pacman; ARECA_ASSET_MARKER="-x86_64.${ID}.pkg.tar.zst" ;;
    fedora)
      ARECA_PACKAGE_TYPE=rpm
      case "${VERSION_ID:-}" in 42|43|44) ARECA_ASSET_MARKER=".fedora${VERSION_ID}.x86_64.rpm" ;; *) die "Fedora ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    nobara|bazzite) ARECA_PACKAGE_TYPE=rpm; ARECA_ASSET_MARKER=".${ID}.x86_64.rpm" ;;
    opensuse-tumbleweed) ARECA_PACKAGE_TYPE=rpm; ARECA_ASSET_MARKER=.opensuse-tumbleweed.x86_64.rpm ;;
    *) die "${ARECA_OS_NAME} chưa có trong danh sách release được hỗ trợ. Hãy build từ mã nguồn theo README." ;;
  esac
}

find_release_asset() {
  local json tag url candidate
  log "Đang kiểm tra release mới nhất..."
  json="$(curl -fsSL --retry 3 -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/${ARECA_REPO}/releases/latest")" \
    || die "Không thể lấy thông tin release mới nhất. Hãy kiểm tra kết nối mạng, hoặc build từ mã nguồn theo README nếu repo chưa có release."
  tag="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$json" | head -n1)"
  [[ -n "$tag" ]] || die "Release mới nhất không có tag hợp lệ."
  url=""
  while IFS= read -r candidate; do
    if [[ "$candidate" == *"$ARECA_ASSET_MARKER" ]]; then url="$candidate"; break; fi
  done < <(sed -n 's/.*"browser_download_url"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$json")
  [[ -n "$url" ]] || die "Release $tag không có gói dành cho ${ARECA_OS_NAME}."
  ARECA_RELEASE_TAG="$tag"; ARECA_ASSET_URL="$url"; ARECA_ASSET_NAME="${url##*/}"
}

download_release_asset() {
  ARECA_TMP_DIR="$(mktemp -d)"
  ARECA_PACKAGE_PATH="$ARECA_TMP_DIR/$ARECA_ASSET_NAME"
  log "Tìm thấy $ARECA_RELEASE_TAG: $ARECA_ASSET_NAME"
  curl -fL --retry 3 --progress-bar -o "$ARECA_PACKAGE_PATH" "$ARECA_ASSET_URL" || die "Tải gói cài đặt thất bại."
}

install_package() {
  local package_path="$ARECA_PACKAGE_PATH"
  log "Đang cài đặt (hệ thống có thể hỏi mật khẩu)..."
  case "$ARECA_PACKAGE_TYPE" in
    deb)
      if dpkg-query -W -f='${Status}' "$ARECA_PACKAGE" 2>/dev/null | grep -q 'install ok installed'; then
        log "Đang gỡ $ARECA_PACKAGE cũ..."
        run_root apt-get remove -y "$ARECA_PACKAGE"
      fi
      remove_unowned_package_conflicts
      require_command apt-get
      run_root apt-get install -y "$package_path" ;;
    pacman)
      local file
      local -a overwrite_args=()
      require_command pacman
      if pacman -Qq "$ARECA_PACKAGE" >/dev/null 2>&1; then
        log "Đang gỡ $ARECA_PACKAGE cũ..."
        run_root pacman -R --noconfirm "$ARECA_PACKAGE"
      fi

      # Bản cài tay có thể để lại file không thuộc gói nào. Chỉ cho ghi đè
      # đúng những đường dẫn chưa có chủ trong gói này.
      while IFS= read -r file; do
        [[ -e "$file" || -L "$file" ]] || continue
        if ! pacman -Qo "$file" >/dev/null 2>&1; then
          overwrite_args+=(--overwrite "$file")
        fi
      done < <(pacman -Qlp "$package_path" | awk 'NF >= 2 { print $2 }')

      if (( ${#overwrite_args[@]} > 0 )); then
        log "Đang thay thế ${#overwrite_args[@]} file ibus-areca cũ chưa được pacman quản lý."
      fi
      run_root pacman -U --needed --noconfirm "${overwrite_args[@]}" "$package_path" ;;
    rpm)
      if rpm -q "$ARECA_PACKAGE" >/dev/null 2>&1; then
        log "Đang gỡ $ARECA_PACKAGE cũ..."
        if command -v dnf >/dev/null 2>&1; then
          run_root dnf remove -y "$ARECA_PACKAGE"
        elif command -v zypper >/dev/null 2>&1; then
          run_root zypper --non-interactive remove "$ARECA_PACKAGE"
        fi
      fi
      remove_unowned_package_conflicts
      if command -v dnf >/dev/null 2>&1; then run_root dnf install -y "$package_path"
      elif command -v zypper >/dev/null 2>&1; then run_root zypper --non-interactive install "$package_path"
      else die "Không tìm thấy dnf hoặc zypper để cài gói RPM."; fi ;;
  esac
}

uinput_is_configured() {
  local target_user="${SUDO_USER:-${USER:-}}"
  [[ -n "$target_user" && "$target_user" != root ]] || return 1
  if id -nG "$target_user" | tr ' ' '\n' | grep -Fxq input; then
    return 0
  fi
  if [[ "$target_user" == "$(id -un)" ]]; then
    [[ -w /dev/uinput ]]
  elif command -v runuser >/dev/null 2>&1; then
    runuser -u "$target_user" -- test -w /dev/uinput
  else
    return 1
  fi
}

prepare_uinput() {
  local target_user="${SUDO_USER:-${USER:-}}"
  if uinput_is_configured; then
    log "Uinput đã được cấu hình cho '$target_user'; không cần reboot."
    return
  fi

  [[ -n "$target_user" && "$target_user" != root ]] \
    || die "Không xác định được user desktop để cấu hình uinput. Hãy chạy installer từ tài khoản desktop."
  ask_yes_no "Uinput chưa được cấu hình cho '$target_user'. Cấu hình quyền và tự reboot sau khi cài?" \
    || die "Đã hủy cài đặt vì uinput chưa được cấu hình."

  getent group input >/dev/null 2>&1 || run_root groupadd --system input
  run_root usermod -aG input "$target_user"
  run_root modprobe uinput || true
  if command -v udevadm >/dev/null 2>&1; then
    run_root udevadm control --reload-rules
    run_root udevadm trigger --subsystem-match=misc --action=add || true
  fi
  ARECA_REBOOT_REQUIRED=1
  log "Đã cấp quyền uinput cho '$target_user'; máy sẽ tự reboot sau khi cài xong."
}

main() {
  require_command curl
  select_platform
  log "Hệ điều hành: $ARECA_OS_NAME"
  find_release_asset
  log_existing_package
  download_release_asset
  prepare_uinput
  install_package
  configure_ibus
  log "Cài đặt ibus-areca hoàn tất."
  if [[ "$ARECA_REBOOT_REQUIRED" == 1 ]]; then
    log "Đang reboot để áp dụng quyền uinput..."
    run_root systemctl reboot
  else
    restart_ibus
  fi
}

main "$@"

#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"
ARECA_PURGE_DATA=1
ARECA_RESTART_IBUS=1
ARECA_USER_ONLY=0

areca_target_home() {
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v getent >/dev/null 2>&1; then
    getent passwd "$ARECA_TARGET_USER" | cut -d: -f6
    return
  fi
  printf '%s\n' "${HOME:?HOME is required}"
}

ARECA_TARGET_HOME="$(areca_target_home)"

run_as_target_user() {
  if [[ -z "$ARECA_TARGET_USER" || "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    local target_uid
    target_uid="$(id -u "$ARECA_TARGET_USER")"
    sudo -H -u "$ARECA_TARGET_USER" env \
      XDG_RUNTIME_DIR="/run/user/$target_uid" \
      DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$target_uid/bus" \
      "$@"
  else
    return 1
  fi
}

run_as_root() {
  if [[ $EUID -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "[ibus-areca] Cần quyền sudo để xóa file hệ thống" >&2
    return 1
  fi
}

# Areca bản fcitx5 dùng chung hai luật udev; còn bản đó thì giữ luật lại để nó
# vẫn dùng được uinput và theo dõi chuột.
fcitx_areca_installed() {
  local candidate
  for candidate in /usr/lib/fcitx5/areca.so /usr/lib64/fcitx5/areca.so \
                   /usr/lib/*/fcitx5/areca.so /usr/local/lib/fcitx5/areca.so \
                   /usr/local/lib64/fcitx5/areca.so \
                   "$ARECA_TARGET_HOME"/.local/lib/fcitx5/areca.so; do
    [[ -e "$candidate" ]] && return 0
  done
  return 1
}

is_shared_udev_rule() {
  case "$(basename -- "$1")" in
    70-areca-pointer.rules|99-uinput-areca.rules) return 0 ;;
  esac
  return 1
}

remove_path() {
  local target="$1"
  if [[ -z "$target" || "$target" == "/" ]]; then
    return
  fi
  if is_shared_udev_rule "$target" && fcitx_areca_installed; then
    if [[ -e "$target" ]]; then
      echo "[ibus-areca] Keeping $target (still used by fcitx5-areca)"
    fi
    return
  fi
  if [[ -e "$target" || -L "$target" ]]; then
    echo "[ibus-areca] Removing $target"
    if [[ -w "$target" && -w "$(dirname "$target")" ]]; then
      rm -rf -- "$target"
    else
      run_as_root rm -rf -- "$target"
    fi
  fi
}

remove_manifest_files() {
  local manifest="$1"
  if [[ -f "$manifest" ]]; then
    echo "[ibus-areca] Checking manifest $manifest"
    while IFS= read -r line || [[ -n "$line" ]]; do
      if [[ -n "$line" && "$line" =~ areca ]]; then
        remove_path "$line"
      fi
    done < "$manifest"
  fi
}

remove_installed_files_in_prefix() {
  local prefix="$1"
  if [[ -z "$prefix" || ! -d "$prefix" ]]; then
    return
  fi

  remove_path "$prefix/libexec/ibus-engine-areca"
  remove_path "$prefix/libexec/ibus-areca-mouse-monitor"
  remove_path "$prefix/lib/ibus/ibus-engine-areca"
  remove_path "$prefix/share/ibus/component/areca.xml"

  remove_path "$prefix/bin/ibus-areca-settings"
  remove_path "$prefix/share/applications/io.github.areca.IBusSettings.desktop"
  remove_path "$prefix/share/ibus-areca"

  remove_path "$prefix/share/icons/hicolor/scalable/apps/ibus-areca.svg"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/ibus-areca.png"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/ibus-areca-settings.svg"
  remove_path "$prefix/share/icons/hicolor/symbolic/apps/ibus-areca-symbolic.svg"

  remove_path "$prefix/lib/udev/rules.d/70-areca-pointer.rules"
  remove_path "$prefix/lib/udev/rules.d/99-uinput-areca.rules"
}

reload_udev_rules() {
  if command -v udevadm >/dev/null 2>&1; then
    echo "[ibus-areca] Reloading udev rules"
    run_as_root udevadm control --reload-rules >/dev/null 2>&1 || true
    run_as_root udevadm trigger >/dev/null 2>&1 || true
  fi
}

update_caches() {
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    for icon_dir in "/usr/share/icons/hicolor" "$ARECA_TARGET_HOME/.local/share/icons/hicolor"; do
      if [[ -d "$icon_dir" ]]; then
        echo "[ibus-areca] Updating icon cache in $icon_dir"
        if [[ -w "$icon_dir" ]]; then
          gtk-update-icon-cache -f -t "$icon_dir" >/dev/null 2>&1 || true
        else
          run_as_root gtk-update-icon-cache -f -t "$icon_dir" >/dev/null 2>&1 || true
        fi
      fi
    done
  fi

  if command -v update-desktop-database >/dev/null 2>&1; then
    for app_dir in "/usr/share/applications" "$ARECA_TARGET_HOME/.local/share/applications"; do
      if [[ -d "$app_dir" ]]; then
        echo "[ibus-areca] Updating desktop database in $app_dir"
        if [[ -w "$app_dir" ]]; then
          update-desktop-database "$app_dir" >/dev/null 2>&1 || true
        else
          run_as_root update-desktop-database "$app_dir" >/dev/null 2>&1 || true
        fi
      fi
    done
  fi

  if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "[ibus-areca] Refreshing KDE application cache"
    run_as_target_user kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
  elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    echo "[ibus-areca] Refreshing KDE application cache"
    run_as_target_user kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
  fi
}

remove_user_data() {
  echo "[ibus-areca] Removing ibus-areca user configuration and data"
  if [[ -n "${XDG_CONFIG_HOME:-}" ]]; then
    remove_path "$XDG_CONFIG_HOME/ibus-areca"
  fi
  remove_path "$ARECA_TARGET_HOME/.config/ibus-areca"
  # Trình khởi chạy riêng mà app cài đặt tạo khi đổi biểu tượng.
  remove_path "$ARECA_TARGET_HOME/.local/share/applications/io.github.areca.IBusSettings.desktop"
}

restart_ibus() {
  if [[ "$ARECA_RESTART_IBUS" != 1 ]] || ! command -v ibus >/dev/null 2>&1; then
    return
  fi
  echo "[ibus-areca] Refreshing IBus component cache"
  run_as_target_user ibus write-cache >/dev/null 2>&1 || true
  if pgrep -u "$ARECA_TARGET_USER" -x ibus-daemon >/dev/null 2>&1; then
    echo "[ibus-areca] Restarting IBus"
    run_as_target_user ibus restart >/dev/null 2>&1 || true
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      ARECA_USER_ONLY=1
      ARECA_PREFIX="$ARECA_TARGET_HOME/.local"
      shift
      ;;
    --prefix)
      ARECA_PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --build-dir)
      ARECA_BUILD_DIR="${2:?missing value for --build-dir}"
      shift 2
      ;;
    --keep-data)
      ARECA_PURGE_DATA=0
      shift
      ;;
    --no-restart)
      ARECA_RESTART_IBUS=0
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --user                 Only remove user installation in $ARECA_TARGET_HOME/.local
  --prefix PATH          Custom installation prefix to clean
  --build-dir PATH       CMake build directory to check install manifest
  --keep-data            Keep user configuration files in ~/.config/ibus-areca
  --no-restart           Do not refresh and restart IBus after uninstall
EOF
      exit 0
      ;;
    *)
      echo "[ibus-areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

echo "[ibus-areca] Starting ibus-areca uninstallation"

remove_manifest_files "$ARECA_BUILD_DIR/install_manifest.txt"
remove_manifest_files "$ARECA_ROOT_DIR/build/install_manifest.txt"
remove_manifest_files "$ARECA_ROOT_DIR/build-make/install_manifest.txt"

if [[ "$ARECA_USER_ONLY" == 1 ]]; then
  remove_installed_files_in_prefix "$ARECA_TARGET_HOME/.local"
else
  remove_installed_files_in_prefix "$ARECA_PREFIX"
  if [[ "$ARECA_PREFIX" != "/usr" ]]; then
    remove_installed_files_in_prefix "/usr"
  fi
  remove_installed_files_in_prefix "/usr/local"
  remove_installed_files_in_prefix "$ARECA_TARGET_HOME/.local"
  reload_udev_rules
fi

if [[ "$ARECA_PURGE_DATA" == 1 ]]; then
  remove_user_data
fi

update_caches
restart_ibus

echo "[ibus-areca] Uninstallation complete. IBus, Fcitx5 and fcitx5-areca remain untouched."

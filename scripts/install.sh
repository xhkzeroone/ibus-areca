#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"
ARECA_INSTALL_DEPS=1
ARECA_UPDATE_SYSTEM=0
ARECA_RESTART_IBUS=1
ARECA_RUN_TESTS=1

areca_target_home() {
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v getent >/dev/null 2>&1; then
    getent passwd "$ARECA_TARGET_USER" | cut -d: -f6
    return
  fi
  printf '%s\n' "${HOME:?HOME is required}"
}

ARECA_TARGET_HOME="$(areca_target_home)"

is_user_prefix() {
  [[ "$ARECA_PREFIX" == "$ARECA_TARGET_HOME" ||
     "$ARECA_PREFIX" == "$ARECA_TARGET_HOME/"* ]]
}

run_as_target_user() {
  if [[ "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
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

update_icon_cache() {
  if ! command -v gtk-update-icon-cache >/dev/null 2>&1; then
    return
  fi
  local icon_theme_dir="$ARECA_PREFIX/share/icons/hicolor"
  if [[ ! -d "$icon_theme_dir" ]]; then
    return
  fi
  echo "[ibus-areca] Updating hicolor icon cache (optional)"
  if is_user_prefix; then
    gtk-update-icon-cache -f -t "$icon_theme_dir" >/dev/null 2>&1 || true
  else
    sudo gtk-update-icon-cache -f -t "$icon_theme_dir" >/dev/null 2>&1 || true
  fi
}

update_desktop_cache() {
  local applications_dir="$ARECA_PREFIX/share/applications"
  if [[ -d "$applications_dir" ]] && \
     command -v update-desktop-database >/dev/null 2>&1; then
    echo "[ibus-areca] Updating desktop application cache (optional)"
    if is_user_prefix; then
      update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
    else
      sudo update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
    fi
  fi

  if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "[ibus-areca] Refreshing KDE application cache (optional)"
    run_as_target_user kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
  elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    echo "[ibus-areca] Refreshing KDE application cache (optional)"
    run_as_target_user kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
  fi
}

install_deps_debian() {
  echo "[ibus-areca] Installing build dependencies with apt"
  sudo apt-get update
  sudo apt-get install -y build-essential cmake ninja-build pkg-config \
    golang-go ibus libibus-1.0-dev libglib2.0-dev libxkbcommon-dev \
    libinput-dev libudev-dev libdbus-1-dev libei-dev libportal-dev \
    python3-gi gir1.2-gtk-4.0 gir1.2-gdkpixbuf-2.0
}

install_deps_arch() {
  local packages=(base-devel cmake ninja pkgconf go ibus glib2 libxkbcommon
                  gtk4 python python-gobject gdk-pixbuf2
                  libinput systemd-libs dbus libei libportal)
  if [[ "$ARECA_UPDATE_SYSTEM" == 1 ]]; then
    echo "[ibus-areca] Updating Arch system and installing build dependencies"
    sudo pacman -Syu --needed --noconfirm "${packages[@]}"
    return
  fi

  # Không dùng pacman -Sy: refresh database rồi cài package mà không nâng toàn
  # hệ thống là partial upgrade và không được Arch hỗ trợ.
  echo "[ibus-areca] Installing Arch dependencies without refreshing or upgrading the system"
  if ! sudo pacman -S --needed --noconfirm "${packages[@]}"; then
    cat >&2 <<EOF
[ibus-areca] Dependency installation failed without a system update.
[ibus-areca] If dependencies are already installed, rerun with --skip-deps.
[ibus-areca] To perform a supported full Arch upgrade, rerun with --system-update.
EOF
    return 1
  fi
}

install_deps_fedora() {
  echo "[ibus-areca] Installing build dependencies with dnf"
  sudo dnf install -y \
    gcc-c++ cmake ninja-build pkgconf-pkg-config go \
    ibus ibus-devel glib2-devel libxkbcommon-devel \
    python3-gobject gtk4 gdk-pixbuf2 \
    libinput-devel systemd-devel dbus-devel libei-devel libportal-devel
}

install_deps_opensuse() {
  echo "[ibus-areca] Installing build dependencies with zypper"
  sudo zypper --non-interactive install --no-recommends \
    gcc-c++ cmake ninja pkg-config go ibus ibus-devel glib2-devel \
    libxkbcommon-devel python3-gobject python3-gobject-Gdk \
    typelib-1_0-Gtk-4_0 libgtk-4-1 typelib-1_0-GdkPixbuf-2_0 \
    libinput-devel systemd-devel libei-devel libportal-devel dbus-1-devel \
    libudev-devel
}

distro_matches() {
  local needle="$1" token
  for token in ${ID:-} ${ID_LIKE:-}; do
    [[ "$token" == "$needle" ]] && return 0
  done
  return 1
}

install_build_deps() {
  if [[ "$(uname -s)" != "Linux" || ! -r /etc/os-release ]]; then
    echo "[ibus-areca] Skipping dependency installation: unsupported host"
    return
  fi

  # shellcheck disable=SC1091
  . /etc/os-release
  if command -v apt-get >/dev/null 2>&1; then
    if distro_matches ubuntu || distro_matches debian || distro_matches linuxmint || distro_matches mint; then
      install_deps_debian
      return
    fi
  fi
  if command -v pacman >/dev/null 2>&1; then
    if distro_matches arch; then install_deps_arch; return; fi
  fi
  if command -v dnf >/dev/null 2>&1; then
    if distro_matches fedora || distro_matches rhel || distro_matches centos; then
      install_deps_fedora
      return
    fi
  fi
  if command -v zypper >/dev/null 2>&1; then
    if distro_matches opensuse || distro_matches suse; then
      install_deps_opensuse
      return
    fi
  fi
  echo "[ibus-areca] Skipping dependency installation for ${PRETTY_NAME:-unknown distro}"
}

restart_ibus() {
  if ! command -v ibus >/dev/null 2>&1; then
    return
  fi
  # Cache component phải được làm mới thì IBus mới thấy engine vừa cài.
  echo "[ibus-areca] Refreshing IBus component cache"
  run_as_target_user ibus write-cache >/dev/null 2>&1 || true
  if pgrep -u "$ARECA_TARGET_USER" -x ibus-daemon >/dev/null 2>&1; then
    echo "[ibus-areca] Restarting IBus (best effort)"
    run_as_target_user ibus restart >/dev/null 2>&1 || true
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
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
    --build-type)
      ARECA_BUILD_TYPE="${2:?missing value for --build-type}"
      shift 2
      ;;
    --skip-deps)
      ARECA_INSTALL_DEPS=0
      shift
      ;;
    --no-system-update)
      ARECA_UPDATE_SYSTEM=0
      shift
      ;;
    --system-update)
      ARECA_UPDATE_SYSTEM=1
      shift
      ;;
    --skip-tests)
      ARECA_RUN_TESTS=0
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
  --user                 Install to $ARECA_TARGET_HOME/.local without sudo
  --prefix PATH          Installation prefix (default: /usr)
  --build-dir PATH       CMake build directory (default: ./build)
  --build-type TYPE      CMake build type (default: RelWithDebInfo)
  --skip-deps            Do not install distro build dependencies
  --no-system-update     Install Arch dependencies without pacman -Sy (default)
  --system-update        Run pacman -Syu before building on Arch
  --skip-tests           Do not run CTest
  --no-restart           Do not refresh and restart IBus

Environment:
  PREFIX=/usr
  BUILD_DIR=./build
  BUILD_TYPE=RelWithDebInfo
EOF
      exit 0
      ;;
    *)
      echo "[ibus-areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$ARECA_PREFIX" in
  "~") ARECA_PREFIX="$ARECA_TARGET_HOME" ;;
  "~/"*) ARECA_PREFIX="$ARECA_TARGET_HOME/${ARECA_PREFIX#"~/"}" ;;
esac

echo "[ibus-areca] root=$ARECA_ROOT_DIR"
echo "[ibus-areca] build=$ARECA_BUILD_DIR prefix=$ARECA_PREFIX type=$ARECA_BUILD_TYPE"

if [[ "$ARECA_INSTALL_DEPS" == 1 ]]; then
  if is_user_prefix; then
    echo "[ibus-areca] User install selected; skipping system dependency installation"
  else
    install_build_deps
  fi
fi

if [[ -d "$ARECA_ROOT_DIR/.git" ]]; then
  echo "[ibus-areca] Initializing Bamboo submodule"
  git -C "$ARECA_ROOT_DIR" submodule update --init
fi

# Giữ generator của build cache cũ. Cache tạo bằng Ninja trên máy đã gỡ ninja
# thì dùng thư mục build khác thay vì xoá cache.
ARECA_CMAKE_GENERATOR=""
if [[ -f "$ARECA_BUILD_DIR/CMakeCache.txt" ]]; then
  ARECA_CACHED_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' \
    "$ARECA_BUILD_DIR/CMakeCache.txt" | head -n 1)"
  if [[ "$ARECA_CACHED_GENERATOR" == "Ninja" ]] && \
     ! command -v ninja >/dev/null 2>&1; then
    echo "[ibus-areca] Existing build cache requires Ninja, but ninja is unavailable"
    ARECA_BUILD_DIR="${ARECA_BUILD_DIR}-make"
    ARECA_CMAKE_GENERATOR="Unix Makefiles"
    echo "[ibus-areca] Using fallback build directory: $ARECA_BUILD_DIR"
  fi
elif command -v ninja >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Ninja"
elif command -v make >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Unix Makefiles"
else
  echo "[ibus-areca] No build program found; install ninja or make" >&2
  exit 1
fi

ARECA_GENERATOR_ARGS=()
if [[ -n "$ARECA_CMAKE_GENERATOR" ]]; then
  ARECA_GENERATOR_ARGS=(-G "$ARECA_CMAKE_GENERATOR")
fi

if ! command -v c++ >/dev/null 2>&1 && \
   ! command -v g++ >/dev/null 2>&1 && \
   ! command -v clang++ >/dev/null 2>&1; then
  echo "[ibus-areca] No C++ compiler found; install g++ or clang++" >&2
  exit 1
fi

if ! pkg-config --exists ibus-1.0; then
  echo "[ibus-areca] libibus development files (ibus-1.0.pc) not found." >&2
  echo "[ibus-areca] Install ibus (Arch) or libibus-1.0-dev / ibus-devel, or rerun without --skip-deps." >&2
  exit 1
fi

cmake -S "$ARECA_ROOT_DIR" -B "$ARECA_BUILD_DIR" \
  "${ARECA_GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$ARECA_BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$ARECA_PREFIX"

echo "[ibus-areca] Building"
cmake --build "$ARECA_BUILD_DIR" -j

if [[ ! -x "$ARECA_BUILD_DIR/ibus-engine-areca" ]]; then
  echo "[ibus-areca] ibus-engine-areca was not built" >&2
  exit 1
fi

if [[ "$ARECA_RUN_TESTS" == 1 ]]; then
  echo "[ibus-areca] Running C++ tests"
  ctest --test-dir "$ARECA_BUILD_DIR" --output-on-failure
fi

if is_user_prefix; then
  echo "[ibus-areca] Installing to user prefix"
  cmake --install "$ARECA_BUILD_DIR"
else
  echo "[ibus-areca] Installing with sudo"
  sudo cmake --install "$ARECA_BUILD_DIR"
  sudo modprobe uinput >/dev/null 2>&1 || true
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v usermod >/dev/null 2>&1; then
    echo "[ibus-areca] Configuring /dev/uinput group permissions for $ARECA_TARGET_USER"
    sudo usermod -aG input "$ARECA_TARGET_USER" || true
    sudo udevadm control --reload-rules >/dev/null 2>&1 && sudo udevadm trigger >/dev/null 2>&1 || true
  fi
fi

update_icon_cache
update_desktop_cache

if [[ "$ARECA_RESTART_IBUS" == 1 ]]; then
  restart_ibus
fi

if is_user_prefix; then
  cat <<EOF
[ibus-areca] Done (user prefix).

IBus only reads system component directories. Before starting ibus-daemon, run:
  export IBUS_COMPONENT_PATH="$ARECA_PREFIX/share/ibus/component:/usr/share/ibus/component"
  ibus write-cache
EOF
else
  cat <<EOF
[ibus-areca] Done.

Next:
  - Open the desktop Keyboard / Input Sources settings and add Vietnamese → Areca,
    or run:  ibus engine areca
  - Test next to Fcitx5 without switching the system input method:
      ibus-daemon -drx
      ibus engine areca
      GTK_IM_MODULE=ibus gnome-text-editor   (or QT_IM_MODULE=ibus kwrite)
  - Settings:  ibus-areca-settings
  - Debug log: set Debug=True in ~/.config/ibus-areca/conf/areca.conf, then run
      journalctl --user -f -t ibus-engine-areca
EOF
fi

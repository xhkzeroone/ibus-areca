# Areca GTK Settings

The GTK4/PyGObject settings application for ibus-areca. It is installed as `ibus-areca-settings` and opened from the **Cài đặt Areca...** item in the IBus menu.

## Run

```sh
python3 gtk-settings/areca_settings.py
```

Dependencies: Python 3, PyGObject, and GTK 4 (`python3-gi` and `gir1.2-gtk-4.0` on Debian/Ubuntu).

The app edits the engine's files under `~/.config/ibus-areca/conf/`: `areca.conf`, `areca-macro-table.conf`, `areca-apps.conf`, and `areca-advanced.conf`. Updates are written atomically. The running engine watches that directory and reloads the files by itself, so saving needs no extra step. The About page can restart IBus with `ibus restart`.

The **Giao diện** tab defaults to Light and offers nine presets: Light, Dark, Areca Dark, Gruvbox, Catppuccin Mocha, Catppuccin Latte, Dracula, One Dark, and Tokyo Night, plus a system color scheme option. Colors apply immediately, including to confirmation dialogs. The selected theme is saved atomically in `[Appearance] Theme` in `areca-gtk-settings.conf` in the same config directory (`XDG_CONFIG_HOME` is respected). Appearance changes do not reload the engine. Restoring defaults on this tab returns to Light.

The **Biểu tượng** tab uses a set of Areca-drawn navigation and action icons and includes six playful mascot choices plus the original Areca mark. Choose one mascot or import a PNG, JPEG, SVG, or WebP image up to 16 MB; the same selection updates both Settings and the IBus tray icon. Imported art is copied into the user's hicolor icon theme as transparent, square PNG sizes, so the source file can be moved later. A per-user GTK launcher is created for the customized Settings icon. The engine reads the tray icon from `TrayIcon` in `areca.conf` and shows it through the `InputMode` property of the IBus panel. Restore defaults to return both icons to the original mark.

Run the config document round-trip check with:

```sh
python3 gtk-settings/areca_settings.py --self-test
```

Check preset text contrast (normal text, secondary text, accent button states, and selection text):

```sh
python3 -m unittest discover -s gtk-settings/tests
```

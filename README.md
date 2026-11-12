# Areca IME cho IBus

[![GitHub stars](https://img.shields.io/github/stars/xhkzeroone/ibus-areca?style=flat&logo=github)](https://github.com/xhkzeroone/ibus-areca/stargazers)
[![Commit activity](https://img.shields.io/github/commit-activity/m/xhkzeroone/ibus-areca)](https://github.com/xhkzeroone/ibus-areca/commits/main)
[![Last commit](https://img.shields.io/github/last-commit/xhkzeroone/ibus-areca)](https://github.com/xhkzeroone/ibus-areca/commits/main)

Areca là bộ gõ tiếng Việt cho Linux, chạy như một engine của IBus và được tối ưu
cho Wayland. Lõi ghép dấu là
[Bamboo Engine](https://github.com/BambooEngine/bamboo-core). Areca tự chọn cách
sửa chữ phù hợp với từng ứng dụng để gõ trực tiếp, không gạch chân, kể cả trên
trình duyệt, Electron, terminal và ứng dụng XWayland.

> ibus-areca đang trong giai đoạn thử nghiệm. Khi gặp lỗi, hãy
> [mở issue](https://github.com/xhkzeroone/ibus-areca/issues) kèm bản phân phối,
> desktop, ứng dụng đang dùng và log debug.

## Mục lục

- [Tính năng](#tính-năng)
- [Cài đặt nhanh](#cài-đặt-nhanh)
- [Cài đặt chi tiết](#cài-đặt-chi-tiết)
- [Cấu hình](#cấu-hình)
- [uinput và quyền truy cập](#uinput-và-quyền-truy-cập)
- [Xử lý sự cố](#xử-lý-sự-cố)
- [Build từ mã nguồn](#build-từ-mã-nguồn)
- [Gỡ cài đặt](#gỡ-cài-đặt)
- [Đóng góp](#đóng-góp)
- [Tài liệu](#tài-liệu)
- [Ghi nhận](#ghi-nhận)
- [Giấy phép](#giấy-phép)

## Tính năng

**Kiểu gõ và bảng mã**
- Telex, Telex 2, Telex W, VNI, VIQR, Microsoft layout, VNI bàn phím tiếng Pháp
  và các kiểu kết hợp Telex + VNI, Telex + VNI + VIQR.
- Xuất Unicode dựng sẵn, Unicode tổ hợp hoặc bảng mã cũ như TCVN3 (ABC), VNI
  Windows, VISCII, VPS, Windows 1258.
- Đặt dấu kiểu mới như `oà`, `uý` hoặc kiểu cũ như `òa`, `úy`.

**Chế độ hiển thị**
- **Rewrite trực tiếp**: chữ hiện thẳng trong ô nhập, không preedit. Đây là chế
  độ mặc định.
- **Preedit**: từ đang gõ được gạch chân cho tới khi kết thúc từ.
- **Redirect (EN)**: chuyển tiếp phím nguyên bản.
- Chọn chế độ riêng cho từng ứng dụng bằng phím tắt, mặc định là phím dấu huyền.

**Tự chọn backend sửa chữ theo ứng dụng**
- Surrounding text khi ứng dụng hỗ trợ xoá qua giao thức input method.
- Backspace hoặc Shift+Left phát qua uinput, Libei/XTest hoặc ForwardKey khi
  ứng dụng không hỗ trợ surrounding text.
- Xử lý riêng ô URL, thanh địa chỉ và gợi ý tự hoàn thành của trình duyệt.
- Tự nhận ra ứng dụng đang focus qua IBus và AT-SPI. Có thể ghi đè chế độ và
  backend cho từng ứng dụng, hoặc bấm phím tắt để chọn ngay trên ứng dụng đang
  dùng.
- Tự tăng thời gian chờ khi máy bận để tránh mất hoặc lặp chữ.

**Gõ thông minh**
- Kiểm tra chính tả: khôi phục từ gốc ngay trong lúc gõ hoặc sau khi gõ xong.
- Khôi phục lỗi chính tả khi nhấn Backspace.
- Sửa lại từ đã gõ xong khi đưa con trỏ về sau từ đó.
- Tự viết hoa sau dấu kết câu.
- Macro gõ tắt, tự đổi hoa thường theo từ viết tắt, hoàn tác bằng Backspace và
  giữ một phím bổ trợ khi nhấn Space để tạm bỏ qua macro.
- Tự reset sau khi click chuột sang vị trí khác.
- Tuỳ chọn tắt tiếng Việt trong terminal.

**Cài đặt**
- Ứng dụng cài đặt GTK riêng `ibus-areca-settings`.
- Menu Areca trên panel IBus: kiểu gõ, bảng mã, kiểm tra chính tả, kiểu đặt dấu,
  macro, tự viết hoa và mở trang cài đặt. Biểu tượng khay đổi được trong ứng
  dụng cài đặt.

## Cài đặt nhanh

```bash
curl -fsSL https://raw.githubusercontent.com/xhkzeroone/ibus-areca/main/install.sh | bash
```

Installer tải gói dựng sẵn từ release mới nhất cho Ubuntu, Linux Mint, Pop!_OS,
Zorin OS, Arch Linux và các bản dựa trên Arch, Fedora, Nobara, Bazzite,
openSUSE Tumbleweed; cấp quyền uinput nếu cần, thêm Areca vào danh sách bộ gõ
của IBus (và nguồn nhập của GNOME) rồi khởi động lại IBus. Mọi thao tác cấp quyền
và reboot đều hỏi xác nhận trước.

Repo chưa có release hoặc bản phân phối chưa có gói dựng sẵn thì build từ mã
nguồn:

```bash
git clone --recurse-submodules https://github.com/xhkzeroone/ibus-areca.git
cd ibus-areca
./scripts/install.sh
```

Script tự cài thư viện cần thiết (Arch, Debian/Ubuntu, Fedora, openSUSE), build,
chạy test, cài vào `/usr`, cấp quyền uinput và làm mới cache của IBus.

Sau đó:

1. Thiết lập biến môi trường cho desktop đang dùng theo mục
   [Thiết lập desktop](#3-thiết-lập-desktop).
2. Đăng xuất rồi đăng nhập lại.
3. Thêm **Areca** vào IBus theo mục [Thêm Areca vào IBus](#4-thêm-areca-vào-ibus).

## Cài đặt chi tiết

### 1. Cài IBus

```bash
# Ubuntu / Debian
sudo apt install ibus libibus-1.0-dev

# Arch Linux / CachyOS
sudo pacman -S ibus

# Fedora
sudo dnf install ibus ibus-devel
```

Bản phân phối khác: cài `ibus`, gói phát triển của libibus và module GTK/Qt của
IBus từ kho phần mềm.

### 2. Cài Areca

**Installer**, cần `curl` và `sudo`, cài gói dựng sẵn từ release mới nhất:

```bash
curl -fsSL https://raw.githubusercontent.com/xhkzeroone/ibus-areca/main/install.sh | bash
```

**Arch Linux qua AUR:**

```bash
# Bản build từ release
paru -S ibus-areca

# Bản binary dựng sẵn
paru -S ibus-areca-bin

# Bản mới nhất từ mã nguồn Git
paru -S ibus-areca-git
```

Có thể thay `paru` bằng `yay`.

**Từ mã nguồn:**

```bash
git clone --recurse-submodules https://github.com/xhkzeroone/ibus-areca.git
cd ibus-areca
./scripts/install.sh
```

Các tuỳ chọn hay dùng của `scripts/install.sh`:

| Tuỳ chọn | Ý nghĩa |
|---|---|
| `--skip-deps` | Không cài thư viện, dùng khi máy đã có đủ |
| `--skip-tests` | Không chạy test |
| `--user` | Cài vào `~/.local`, không cần sudo |
| `--system-update` | Trên Arch, chạy `pacman -Syu` trước khi cài thư viện |
| `--no-restart` | Không làm mới cache và khởi động lại IBus |

Cài vào `~/.local` thì IBus không tự thấy engine. Trước khi chạy `ibus-daemon`,
đặt:

```bash
export IBUS_COMPONENT_PATH="$HOME/.local/share/ibus/component:/usr/share/ibus/component"
ibus write-cache
```

### 3. Thiết lập desktop

Kiểm tra phiên đang dùng bằng:

```bash
echo "$XDG_CURRENT_DESKTOP / $XDG_SESSION_TYPE"
```

Hầu hết desktop dùng chung bộ biến môi trường sau. Bộ đầy đủ này giúp ứng dụng
Wayland native, XWayland, GTK, Qt và SDL đều nhận bộ gõ:

```ini
XMODIFIERS=@im=ibus
GTK_IM_MODULE=ibus
QT_IM_MODULE=ibus
SDL_IM_MODULE=ibus
```

Chỉ làm theo **một** mục phù hợp:

<details>
<summary><b>GNOME</b></summary>

GNOME đã tích hợp sẵn IBus, không cần đặt biến môi trường hay autostart. Thêm
Areca theo mục [Thêm Areca vào IBus](#4-thêm-areca-vào-ibus).

</details>

<details>
<summary><b>KDE Plasma Wayland</b></summary>

1. Mở **System Settings → Keyboard → Virtual Keyboard** và chọn **IBus Wayland**
   nếu có. KWin tự khởi động IBus, không cần autostart thêm.
2. Lưu bộ biến môi trường ở trên vào `~/.config/environment.d/ibus.conf`.

Nếu riêng một ứng dụng Wayland native bị nhấp nháy hoặc cửa sổ gợi ý sai vị trí,
chỉ bỏ biến cho ứng dụng đó:

```bash
env -u GTK_IM_MODULE ten-ung-dung
env -u QT_IM_MODULE ten-ung-dung
```

</details>

<details>
<summary><b>Hyprland</b></summary>

Thêm vào `~/.config/hypr/hyprland.conf`:

```ini
env = XMODIFIERS,@im=ibus
env = GTK_IM_MODULE,ibus
env = QT_IM_MODULE,ibus
env = SDL_IM_MODULE,ibus
exec-once = ibus-daemon -drx
```

Dùng UWSM hoặc đã bật XDG autostart thì bỏ dòng `exec-once` nếu IBus đã tự chạy,
để tránh chạy hai tiến trình `ibus-daemon`.

</details>

<details>
<summary><b>Niri</b></summary>

Thêm vào `~/.config/niri/config.kdl`:

```kdl
environment {
    XMODIFIERS "@im=ibus"
    GTK_IM_MODULE "ibus"
    QT_IM_MODULE "ibus"
    SDL_IM_MODULE "ibus"
}

spawn-at-startup "ibus-daemon" "-drx"
```

</details>

<details>
<summary><b>Sway và Wayland WM khác</b></summary>

1. Lưu bộ biến môi trường vào `~/.config/environment.d/ibus.conf`.
2. Thêm `exec ibus-daemon -drx` vào cấu hình Sway, hoặc lệnh khởi động tương
   đương của compositor.

</details>

<details>
<summary><b>Phiên X11</b></summary>

Thêm vào `~/.xprofile` hoặc file khởi động phiên X11 tương đương:

```bash
export XMODIFIERS=@im=ibus
export GTK_IM_MODULE=ibus
export QT_IM_MODULE=ibus
export SDL_IM_MODULE=ibus
ibus-daemon -drx
```

Desktop đã tự khởi động IBus thì bỏ dòng cuối.

</details>

Sau khi đổi biến môi trường, đăng xuất rồi đăng nhập lại. Khởi động lại máy là
cách chắc chắn nhất để mọi ứng dụng nhận cấu hình mới.

### 4. Thêm Areca vào IBus

- **GNOME:** **Settings → Keyboard → Input Sources → +**, chọn **Vietnamese**
  rồi **Areca**.
- **Desktop khác:** chạy `ibus-setup`, vào tab **Input Method**, bấm **Add**,
  chọn **Vietnamese** rồi **Areca**.
- Hoặc chuyển ngay bằng lệnh:

  ```bash
  ibus engine areca
  ```

### Thử mà không đổi bộ gõ hệ thống

Đang dùng bộ gõ khác làm mặc định vẫn thử được Areca trên một ứng dụng:

```bash
ibus-daemon -drx
ibus engine areca
GTK_IM_MODULE=ibus gnome-text-editor
```

Với ứng dụng Qt, dùng `QT_IM_MODULE=ibus kwrite`.

## Cấu hình

- Mở **Cài đặt Areca** từ menu ứng dụng hoặc chạy `ibus-areca-settings`.
- Hoặc chọn **Cài đặt Areca...** trong menu Areca trên panel IBus.

Các nhóm cấu hình chính:

| Nhóm | Nội dung |
|---|---|
| Chung | Kiểu gõ, bảng mã, kiểm tra chính tả, kiểu đặt dấu, tự viết hoa, phím chọn chế độ cho ứng dụng |
| Macro | Bật tắt macro, bảng từ viết tắt, hoàn tác bằng Backspace, phím bỏ qua macro |
| Theo ứng dụng | Ghi đè chế độ và backend cho từng ứng dụng |
| Nâng cao | Thời gian chờ của từng backend, chọn uinput hay Libei/XTest, log debug |

Cấu hình được lưu ở `~/.config/ibus-areca/conf/areca*.conf`. Engine tự nạp lại
khi các file này thay đổi, không cần khởi động lại IBus.

## uinput và quyền truy cập

Backend Backspace và Shift+Left dùng `/dev/uinput`. `scripts/install.sh` đã
thiết lập sẵn. Nếu Areca báo không khởi tạo được uinput:

```bash
# Nạp module ngay
sudo modprobe uinput

# Tự nạp mỗi lần khởi động
echo uinput | sudo tee /etc/modules-load.d/areca-uinput.conf

# Áp dụng udev rule Areca đã cài
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Nếu vẫn chưa có quyền ghi, thêm tài khoản vào nhóm `input` rồi đăng xuất và đăng
nhập lại:

```bash
sudo usermod -aG input "$USER"
```

Kiểm tra:

```bash
lsmod | grep '^uinput'
ls -l /dev/uinput
test -w /dev/uinput && echo "uinput: OK" || echo "uinput: chưa có quyền ghi"
```

Cuối cùng khởi động lại IBus để Areca thử mở uinput lại:

```bash
ibus restart
```

Nhóm `input` cho quyền truy cập thiết bị đầu vào ở mức thấp. Không muốn cấp quyền
này thì bỏ qua bước `usermod`, Areca sẽ tự dùng backend dự phòng.

## Xử lý sự cố

### Không gõ được

```bash
pgrep -a ibus-daemon
ibus engine
ibus list-engine | grep -i areca
printenv | grep -E '^(XMODIFIERS|GTK_IM_MODULE|QT_IM_MODULE|SDL_IM_MODULE)='
```

- Chỉ được có một tiến trình `ibus-daemon`.
- `ibus engine` phải in ra `areca` khi đang gõ tiếng Việt.
- `ibus list-engine` không thấy Areca thì chạy `ibus write-cache` rồi
  `ibus restart`.
- Không chạy IBus và một bộ gõ khác cùng lúc cho một ứng dụng.
- Biến môi trường trống nghĩa là chưa đăng nhập lại sau khi cấu hình.

### Mất chữ, lặp chữ hoặc dấu sai trên một ứng dụng

1. Đặt con trỏ vào ứng dụng đó và bấm phím tắt chọn backend để thử backend khác.
2. Nếu một backend chạy ổn, lưu lại trong mục cấu hình theo ứng dụng.
3. Nếu không backend nào ổn, gửi issue kèm log debug.

### Lấy log debug

1. Bật **Bật log debug Areca** trong cấu hình nâng cao, hoặc đặt `Debug=True`
   trong `~/.config/ibus-areca/conf/areca.conf`.
2. Chạy IBus ngay trong terminal để thấy log của engine:

   ```bash
   ibus-daemon -rxv
   ```

3. Chỉ lấy các dòng quan trọng:

   ```bash
   ibus-daemon -rxv 2>&1 | grep --line-buffered -E 'areca.*(rewrite select backend|reliability|probe|surrounding restore|reset rejected|uinput|failed|timeout)'
   ```

## Build từ mã nguồn

Cần bộ biên dịch C++20, CMake, Ninja, Go và gói phát triển của IBus, GLib,
xkbcommon, libinput, libudev, DBus, X11, Xtst. libei và libportal là tuỳ chọn,
dùng cho backend Native trên Wayland; thiếu thì Areca dùng XTest. Ứng dụng cài
đặt cần Python 3, PyGObject và GTK 4.

```bash
git clone --recurse-submodules https://github.com/xhkzeroone/ibus-areca.git
cd ibus-areca
```

Dùng script có sẵn để build, cài và làm mới IBus:

```bash
./scripts/install.sh
```

Hoặc làm từng bước:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
ctest --test-dir build --output-on-failure

sudo cmake --install build
sudo udevadm control --reload-rules
sudo udevadm trigger
ibus write-cache
ibus restart
```

Máy chưa có libibus vẫn build và chạy test được phần lõi; chỉ riêng
`ibus-engine-areca` cần libibus.

Backend uinput chưa chạy thì xem mục
[uinput và quyền truy cập](#uinput-và-quyền-truy-cập).

## Gỡ cài đặt

```bash
./scripts/uninstall.sh
```

Script gỡ engine, ứng dụng cài đặt, biểu tượng, udev rule và cấu hình trong
`~/.config/ibus-areca`. Thêm `--keep-data` để giữ cấu hình, `--user` nếu đã cài
vào `~/.local`. IBus và các bộ gõ khác không bị động tới.

## Đóng góp

Mọi báo lỗi, ý tưởng và Pull Request đều được hoan nghênh. Trước khi gửi PR,
hãy đọc [CONTRIBUTING.md](CONTRIBUTING.md), đặc biệt mục quy tắc bắt buộc:

- Mỗi PR chỉ giải quyết một vấn đề.
- Format bằng `git clang-format main`.
- Build sạch, `ctest` qua toàn bộ, có test cho thay đổi.
- Không chặn main loop của engine.
- Gõ thử trên ứng dụng thật và ghi kết quả vào PR.

## Tài liệu

- [Website giới thiệu Areca](https://xhkzeroone.github.io/ArecaIME/)
- [Kiến trúc trực quan](https://xhkzeroone.github.io/ArecaIME/architecture.html)
- [Tài liệu kiến trúc](https://github.com/xhkzeroone/ArecaIME/blob/main/docs/ARCHITECTURE.md)
- [Đặc tả kỹ thuật](https://github.com/xhkzeroone/ArecaIME/blob/main/docs/ARECA_ARCHITECTURE_SPECIFICATION.md)
- [Thời gian chờ thích nghi](https://github.com/xhkzeroone/ArecaIME/blob/main/docs/ADAPTIVE_DELAYS.md)
- [Hướng dẫn debug](https://github.com/xhkzeroone/ArecaIME/blob/main/docs/DEBUGGING.md)

Các tài liệu này nằm ở repo Areca gốc. Hàng đợi phím, backend sửa chữ, cách chọn
backend và restore trong ibus-areca giống hệt Areca gốc.

## Star History

[![Biểu đồ Star History](https://api.star-history.com/svg?repos=xhkzeroone/ibus-areca&type=Date)](https://star-history.com/#xhkzeroone/ibus-areca&Date)

## Ghi nhận

Xin cảm ơn những người đã đóng góp trực tiếp cho Areca:

<table>
  <tr>
    <th>Thành viên</th>
    <th>Đóng góp</th>
  </tr>
  <tr>
    <td align="center" width="160">
      <a href="https://github.com/kimxuanhong">
        <img src="https://github.com/kimxuanhong.png?size=160" width="80" alt="Kim Xuân Hồng"><br>
        <b>Kim Xuân Hồng</b>
      </a><br>
      <sub>@kimxuanhong · @xhkzeroone</sub>
    </td>
    <td>
      Tác giả và người phát triển chính.
      <ul>
        <li>Kiến trúc bộ gõ, hàng đợi phím và chế độ Rewrite, Preedit</li>
        <li>Các backend sửa chữ: surrounding text, uinput, Shift+Left, Libei/XTest, ForwardKey</li>
        <li>Nhận diện ứng dụng qua AT-SPI, xử lý trình duyệt và thanh địa chỉ</li>
        <li>Sửa từ đã commit, thời gian chờ thích nghi</li>
        <li>Ứng dụng cài đặt GTK, installer, gói phân phối và website</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td align="center" width="160">
      <a href="https://github.com/justanoobcoder">
        <img src="https://github.com/justanoobcoder.png?size=160" width="80" alt="Nguyễn Hồng Hiệp"><br>
        <b>Nguyễn Hồng Hiệp</b>
      </a><br>
      <sub>@justanoobcoder</sub>
    </td>
    <td>
      <ul>
        <li>Kiểm tra chính tả thời gian thực và lựa chọn chế độ chính tả</li>
        <li>Ứng dụng cài đặt bản đầu: tab giao diện, tìm kiếm macro, các tuỳ chọn mới</li>
        <li>Macro tự viết hoa theo từ viết tắt và hoàn tác macro bằng Backspace</li>
        <li>Xử lý tên frontend khác nhau giữa các môi trường</li>
        <li>Hướng dẫn cài trên NixOS, cấu hình CI cho settings</li>
      </ul>
    </td>
  </tr>
</table>

Danh sách đầy đủ xem tại
[trang contributors](https://github.com/xhkzeroone/ibus-areca/graphs/contributors).

Xin chân thành cảm ơn các anh, chị, em trong cộng đồng đã dành thời gian trải
nghiệm Areca, báo lỗi, gửi log và chia sẻ những ý tưởng quý báu. Mỗi phản hồi
đều là một đóng góp thiết thực, giúp Areca ngày càng ổn định và hoạt động tốt
hơn trên nhiều ứng dụng, môi trường desktop và bản phân phối Linux.

Areca được xây dựng trên nền tảng
[Bamboo Engine / bamboo-core](https://github.com/BambooEngine/bamboo-core),
thư viện xử lý tiếng Việt do **Lương Thành Lâm** phát triển và chia sẻ với cộng
đồng dưới giấy phép MIT. Dự án xin gửi lời cảm ơn chân thành đến anh vì đã tạo
ra một nền tảng vững chắc, giúp Areca có thể tập trung hoàn thiện trải nghiệm
gõ tiếng Việt trên Linux và Wayland.

Areca cũng trân trọng ghi nhận những công trình đặt nền móng cho Bamboo Engine:
**Trung Ngo**, tác giả `bogo.js`, và **Trần Kỳ Nam**, tác giả `GoTiengViet`.
Những đóng góp của các tác giả đã giúp hệ sinh thái bộ gõ tiếng Việt nguồn mở
ngày càng phong phú và tạo điều kiện để các dự án như Areca tiếp tục phát triển.

[Mã nguồn Bamboo Core](https://github.com/BambooEngine/bamboo-core) cùng
[thông tin giấy phép MIT nguyên bản](https://github.com/BambooEngine/bamboo-core?tab=MIT-1-ov-file)
được giữ nguyên trong submodule `bamboo/bamboo-core` để thể hiện đầy đủ nguồn
gốc và sự ghi nhận đối với các tác giả.

Xin cảm ơn các dự án bộ gõ tiếng Việt đã cung cấp nhiều ý tưởng và nguồn tham
khảo hữu ích trong quá trình phát triển Areca:

- [VMK — Vietnamese Micro Key](https://github.com/thanhpy2009/VMK)
- [Fcitx5 Lotus](https://github.com/LotusInputMethod/fcitx5-lotus)
- [SKey / fcitx5-skey](https://github.com/collyn/skey)

## Giấy phép

- Areca được phát hành theo giấy phép [MIT](LICENSE).
- Thư viện [Bamboo Core](https://github.com/BambooEngine/bamboo-core) được phát
  hành theo [giấy phép MIT](https://github.com/BambooEngine/bamboo-core?tab=MIT-1-ov-file).

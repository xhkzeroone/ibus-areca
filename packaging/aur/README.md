# Publish ibus-areca lên AUR

Thư mục này giữ công thức cho ba package AUR. Không dùng
`packaging/arch/PKGBUILD`: file đó nhận source tarball do GitHub Actions tạo
trước và chỉ phục vụ job CI.

| Package | Nguồn | Khi nào cập nhật |
|---|---|---|
| `ibus-areca` | Build từ tag `v$pkgver` | Mỗi release |
| `ibus-areca-bin` | Gói `.pkg.tar.zst` dựng sẵn trong GitHub Release | Mỗi release, sau khi workflow đóng gói chạy xong |
| `ibus-areca-git` | Build từ nhánh `main` | Chỉ khi đổi dependency hay cách build; `pkgver()` tự lấy commit mới |

Ba package `provides` cùng tên `ibus-areca` và `conflicts` lẫn nhau, nên người
dùng chỉ cài được một bản.

## Chuẩn bị tài khoản

1. Tạo tài khoản tại <https://aur.archlinux.org/register>.
2. Thêm public SSH key trong trang **My Account** của AUR.
3. Kiểm tra kết nối:

   ```bash
   ssh aur@aur.archlinux.org help
   ```

## Publish lần đầu

`ibus-areca` và `ibus-areca-bin` cần tag GitHub tương ứng, và `-bin` cần thêm
gói Arch trong release của tag đó:

```bash
git ls-remote --tags https://github.com/xhkzeroone/ibus-areca.git v0.1.0
```

Với mỗi package, clone repository AUR rỗng, chép `PKGBUILD` và `.SRCINFO` rồi
build thử. Ví dụ với `ibus-areca-git`:

```bash
git -c init.defaultBranch=master clone \
  ssh://aur@aur.archlinux.org/ibus-areca-git.git aur-ibus-areca-git
cd aur-ibus-areca-git
cp /path/to/ibus-areca/packaging/aur/ibus-areca-git/PKGBUILD .
cp /path/to/ibus-areca/packaging/aur/ibus-areca-git/.SRCINFO .
makepkg --syncdeps --cleanbuild --check
namcap PKGBUILD ibus-areca-git-*.pkg.tar.zst
git add PKGBUILD .SRCINFO
git commit -m "Initial import"
git push origin master
```

Với `ibus-areca-bin`, chạy `updpkgsums` để điền checksum của gói release trước
khi sinh `.SRCINFO`.

AUR chỉ nhận metadata và công thức build, không nhận file `.pkg.tar.zst` vừa
build.

## Cập nhật phiên bản

Sau khi tạo tag GitHub mới và workflow đóng gói chạy xong:

1. Trong `ibus-areca` và `ibus-areca-bin`, sửa `pkgver`, đặt lại `pkgrel=1`.
2. Nếu tag đổi Bamboo, cập nhật `_bamboo_commit` trong `ibus-areca` và
   `ibus-areca-git`:

   ```bash
   git ls-tree vX.Y.Z bamboo/bamboo-core
   ```

3. Với `ibus-areca-bin`, chạy `updpkgsums`.
4. Sinh lại `.SRCINFO`, build thử và push repository AUR:

   ```bash
   makepkg --printsrcinfo > .SRCINFO
   makepkg --syncdeps --cleanbuild --check
   git add PKGBUILD .SRCINFO
   git commit -m "Update to X.Y.Z"
   git push
   ```

Không được quên `.SRCINFO`; AUR dùng file này để hiển thị version và dependency.

# ibus-unikey

Fork của [ibus-unikey](https://github.com/vn-input/ibus-unikey) — bộ gõ tiếng Việt cho IBus (Telex, VNI, STelex). Đã fix lỗi `Win+Space` bị chặn khi đang gõ dở và tắt gạch chân preedit.

> Upstream không còn phát triển. Repo này chỉ duy trì bản build cho Ubuntu/Debian hiện đại.

## Cài đặt

### Cách nhanh (khuyên dùng)

```bash
git clone https://github.com/mwarevn/ibus-unikey.git && cd ibus-unikey
./scripts/setup.sh -y
```

Script tự: cài deps, build, `cmake --install`, `glib-compile-schemas`, thêm `Unikey` vào `preload-engines`, cấu hình env (`~/.xinputrc`, `~/.profile`), `ibus restart`. Sau đó **logout/login** một lần.

Tùy chọn:

```bash
./scripts/setup.sh --build-only -y   # chỉ build + install, không đụng config
./scripts/setup.sh --setup-only -y   # chỉ setup ibus/gsettings/env
./scripts/setup.sh --uninstall       # gỡ (giữ deps)
```

### Cài thủ công

```bash
sudo apt-get install -y cmake g++ make pkg-config libibus-1.0-dev libgtk-3-dev gettext

cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build -j$(nproc)
sudo cmake --install build
sudo glib-compile-schemas /usr/share/glib-2.0/schemas

ibus restart
ibus-setup   # tab Input Method -> Add -> Vietnamese - Unikey
im-config -n ibus   # khi hỏi user configuration, chọn ibus (ghi ~/.xinputrc)
```

Sau đó logout/login, chọn **Unikey** trong tray/input source.

### Cài từ file .deb (build sẵn)

Build trên máy (tạo `build/ibus-unikey_0.7.0_amd64.deb`):

```bash
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build -j$(nproc)
cpack --config build/CPackConfig.cmake -G DEB -B build
# hoặc: cmake --build build --target package  (alias của cpack)
```

Cài / gỡ:

```bash
sudo apt install ./build/ibus-unikey_0.7.0_amd64.deb
# postinst tự chạy glib-compile-schemas; sau đó:
ibus restart   # rồi logout/login 1 lần

sudo apt remove ibus-unikey
```

## Sử dụng

- **Chọn bộ gõ:** `ibus-setup-unikey` hoặc icon Unikey trên panel → `More settings...`
  - Input method: `Telex` (mặc định) / `VNI` / `STelex` / `STelex2`
  - Charset: `Unicode` (mặc định)
- **Gõ tiếng Việt:** gõ như bình thường, từ được commit khi gặp dấu `space` / dấu câu (`, . ; :`...). `Backspace` sửa dấu.
- **Đổi EN <-> VI:**
  - Bấm `Shift+Shift` (double tap), hoặc `Shift+Space`, hoặc `Ctrl+Space`
  - Hoặc bấm toggle `EN/VI` trên panel IBus
- **Win+Space** (đổi layout hệ thống) không bị nuốt khi đang gõ dở — preedit sẽ được commit trước khi chuyển.

## Gỡ cài đặt

```bash
./scripts/setup.sh --uninstall
# hoặc thủ công: sudo cmake --install build --prefix /usr (không có uninstall target,
# xóa tay: /usr/lib/ibus/ibus-engine-unikey, /usr/lib/ibus/ibus-setup-unikey,
#          /usr/share/ibus/component/unikey.xml, /usr/share/glib-2.0/schemas/org.freedesktop.ibus.engine.unikey.gschema.xml)
sudo glib-compile-schemas /usr/share/glib-2.0/schemas
ibus restart
```

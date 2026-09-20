# PIXEL PRO – Lumi Macropad (ESP32-S2 Mini)

Firmware thử phần cứng đầu tiên cho ESP32-S2 Mini + ILI9486 3.5 inch 480x320 8-bit parallel + 8 phím + EC11.

## Matrix chốt gọn nhất

8 phím dùng 2 ROW x 4 COL = 6 chân MCU. Đây là matrix chuẩn, vẫn cho phép bấm nhiều phím đồng thời khi mỗi switch có diode.

### LCD ILI9486

| Màn ILI9486 | ESP32-S2 Mini |
|---|---|
| LCD_D0 | D1 |
| LCD_D1 | D2 |
| LCD_D2 | D3 |
| LCD_D3 | D4 |
| LCD_D4 | D5 |
| LCD_D5 | D6 |
| LCD_D6 | D7 |
| LCD_D7 | D8 |
| LCD_WR | D9 |
| LCD_RS / DC | D10 |
| LCD_CS | D11 |
| LCD_RST | D12 |
| LCD_RD | 3V3 |
| 5V | 5V |
| GND | GND |
| 3V3 trên shield | không nối |
| SD_SS/DI/DO/SCK | chưa nối ở v0.1 |

LCD_RD kéo thẳng lên 3V3 vì PIXEL PRO chỉ cần ghi framebuffer ra màn. Như vậy tiết kiệm thêm 1 chân.

### 8 phím – matrix 2x4

| Matrix | ESP32-S2 Mini |
|---|---|
| ROW0 | D13 |
| ROW1 | D14 |
| COL0 | D33 |
| COL1 | D34 |
| COL2 | D35 |
| COL3 | D36 |

Thứ tự: ROW0 = K1 K2 K3 K4, ROW1 = K5 K6 K7 K8.

Mỗi phím: COL -> SWITCH -> diode 1N4148 -> ROW. Đầu có vạch đen/cathode của diode quay về ROW.

### Encoder EC11

| EC11 | ESP32-S2 Mini |
|---|---|
| A | D37 |
| B | D38 |
| C / Common | GND |
| SW | D39 |
| chân còn lại của SW | GND |

Xoay mặc định Volume - / Volume +. Nhấn chuyển Profile 1 -> 2 -> 3 -> 4 -> 5 -> 1.

### RGB

D40 dành riêng cho Data WS2812. Firmware v0.1 chưa bật driver RGB; chân được giữ để không phải đi lại dây về sau.

### Chân còn trống

D15, D16, D17, D18, D21 để dành cho touch / SD / cảm biến sau này.

## USB / VIA / Lumi

Firmware dùng USB HID với Keyboard, Consumer/Media và Raw HID 32 byte tương thích VIA/QMK. Raw HID dùng Usage Page 0xFF60 và Usage 0x61.

Lumi protocol dùng cùng Raw HID với magic LQ, tương thích QmkRawHidLink trong Lumi Macropad app.

VID dev: 0x303A. PID dev: 0x4009.

VIA definition nằm tại via/pixel-pro-s2.json. Mở VIA, bật Design tab, Load Draft Definition rồi chọn JSON.

Firmware hỗ trợ 5 layer/profile, đọc/ghi keycode, bulk keymap buffer, encoder CW/CCW, reset dynamic keymap và jump bootloader. Keymap lưu NVS nên tắt nguồn không mất.

## Lumi Macropad App

HELLO: LUMIPAD|3|FW=0.1.0|CAPS=PROFILE,ACTION,PCMON,PANEL,MEM,SAVERSTATE

v0.1 đã có kết nối Lumi Raw HID, Profile, PC Monitor cơ bản trên LCD, Now Playing text cơ bản, PANEL/MEM/BAT và restart/bootloader/sleep/wake.

RGB/GIF upload đầy đủ sẽ thêm sau khi xác nhận đúng phần cứng LCD + USB + matrix.

## Build / nạp

GitHub Actions build cho board esp32:esp32:lolin_s2_mini bằng Arduino-ESP32 3.3.12.

Artifact pixel-pro-s2-firmware chứa file bin. Nếu build tạo file merged.bin thì nạp merged.bin tại offset 0x0. Hoặc mở firmware/PixelPro_S2/PixelPro_S2.ino bằng Arduino IDE và Upload.

## Lưu ý

Bản v0.1 ưu tiên bring-up phần cứng: màn, matrix, encoder, USB HID, VIA và Lumi handshake. Sau khi xác nhận màn đúng màu/chiều và 8 phím đúng thứ tự, mới bật touch/RGB/GIF để tránh debug nhiều phần cùng lúc.
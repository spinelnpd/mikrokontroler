# Pemantau Detak Jantung & SpO₂ — ESP32 + MAX30102

Project **Kelompok Spinel** untuk **Luminous Quest Mikrokontroler**, Teti Lab Skill (TLS) 2026.
Studi kasus **H-2: Pertolongan Darurat**.

Alat ini mengukur **detak jantung (BPM)** dan **kadar oksigen darah (SpO₂)** dari ujung jari menggunakan sensor MAX30102. Hasilnya ditampilkan di layar OLED, dan buzzer berbunyi setiap kali pembacaan valid. Prototipe ini menjadi dasar sistem darurat yang nantinya bisa menyalakan pompa oksigen secara otomatis.

## Link

- Video YouTube: <https://youtu.be/_ckiI6Cbu-k>
- Instagram: <https://www.instagram.com/p/Dd_mx3PhfYQ/>
- Simulasi Wokwi: <https://wokwi.com/projects/476776056645355521>

## Komponen

| Komponen | Jumlah |
|---|---|
| ESP32 DevKit | 1 |
| Sensor MAX30102 (MH-ET LIVE) | 1 |
| OLED 1.3" SH1106 128×64 (I2C) | 1 |
| Buzzer | 1 |
| Breadboard | 1 |
| Kabel jumper | secukupnya |
| Kabel data USB | 1 |

## Wiring

| Modul | Pin modul | Pin ESP32 |
|---|---|---|
| MAX30102 | VIN | 3V3 |
| MAX30102 | GND | GND |
| MAX30102 | SDA | GPIO 21 |
| MAX30102 | SCL | GPIO 22 |
| OLED SH1106 | VCC | 3V3 |
| OLED SH1106 | GND | GND |
| OLED SH1106 | SDA | GPIO 21 |
| OLED SH1106 | SCL | GPIO 22 |
| Buzzer | + | GPIO 13 |
| Buzzer | − | GND |

MAX30102 (alamat 0x57) dan OLED (alamat 0x3C) berbagi jalur I2C yang sama.

## Struktur repository

```
pemantau_detak_jantung/pemantau_detak_jantung.ino   kode untuk alat asli (Arduino IDE)
simulasi-wokwi/sketch.ino                           kode versi simulasi (OLED SSD1306)
simulasi-wokwi/diagram.json                         rangkaian simulasi
simulasi-wokwi/max30102.chip.c / .chip.json         custom chip MAX30102 untuk Wokwi
simulasi-wokwi/libraries.txt                        library yang dipakai
```

## Menjalankan di alat asli

1. Install **Arduino IDE** dan board **ESP32** (Boards Manager → "esp32" by Espressif).
2. Install library: **SparkFun MAX3010x Pulse and Proximity Sensor Library** dan **U8g2**.
3. Buka `pemantau_detak_jantung/pemantau_detak_jantung.ino`, pilih board **ESP32 Dev Module**, lalu upload.
4. Tempelkan jari pada sensor. OLED akan menampilkan `Letakkan Jari` → `Measuring...` → `BPM` & `SpO2`.

## Menjalankan simulasi Wokwi

Wokwi belum punya part MAX30102, jadi sensor ini dibuat sebagai **custom chip** (`max30102.chip.c`). Chip ini meniru register I2C MAX30102 yang dibaca library SparkFun dan menghasilkan sinyal PPG sintetis.

1. Buka <https://wokwi.com/projects/new/esp32>.
2. Ganti isi `sketch.ino` dan `diagram.json` dengan file dari folder `simulasi-wokwi/`.
3. Tambahkan file baru (tombol ▾ di samping tab file → *New file*): `libraries.txt`, `max30102.chip.json`, dan `max30102.chip.c`, lalu isi sesuai file di folder ini.
4. Klik ▶ **Start**. Klik chip MAX30102 untuk mengatur slider:
   - **Jari di sensor**: 0 = OLED menampilkan `Letakkan Jari`, 1 = pengukuran berjalan
   - **Detak jantung (BPM)**: 40–180
   - **SpO2 (%)**: 85–100
5. Simpan project (**Save**) lalu salin link-nya ke bagian *Link* di atas.

Catatan: di simulasi OLED memakai driver SSD1306 karena Wokwi tidak punya SH1106. Ukuran dan koneksinya sama.

## Cara kerja

1. Program mengambil 100 sampel cahaya merah dan inframerah dari MAX30102 (25 sampel/detik, ±4 detik).
2. Fungsi `maxim_heart_rate_and_oxygen_saturation()` menghitung BPM dan SpO₂ dari sampel tersebut.
3. Jika nilai IR < 50000 berarti tidak ada jari, dan OLED menampilkan `Letakkan Jari`.
4. Jika data valid, buzzer berbunyi (1 kHz, 30 ms) lalu BPM dan SpO₂ tampil di OLED. Jika belum valid, OLED menampilkan `Measuring...`.

## Anggota Kelompok Spinel

Azmi Zulfa Hakim · Barlian Muhammad Vesile · Bonifacio Marvel Wahyudi · Cece Nafhah Rahmadini · Dalton Gustiawan Palungan · Dhafin Athaya Rista · Fakhri Muzakki Hidayat · Firdaus Muhammad Faris · M. Zufar Alfa Rizki · Muhammad Avicena Fathir Athaya · Muhammad Fadhli Aulia Rahman · Muhammad Farras Rajendra Rakha · Muhammad Hafidz Ar-Rahman · Naura Hanania Nadhir · Razan Aqil Fata · Shada Alkhar Winnyan

## Referensi

- Library SparkFun MAX3010x — <https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library>
- Library U8g2 — <https://github.com/olikraus/u8g2>
- Datasheet MAX30102 — <https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf>
- Dokumentasi custom chip Wokwi — <https://docs.wokwi.com/chips-api/getting-started>

// Wokwi Custom Chip: MAX30102 (sensor detak jantung & SpO2) - versi simulasi
// Kelompok Spinel - Luminous Quest Mikrokontroler
//
// Chip ini meniru register I2C MAX30102 yang dipakai library SparkFun MAX3010x:
// PART_ID, MODE_CONFIG, FIFO_CONFIG, SPO2_CONFIG, pointer FIFO, dan FIFO_DATA.
// Sinyal PPG (cahaya merah & inframerah) dibuat sintetis dan bisa diatur lewat
// slider di simulator: jari di sensor (0/1), detak jantung (BPM), dan SpO2 (%).

#include "wokwi-api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define I2C_ADDRESS 0x57

#define REG_FIFO_WR_PTR 0x04
#define REG_OVF_COUNTER 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_TEMP_INT 0x1F
#define REG_TEMP_FRAC 0x20
#define REG_REV_ID 0xFE
#define REG_PART_ID 0xFF

#define PART_ID 0x15
#define FIFO_DEPTH 32
#define MAX_18BIT 262143

typedef struct {
  uint8_t regs[256];
  uint8_t reg_ptr;
  bool expect_reg_ptr;
  uint32_t fifo[FIFO_DEPTH][3]; // [sampel][red, ir, green]
  uint8_t wr_ptr;
  uint8_t rd_ptr;
  uint8_t byte_idx; // posisi byte di sampel yang sedang dibaca
  double phase;     // fase siklus jantung 0..1
  double t;         // waktu sinyal (detik)
  uint64_t next_sample_ns;
  uint32_t seed;
  uint32_t attr_finger;
  uint32_t attr_bpm;
  uint32_t attr_spo2;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);
static void on_timer(void *user_data);

static void reset_registers(chip_state_t *chip) {
  memset(chip->regs, 0, sizeof(chip->regs));
  chip->regs[REG_TEMP_INT] = 25;
  chip->wr_ptr = 0;
  chip->rd_ptr = 0;
  chip->byte_idx = 0;
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip, 0, sizeof(chip_state_t));
  reset_registers(chip);
  chip->seed = 12345;
  chip->attr_finger = attr_init("finger", 1);
  chip->attr_bpm = attr_init_float("bpm", 75.0f);
  chip->attr_spo2 = attr_init_float("spo2", 98.0f);

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = I2C_ADDRESS,
    .scl = pin_init("SCL", INPUT),
    .sda = pin_init("SDA", INPUT),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };
  i2c_init(&i2c_config);

  const timer_config_t timer_config = {
    .user_data = chip,
    .callback = on_timer,
  };
  timer_t timer = timer_init(&timer_config);
  timer_start(timer, 1000, true); // cek tiap 1 ms apakah sudah waktunya sampel baru

  printf("MAX30102 (simulasi) siap di alamat 0x%02X\n", I2C_ADDRESS);
}

// ---------- Konfigurasi dari register ----------

static int active_leds(chip_state_t *chip) {
  switch (chip->regs[REG_MODE_CONFIG] & 0x07) {
    case 0x02: return 1; // red only
    case 0x03: return 2; // red + IR (mode SpO2)
    case 0x07: return 3; // multi-LED
    default: return 0;   // belum dikonfigurasi / shutdown
  }
}

// Laju sampel efektif = sample rate / sample averaging
static double sample_rate(chip_state_t *chip) {
  static const double rates[8] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
  int sr = (chip->regs[REG_SPO2_CONFIG] >> 2) & 0x07;
  int avg_code = (chip->regs[REG_FIFO_CONFIG] >> 5) & 0x07;
  if (avg_code > 5) avg_code = 5;
  return rates[sr] / (double)(1 << avg_code);
}

// ---------- Pembuat sinyal PPG ----------

static double noise(chip_state_t *chip) {
  chip->seed = chip->seed * 1664525u + 1013904223u;
  return ((chip->seed >> 8) / 16777216.0) * 2.0 - 1.0;
}

// Bentuk denyut: naik cepat (sistolik), turun perlahan; satu puncak per detak, nilai 0..1
static double pulse_shape(double ph) {
  if (ph < 0.15) return 0.5 - 0.5 * cos(M_PI * ph / 0.15);
  return exp(-(ph - 0.15) / 0.2);
}

// Rasio R (red/IR) yang menghasilkan SpO2 tertentu menurut tabel algoritma Maxim:
// SpO2 = -45.060 R^2 + 30.354 R + 94.845  ->  ambil akar yang lebih besar
static double ratio_for_spo2(double spo2) {
  if (spo2 > 99.9) spo2 = 99.9;
  double a = -45.060, b = 30.354, c = 94.845 - spo2;
  double disc = b * b - 4 * a * c;
  if (disc < 0) disc = 0;
  return (-b - sqrt(disc)) / (2 * a);
}

static uint32_t clamp18(double v) {
  if (v < 0) return 0;
  if (v > MAX_18BIT) return MAX_18BIT;
  return (uint32_t)v;
}

static void push_sample(chip_state_t *chip, double rate) {
  bool finger = attr_read(chip->attr_finger) != 0;
  double bpm = attr_read_float(chip->attr_bpm);
  double spo2 = attr_read_float(chip->attr_spo2);
  double dt = 1.0 / rate;
  chip->t += dt;
  chip->phase += bpm / 60.0 * dt;
  chip->phase -= floor(chip->phase);

  double red, ir;
  if (finger) {
    double ir_dc = 120000 + 150 * sin(chip->t * 0.6); // sedikit drift baseline
    double red_dc = 100000 + 120 * sin(chip->t * 0.6);
    double ir_ac = 1600;
    double red_ac = ratio_for_spo2(spo2) * ir_ac * red_dc / ir_dc;
    double p = pulse_shape(chip->phase);
    ir = ir_dc - ir_ac * p + 25 * noise(chip);
    red = red_dc - red_ac * p + 25 * noise(chip);
  } else {
    ir = 1800 + 150 * noise(chip); // tanpa jari: pantulan sangat kecil
    red = 1500 + 150 * noise(chip);
  }

  uint8_t next = (chip->wr_ptr + 1) % FIFO_DEPTH;
  if (next == chip->rd_ptr) {
    // FIFO penuh -> roll over: buang sampel tertua
    chip->rd_ptr = (chip->rd_ptr + 1) % FIFO_DEPTH;
    chip->regs[REG_OVF_COUNTER] = (chip->regs[REG_OVF_COUNTER] + 1) & 0x1F;
  }
  chip->fifo[chip->wr_ptr][0] = clamp18(red);
  chip->fifo[chip->wr_ptr][1] = clamp18(ir);
  chip->fifo[chip->wr_ptr][2] = 0;
  chip->wr_ptr = next;
}

static void on_timer(void *user_data) {
  chip_state_t *chip = user_data;
  uint64_t now = get_sim_nanos();
  if (active_leds(chip) == 0) {
    chip->next_sample_ns = now;
    return;
  }
  double rate = sample_rate(chip);
  uint64_t period = (uint64_t)(1e9 / rate);
  if (chip->next_sample_ns == 0) chip->next_sample_ns = now + period;
  int guard = 0;
  while (now >= chip->next_sample_ns && guard++ < 64) {
    push_sample(chip, rate);
    chip->next_sample_ns += period;
  }
}

// ---------- I2C ----------

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = user_data;
  if (!read) chip->expect_reg_ptr = true;
  chip->byte_idx = 0;
  return true; // ACK
}

static uint8_t read_fifo_byte(chip_state_t *chip) {
  int leds = active_leds(chip);
  if (leds == 0) leds = 1;
  int ch = chip->byte_idx / 3;
  int part = chip->byte_idx % 3;
  uint32_t v = chip->fifo[chip->rd_ptr][ch < 3 ? ch : 0];
  uint8_t out = part == 0 ? (v >> 16) & 0x03 : part == 1 ? (v >> 8) & 0xFF : v & 0xFF;
  chip->byte_idx++;
  if (chip->byte_idx >= leds * 3) {
    chip->byte_idx = 0;
    if (chip->rd_ptr != chip->wr_ptr) chip->rd_ptr = (chip->rd_ptr + 1) % FIFO_DEPTH;
  }
  return out;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = user_data;
  uint8_t reg = chip->reg_ptr;
  if (reg == REG_FIFO_DATA) return read_fifo_byte(chip);
  uint8_t value;
  switch (reg) {
    case REG_FIFO_WR_PTR: value = chip->wr_ptr; break;
    case REG_FIFO_RD_PTR: value = chip->rd_ptr; break;
    case REG_PART_ID: value = PART_ID; break;
    case REG_REV_ID: value = 0x03; break;
    default: value = chip->regs[reg]; break;
  }
  chip->reg_ptr++;
  return value;
}

static void write_register(chip_state_t *chip, uint8_t reg, uint8_t data) {
  switch (reg) {
    case REG_MODE_CONFIG:
      if (data & 0x40) {
        reset_registers(chip); // soft reset, bit RESET langsung kembali 0
        return;
      }
      chip->regs[reg] = data;
      break;
    case REG_FIFO_WR_PTR: chip->wr_ptr = data % FIFO_DEPTH; break;
    case REG_FIFO_RD_PTR:
      chip->rd_ptr = data % FIFO_DEPTH;
      chip->byte_idx = 0;
      break;
    case REG_PART_ID:
    case REG_REV_ID:
      break; // read-only
    default: chip->regs[reg] = data; break;
  }
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = user_data;
  if (chip->expect_reg_ptr) {
    chip->reg_ptr = data;
    chip->expect_reg_ptr = false;
    chip->byte_idx = 0;
    return true;
  }
  write_register(chip, chip->reg_ptr, data);
  if (chip->reg_ptr != REG_FIFO_DATA) chip->reg_ptr++;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  // tidak ada yang perlu dilakukan
}

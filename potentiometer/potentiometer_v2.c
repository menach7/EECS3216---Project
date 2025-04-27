#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/time.h"

// ─────────── Display constants ───────────
#define W        128
#define H         64
#define FB_LEN   (W * H / 8)
static uint8_t fb[FB_LEN];
#define OLED_ADDR 0x3C

// ─────────── I2C Pin Overrides ───────────
#undef  PICO_DEFAULT_I2C_SDA_PIN
#undef  PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 16
#define PICO_DEFAULT_I2C_SCL_PIN 17
#define i2c_default           i2c0

// ─────────── ADC channels ───────────
#define POT_LEFT_ADC   0   // GP26
#define POT_RIGHT_ADC  1   // GP27

// ─────────── Helper macros ───────────
#define DEG2RAD(x) ((x) * M_PI / 180.0f)

// ─────────── OLED Functions ───────────
static inline void oled_cmd(uint8_t c) {
    uint8_t buf[2] = {0x80, c};
    i2c_write_blocking(i2c_default, OLED_ADDR, buf, 2, false);
}

static void oled_cmds(const uint8_t *seq, size_t n) {
    while (n--) oled_cmd(*seq++);
}

static void oled_data(const uint8_t *d, size_t n) {
    uint8_t chunk[17] = {0x40};
    while (n) {
        size_t len = n > 16 ? 16 : n;
        memcpy(chunk + 1, d, len);
        i2c_write_blocking(i2c_default, OLED_ADDR, chunk, len + 1, false);
        d += len;
        n -= len;
    }
}

static void oled_init(void) {
    static const uint8_t init_seq[] = {
        0xAE,0x20,0x00,0x40,0xA1,0xA8,(H-1),0xC8,0xD3,0x00,
        0xDA,0x12,0xD5,0x80,0xD9,0xF1,0xDB,0x20,0x81,0xFF,
        0xA4,0xA6,0x8D,0x14,0x2E,0xAF
    };
    oled_cmds(init_seq, sizeof(init_seq));
    sleep_ms(50);
}

static void oled_refresh(void) {
    const uint8_t addr_seq[] = {0x21,0,W-1,0x22,0,(H/8)-1};
    oled_cmds(addr_seq, sizeof(addr_seq));
    oled_data(fb, FB_LEN);
}

static inline void px(int x, int y, bool on) {
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    uint16_t idx = (y >> 3) * W + x;
    uint8_t mask = 1u << (y & 7);
    fb[idx] = on ? (fb[idx] | mask) : (fb[idx] & ~mask);
}

// ─────────── Potentiometer Handling ───────────
static void adc_init_pots(void) {
    adc_init();
    adc_gpio_init(26); // POT_LEFT_ADC (GP26)
    adc_gpio_init(27); // POT_RIGHT_ADC (GP27)
}

static uint16_t read_pot_raw(uint pot_adc) {
    adc_select_input(pot_adc);
    return adc_read(); // 12-bit (0-4095)
}

static int map_pot_to_percent(uint16_t raw) {
    return (int)((raw / 4095.0f) * 100.0f);
}

// ─────────── Gauge Drawing ───────────
void clear_fb(void) {
    memset(fb, 0, FB_LEN);
}

void draw_semicircle(int cx, int cy, int radius) {
    for (int angle = -90; angle <= 90; angle += 3) {
        float rad = DEG2RAD(angle);
        int x = cx + (int)(radius * cosf(rad));
        int y = cy - (int)(radius * sinf(rad));
        px(x, y, true);
    }
}

void draw_needle(int cx, int cy, float angle_deg, int length) {
    float rad = DEG2RAD(angle_deg);
    int x_end = cx + (int)(length * cosf(rad));
    int y_end = cy - (int)(length * sinf(rad));
    int dx = abs(x_end - cx), sx = cx < x_end ? 1 : -1;
    int dy = -abs(y_end - cy), sy = cy < y_end ? 1 : -1;
    int err = dx + dy;
    int x = cx, y = cy;
    while (true) {
        px(x, y, true);
        if (x == x_end && y == y_end) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void render_gauge(int pot_left_val, int pot_right_val) {
    clear_fb();

    int cx = W / 2;
    int cy = H - 1;
    int radius = 28;

    draw_semicircle(cx, cy, radius);

    float angle_left = ((pot_left_val / 100.0f) * 180.0f) - 90.0f;
    float angle_right = ((pot_right_val / 100.0f) * 180.0f) - 90.0f;

    draw_needle(cx, cy, angle_left, radius - 2);
    draw_needle(cx, cy, angle_right, radius - 6);

    oled_refresh();
}

// ─────────── Prompts ───────────
bool is_aligned(int l, int r, int margin) {
    return abs(l - r) <= margin;
}

void prompt_1() {
    printf("Prompt 1: Sync back-and-forth 5 times.\n");
    int toggles = 0;
    bool moving_forward = true;
    int last_l = map_pot_to_percent(read_pot_raw(POT_LEFT_ADC));
    int last_r = map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC));

    while (toggles < 5) {
        int l = map_pot_to_percent(read_pot_raw(POT_LEFT_ADC));
        int r = map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC));
        render_gauge(l, r);

        if (l < 15 || l > 85 || r < 15 || r > 85) {
            printf("Out of bounds!\n");
            return;
        }

        if (is_aligned(l, r, 5)) {
            if ((moving_forward && l < last_l && r < last_r) ||
                (!moving_forward && l > last_l && r > last_r)) {
                toggles++;
                moving_forward = !moving_forward;
                printf("Toggle %d complete.\n", toggles);
            }
        }

        last_l = l;
        last_r = r;
        sleep_ms(50);
    }

    printf("Prompt 1 complete!\n");
}

void prompt_2() {
    printf("Prompt 2: Left toggles inside 30-70, right full toggles.\n");
    int left_toggles = 0;
    int right_toggles = 0;
    bool l_dir = true;
    bool r_dir = true;
    int last_l = map_pot_to_percent(read_pot_raw(POT_LEFT_ADC));
    int last_r = map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC));

    while (left_toggles < 10 || right_toggles < 2) {
        int l = map_pot_to_percent(read_pot_raw(POT_LEFT_ADC));
        int r = map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC));
        render_gauge(l, r);

        if (l < 30 || l > 70) {
            printf("Left pot out of bounds!\n");
            return;
        }

        if ((l_dir && l < last_l) || (!l_dir && l > last_l)) {
            l_dir = !l_dir;
            left_toggles++;
            printf("Left toggle %d\n", left_toggles);
        }

        if ((r_dir && r > 95) || (!r_dir && r < 5)) {
            r_dir = !r_dir;
            right_toggles++;
            printf("Right toggle %d\n", right_toggles / 2);
        }

        last_l = l;
        last_r = r;
        sleep_ms(50);
    }

    printf("Prompt 2 complete!\n");
}

void prompt_3() {
    printf("Prompt 3: Pull right pot to 0, then max left pot in 3 sec.\n");

    while (map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)) > 5) {
        render_gauge(map_pot_to_percent(read_pot_raw(POT_LEFT_ADC)),
                     map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)));
        sleep_ms(50);
    }

    absolute_time_t start = get_absolute_time();
    absolute_time_t deadline = delayed_by_ms(start, 3000);

    while (absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        int l = map_pot_to_percent(read_pot_raw(POT_LEFT_ADC));
        render_gauge(l, map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)));

        if (l > 95) {
            printf("Success! Left pot maxed.\n");
            return;
        }
        sleep_ms(50);
    }

    printf("Time's up! Failed to max left pot.\n");
}

// ─────────── Main ───────────
int main(void) {
    stdio_init_all();
    sleep_ms(100);

    i2c_init(i2c_default, 100000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    oled_init();
    adc_init_pots();

    printf("Return both pots to 0 to start.\n");
    while (map_pot_to_percent(read_pot_raw(POT_LEFT_ADC)) > 5 ||
           map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)) > 5) {
        render_gauge(map_pot_to_percent(read_pot_raw(POT_LEFT_ADC)),
                     map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)));
        sleep_ms(50);
    }

    printf("Starting game...\n");
    srand(time_us_32());
    int prompt = rand() % 3 + 1;

    switch (prompt) {
        case 1: prompt_1(); break;
        case 2: prompt_2(); break;
        case 3: prompt_3(); break;
    }

    printf("Game complete!\n");

    while (1) {
        render_gauge(map_pot_to_percent(read_pot_raw(POT_LEFT_ADC)),
                     map_pot_to_percent(read_pot_raw(POT_RIGHT_ADC)));
        sleep_ms(50);
    }
}

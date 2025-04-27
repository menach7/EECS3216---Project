#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include "ssd1306_font.h"

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

// ─────────── ADC channel ───────────
#define POT_ADC   1   // GP27

// ─────────── Game parameters ───────────
#define TARGET_ZONE_WIDTH 20  // degrees
#define TIME_TO_REACH 5000    // 5 seconds to reach target
#define TIME_TO_HOLD 10000    // 10 seconds to hold in target

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
static void adc_init_pot(void) {
    adc_init(POT_ADC);
    adc_gpio_init(27); // POT_ADC (GP27)
}

static uint16_t read_pot_raw(void) {
    adc_select_input(POT_ADC);
    return adc_read(); // 12-bit (0-4095)
}

static float map_pot_to_degrees(uint16_t raw) {
    return (raw / 4095.0f) * 180.0f;
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

void draw_target_zone(int cx, int cy, int radius, float start_deg, float end_deg) {
    for (float angle = start_deg; angle <= end_deg; angle += 1.0f) {
        float rad = DEG2RAD(angle);
        int x = cx + (int)((radius - 10) * cosf(rad));
        int y = cy - (int)((radius - 10) * sinf(rad));
        px(x, y, true);
    }
}

void render_gauge(float pot_angle, float target_start, float target_end) {
    clear_fb();

    int cx = W / 2;
    int cy = H - 1;
    int radius = 28;

    draw_semicircle(cx, cy, radius);
    
    // Draw target zone (green area - represented by thicker line)
    draw_target_zone(cx, cy, radius, target_start, target_end);
    
    // Draw needle (current position)
    draw_needle(cx, cy, pot_angle - 90.0f, radius - 2);

    oled_refresh();
}

// ─────────── Game Logic ───────────
void play_game(void) {
    // Generate random target zone (20 degrees wide)
    float target_start = (rand() % (180 - TARGET_ZONE_WIDTH)) + 0.0f;
    float target_end = target_start + TARGET_ZONE_WIDTH;
    
    printf("Target zone: %.1f to %.1f degrees\n", target_start, target_end);
    
    absolute_time_t game_start = get_absolute_time();
    absolute_time_t reach_deadline = delayed_by_ms(game_start, TIME_TO_REACH);
    absolute_time_t hold_deadline;
    
    bool in_target = false;
    bool target_reached = false;
    float hold_time = 0;
    
    while (true) {
        float current_angle = map_pot_to_degrees(read_pot_raw());
        render_gauge(current_angle, target_start - 90.0f, target_end - 90.0f);
        
        // Check if we're in the target zone
        bool now_in_target = (current_angle >= target_start && current_angle <= target_end);
        
        if (now_in_target) {
            if (!in_target) {
                // Just entered the target zone
                printf("Entered target zone!\n");
                if (!target_reached) {
                    target_reached = true;
                    hold_deadline = delayed_by_ms(get_absolute_time(), TIME_TO_HOLD);
                }
            }
            
            // Check if we've held long enough
            if (target_reached && absolute_time_diff_us(get_absolute_time(), hold_deadline) <= 0) {
                printf("Success! Held in target zone for 10 seconds.\n");
                return;
            }
        } else {
            if (in_target) {
                // Left the target zone
                printf("Left target zone!\n");
            }
            
            // Check if we've run out of time to reach the target
            if (!target_reached && absolute_time_diff_us(get_absolute_time(), reach_deadline) <= 0) {
                printf("Failed! Didn't reach target zone in 5 seconds.\n");
                return;
            }
        }
        
        in_target = now_in_target;
        sleep_ms(50);
    }
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
    adc_init_pot();

    printf("Return pot to 0 to start.\n");
    while (map_pot_to_degrees(read_pot_raw()) > 5.0f) {
        render_gauge(map_pot_to_degrees(read_pot_raw()), -90.0f, -90.0f);
        sleep_ms(50);
    }

    printf("Starting game...\n");
    srand(time_us_32());
    
    while (1) {
        play_game();
        
        // Wait for pot to return to 0 before next game
        printf("Return pot to 0 to play again.\n");
        while (map_pot_to_degrees(read_pot_raw()) > 5.0f) {
            render_gauge(map_pot_to_degrees(read_pot_raw()), -90.0f, -90.0f);
            sleep_ms(50);
        }
    }
}
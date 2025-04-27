#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/binary_info.h"

// LCD command definitions
const int LCD_CLEARDISPLAY = 0x01;
const int LCD_RETURNHOME   = 0x02;
const int LCD_ENTRYMODESET = 0x04;
const int LCD_DISPLAYCONTROL = 0x08;
const int LCD_CURSORSHIFT  = 0x10;
const int LCD_FUNCTIONSET  = 0x20;
const int LCD_SETCGRAMADDR = 0x40;
const int LCD_SETDDRAMADDR = 0x80;

// Flags for display entry mode
const int LCD_ENTRYSHIFTINCREMENT = 0x01;
const int LCD_ENTRYLEFT = 0x02;

// Flags for display and cursor control
const int LCD_BLINKON = 0x01;
const int LCD_CURSORON = 0x02;
const int LCD_DISPLAYON = 0x04;

// Flags for display and cursor shift
const int LCD_MOVERIGHT = 0x04;
const int LCD_DISPLAYMOVE = 0x08;

// Flags for function set
const int LCD_5x10DOTS = 0x04;
const int LCD_2LINE = 0x08;
const int LCD_8BITMODE = 0x10;

// Flag for backlight control
const int LCD_BACKLIGHT = 0x08;

const int LCD_ENABLE_BIT = 0x04;

// By default these LCD display drivers are on bus address 0x27
static int addr = 0x27;

// Modes for lcd_send_byte
#define LCD_CHARACTER  1
#define LCD_COMMAND    0

#define MAX_LINES      2
#define MAX_CHARS      16

// DDR Game Settings & Button Pins
#define BUTTON_UP_PIN     18
#define BUTTON_RIGHT_PIN  19
#define BUTTON_DOWN_PIN   20
#define BUTTON_LEFT_PIN   21

// Basic timing and gameplay constants (adjusted for half speed)
#define HIT_ZONE_POS      0      // leftmost column is our hit zone
#define SCROLL_DELAY_MS   300    // delay between each scroll update
#define HIT_WINDOW_MS     400    // timing window in ms

// Difficulty settings: these values will adjust as rounds progress.
uint32_t base_delay_ms = 2000;   // initial arrow delay (ms)
uint32_t scroll_delay_ms = SCROLL_DELAY_MS;
int sequence_length = 5;         // number of arrows per round

// Structure for a scrolling arrow command.
typedef struct {
    int arrow;                 // 0=LEFT, 1=UP, 2=RIGHT, 3=DOWN
    absolute_time_t hit_time;  // Scheduled time to hit
    bool hit;                  // Whether the arrow has been hit
    bool counted;              // Whether this hit was counted (for tracking)
} ArrowCommand;

#define MAX_ARROWS 10
ArrowCommand arrows[MAX_ARROWS];
int arrow_count = 0;

// Global score and combo variables.
int score = 0;
int combo = 0;
int target_arrow_count = 0;  // Count of tracked arrow hits
int target_arrow = 1;        // Default to UP (A)

// ---------- LCD Functions ----------

// Write a single byte over I2C.
void i2c_write_byte(uint8_t val) {
#ifdef i2c_default
    i2c_write_blocking(i2c_default, addr, &val, 1, false);
#endif
}

// Increase delay to allow the LCD to latch data properly.
void lcd_toggle_enable(uint8_t val) {
    #define DELAY_US 500
    sleep_us(DELAY_US);
    i2c_write_byte(val | LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
    i2c_write_byte(val & ~LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
}

void lcd_send_byte(uint8_t val, int mode) {
    uint8_t high = mode | (val & 0xF0) | LCD_BACKLIGHT;
    uint8_t low  = mode | ((val << 4) & 0xF0) | LCD_BACKLIGHT;
    i2c_write_byte(high);
    lcd_toggle_enable(high);
    i2c_write_byte(low);
    lcd_toggle_enable(low);
}

void lcd_clear(void) {
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
}

void lcd_set_cursor(int line, int position) {
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_send_byte(val, LCD_COMMAND);
}

static inline void lcd_char(char val) {
    lcd_send_byte(val, LCD_CHARACTER);
}

void lcd_string(const char *s) {
    while (*s) {
        lcd_char(*s++);
    }
}

// ---------- Custom Character Functions ----------
// Create a custom character (location 0 to 7) from an 8-byte bitmap.
void lcd_create_custom_char(uint8_t location, uint8_t charmap[]) {
    lcd_send_byte(LCD_SETCGRAMADDR | (location << 3), LCD_COMMAND);
    for (int i = 0; i < 8; i++) {
        lcd_send_byte(charmap[i], LCD_CHARACTER);
    }
    // Return to DDRAM.
    lcd_send_byte(LCD_SETDDRAMADDR, LCD_COMMAND);
}

// Define custom arrow bitmaps.
uint8_t arrow_left[8] = {
    0b00100,
    0b01000,
    0b11111,
    0b01000,
    0b00100,
    0b00000,
    0b00000,
    0b00000
};

uint8_t arrow_up[8] = {
    0b00100,
    0b01110,
    0b10101,
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b00000
};

uint8_t arrow_right[8] = {
    0b00100,
    0b00010,
    0b11111,
    0b00010,
    0b00100,
    0b00000,
    0b00000,
    0b00000
};

uint8_t arrow_down[8] = {
    0b00100,
    0b00100,
    0b00100,
    0b00100,
    0b10101,
    0b01110,
    0b00100,
    0b00000
};

// ---------- Updated LCD Initialization ----------
// Initialize LCD and load custom arrow characters.
void lcd_init_custom() {
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x03, LCD_COMMAND);
    lcd_send_byte(0x02, LCD_COMMAND);
    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
    lcd_clear();

    // Load custom arrow characters into locations 0-3.
    lcd_create_custom_char(0, arrow_left);
    lcd_create_custom_char(1, arrow_up);
    lcd_create_custom_char(2, arrow_right);
    lcd_create_custom_char(3, arrow_down);
}

// ---------- Button Functions ----------
void buttons_init() {
    gpio_init(BUTTON_LEFT_PIN);
    gpio_set_dir(BUTTON_LEFT_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_LEFT_PIN);

    gpio_init(BUTTON_UP_PIN);
    gpio_set_dir(BUTTON_UP_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_UP_PIN);

    gpio_init(BUTTON_RIGHT_PIN);
    gpio_set_dir(BUTTON_RIGHT_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_RIGHT_PIN);

    gpio_init(BUTTON_DOWN_PIN);
    gpio_set_dir(BUTTON_DOWN_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_DOWN_PIN);
}

// Returns the button index pressed or -1 if none. (Active-low)
int get_button_pressed() {
    if (gpio_get(BUTTON_LEFT_PIN) == 0) return 0;
    if (gpio_get(BUTTON_UP_PIN) == 0) return 1;
    if (gpio_get(BUTTON_RIGHT_PIN) == 0) return 2;
    if (gpio_get(BUTTON_DOWN_PIN) == 0) return 3;
    return -1;
}

// Wait for a button press (with debounce) up to timeout_ms.
int wait_for_button_press(uint32_t timeout_ms) {
    absolute_time_t start = get_absolute_time();
    while (absolute_time_diff_us(start, get_absolute_time()) < timeout_ms * 1000) {
        int button = get_button_pressed();
        if (button != -1) {
            sleep_ms(50); // simple debounce
            while (get_button_pressed() != -1);
            return button;
        }
        sleep_ms(10);
    }
    return -1;
}

// ---------- Game Functions ----------
// Add a new arrow command to the scrolling array.
void add_arrow_command(int arrow, uint32_t delay_ms) {
    if (arrow_count < MAX_ARROWS) {
        arrows[arrow_count].arrow = arrow;
        arrows[arrow_count].hit_time = make_timeout_time_ms(delay_ms);
        arrows[arrow_count].hit = false;
        arrows[arrow_count].counted = false;
        arrow_count++;
    }
}

// Update the display to show scrolling arrows using custom characters.
void update_scrolling_arrows() {
    lcd_clear();
    // Display arrows on row 0. Their horizontal position is determined by time left.
    for (int i = 0; i < arrow_count; i++) {
        int time_diff_us = absolute_time_diff_us(get_absolute_time(), arrows[i].hit_time);
        int pos = (int)(time_diff_us / 200000); // Adjusted for slower scroll speed
        if (pos >= MAX_CHARS) pos = MAX_CHARS - 1;
        if (pos < 0) pos = 0;
        lcd_set_cursor(0, pos);
        // Display the custom character corresponding to the arrow.
        lcd_send_byte(arrows[i].arrow, LCD_CHARACTER);
    }
}

// Provide quick visual feedback on the LCD.
void show_feedback(const char *msg) {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string(msg);
    sleep_ms(500);
    lcd_clear();
}

// Award points based on how close the timing was.
void register_hit(uint32_t timing_diff_us, int arrow_type) {
    if (timing_diff_us < 100000) {  // within 100ms = perfect hit
        score += 100 * (combo + 1);
        combo++;
        show_feedback("Perfect!");
    } else if (timing_diff_us < 200000) {  // within 200ms = great
        score += 50 * (combo + 1);
        combo++;
        show_feedback("Great!");
    } else {  // late but still a hit
        score += 20;
        combo = 0;
        show_feedback("Good");
    }

    // Count if it's our target arrow type
    if (arrow_type == target_arrow && !arrows[arrow_count].counted) {
        target_arrow_count++;
        arrows[arrow_count].counted = true;
    }
}

// Adjust difficulty (e.g., speed up arrows) as rounds progress.
void update_difficulty(int round) {
    if (round > 0) {
        uint32_t new_delay = 2000 - (round * 200);
        base_delay_ms = (new_delay < 1000) ? 1000 : new_delay;
    }
}

// The main game loop for a round with scrolling arrows.
void game_loop_scrolling(int round) {
    arrow_count = 0;
    combo = 0;
    // Schedule a series of arrows.
    for (int i = 0; i < sequence_length; i++) {
        add_arrow_command(rand() % 4, base_delay_ms * (i + 1));
    }
    
    // Run the scrolling loop until all arrows have been processed.
    while (arrow_count > 0) {
        update_scrolling_arrows();
        sleep_ms(scroll_delay_ms);
        
        // Check each arrow to see if it is within the hit window.
        for (int i = 0; i < arrow_count; i++) {
            int32_t diff_us = (int32_t)absolute_time_diff_us(get_absolute_time(), arrows[i].hit_time);
            if (!arrows[i].hit && (diff_us < HIT_WINDOW_MS * 1000)) {
                lcd_set_cursor(1, 0);
                lcd_string("Hit ");
                lcd_send_byte(arrows[i].arrow, LCD_CHARACTER);
                
                int btn = wait_for_button_press(HIT_WINDOW_MS);
                if (btn == arrows[i].arrow) {
                    register_hit(diff_us, arrows[i].arrow);
                    arrows[i].hit = true;
                } else {
                    combo = 0;
                    show_feedback("Miss!");
                    arrows[i].hit = true;
                }
            }
        }
        // Remove arrows that have passed their hit window.
        int j = 0;
        for (int i = 0; i < arrow_count; i++) {
            if (absolute_time_diff_us(arrows[i].hit_time, get_absolute_time()) < (HIT_WINDOW_MS * 1000))
                arrows[j++] = arrows[i];
        }
        arrow_count = j;
    }
    
    lcd_clear();
    char scoreStr[16];
    snprintf(scoreStr, sizeof(scoreStr), "Target:%d", target_arrow_count);
    lcd_set_cursor(0, 0);
    lcd_string(scoreStr);
    sleep_ms(3000);
}

int DDR_v3_game(char mode_char) {
#if !defined(i2c_default) || !defined(PICO_DEFAULT_I2C_SDA_PIN) || !defined(PICO_DEFAULT_I2C_SCL_PIN)
    #warning i2c/lcd_1602_i2c example requires a board with I2C pins
    return 0;
#else
    stdio_init_all();
    
    // Set I2C to 400kHz.
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, 
                             PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));
    
    lcd_init_custom();
    buttons_init();
    srand(time_us_32());
    
    // Initialize game state
    score = 0;
    combo = 0;
    target_arrow_count = 0;
    
    // Set target arrow based on mode character
    switch(mode_char) {
        case 'A': target_arrow = 1; break; // Up
        case 'B': target_arrow = 3; break; // Down
        case 'C': target_arrow = 0; break; // Left
        case 'D': target_arrow = 2; break; // Right
        default:  target_arrow = 1; break; // Default to up
    }
    
    int round = 1;
    while (1) {
        lcd_clear();
        lcd_set_cursor(0, 0);
        
        // Show which arrow we're tracking
        char arrow_name[10];
        switch(target_arrow) {
            case 0: strcpy(arrow_name, "TRACK:LEFT"); break;
            case 1: strcpy(arrow_name, "TRACK:UP"); break;
            case 2: strcpy(arrow_name, "TRACK:RIGHT"); break;
            case 3: strcpy(arrow_name, "TRACK:DOWN"); break;
        }
        lcd_string(arrow_name);
        
        lcd_set_cursor(1, 0);
        lcd_string("Press any btn");
        while (get_button_pressed() == -1) {
            sleep_ms(10);
        }
        while (get_button_pressed() != -1) {
            sleep_ms(10);
        }
        sleep_ms(500);
        
        update_difficulty(round);
        game_loop_scrolling(round);
        round++;
        
        // Return after one complete game session
        break;
    }
    
    return target_arrow_count;
#endif
}

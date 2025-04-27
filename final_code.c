#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "ssd1306_font.h"

// Hardware configuration
#define POT_ADC 0       // GP26 (ADC0)
#define BUTTON_PIN 15   // GP15
#define OLED_ADDR 0x3C
#define W 128
#define H 64
#define FB_LEN (W*H/8)
static uint8_t fb[FB_LEN];

// Display functions (similar to your existing code)
static void oled_init() { /* ... */ }
static void oled_refresh() { /* ... */ }
static void px(int x, int y, bool on) { /* ... */ }

static void draw_digit(int x, int y, int digit, bool highlight) {
    const uint8_t digits[10][8] = { /* ... */ };
    // ... same digit drawing implementation ...
}

static void render_ui(int digit_pos, int current_digit, int expected_code) {
    memset(fb, 0, FB_LEN);
    
    // Draw instruction
    const char* msg = digit_pos == 0 ? "Enter 1st digit:" : "Enter 2nd digit:";
    int x = (W - strlen(msg)*8)/2;
    for(int i=0; msg[i]; i++) {
        // Simple text drawing (implement properly)
        if(msg[i] >= '0' && msg[i] <= '9') {
            draw_digit(x + i*8, 10, msg[i]-'0', false);
        }
    }
    
    // Draw expected code hint
    char code_str[3];
    snprintf(code_str, 3, "%02d", expected_code);
    x = (W - 16)/2;
    draw_digit(x, 2, code_str[0]-'0', false);
    draw_digit(x+8, 2, code_str[1]-'0', false);
    
    // Draw digit selector
    x = (W - 100)/2;
    for(int i=0; i<10; i++) {
        draw_digit(x + i*10, 30, i, i == current_digit);
    }
    
    oled_refresh();
}

bool verify_final_code(int expected_code) {
    // Initialize hardware
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    oled_init();
    
    int entered_code = 0;
    int digits[2] = {0};
    
    for(int pos=0; pos<2; pos++) {
        int current_digit = 0;
        while(true) {
            // Update display
            render_ui(pos, current_digit, expected_code);
            
            // Check potentiometer
            int new_digit = (adc_read() * 10) / 4096;
            if(new_digit != current_digit) {
                current_digit = new_digit;
            }
            
            // Check button press
            if(!gpio_get(BUTTON_PIN)) {
                sleep_ms(50); // Debounce
                while(!gpio_get(BUTTON_PIN)); // Wait release
                digits[pos] = current_digit;
                break;
            }
            sleep_ms(50);
        }
    }
    
    // Combine digits
    entered_code = digits[0]*10 + digits[1];
    
    // Show result
    memset(fb, 0, FB_LEN);
    char result_msg[32];
    bool success = (entered_code == expected_code);
    snprintf(result_msg, 32, "%s! Expected: %02d Got: %02d", 
             success ? "CORRECT" : "WRONG", 
             expected_code, entered_code);
    
    // Draw result (simplified)
    oled_refresh();
    sleep_ms(2000);
    
    return success;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

// Game module headers
#include "DDR_v3.h"
#include "potentiometer_v3.h"
#include "Doom_v8.h"
#include "final_code.h"


#define BUTTON_PIN 15

// Game state structure
typedef struct {
    char control_string[4];
    int ddr_score;          // Count of arrows (A/B/C/D)
    int doom_score;         // Count of targets (E/F)
    time_t start_time;
    bool timed_out;
} GameState;

// Generate control string (format: [A-D]P[E-F])
void generate_control_string(char *str) {
    str[0] = 'A' + (rand() % 4);  // A-D
    str[1] = 'P';                 // Always P
    str[2] = 'E' + (rand() % 2);  // E or F
    str[3] = '\0';
}

// Check if time remains (3 minute limit)
bool check_time_remaining(GameState *state) {
    if (time(NULL) - state->start_time > 180) { // 3 minutes = 180 seconds
        state->timed_out = true;
        printf("\nTIME'S UP! Game over.\n");
        return false;
    }
    printf("Time remaining: %ld seconds\n", 180 - (time(NULL) - state->start_time));
    return true;
}

// Initialize ADC for potentiometer
void adc_init_pot() {
    adc_init();
    adc_gpio_init(26); // GP26 (ADC0)
    adc_select_input(0);
}

// Read potentiometer value (0-9)
int read_pot_digit() {
    uint16_t raw = adc_read(); // 0-4095
    return (raw * 10) / 4096;  // Scale to 0-9
}

// Enter final code using potentiometer
if (!state.timed_out) {
    printf("\n=== ALL GAMES COMPLETED ===\n");
    printf("Control String: %s\n", state.control_string);
    printf("DDR Arrows: %d\n", state.ddr_score);
    printf("Doom Targets: %d\n", state.doom_score);
    
    // Calculate expected code
    int expected_code = state.ddr_score * 10 + state.doom_score;
    
    // Verify code
    printf("\n=== FINAL CODE ENTRY ===\n");
    bool success = verify_final_code(expected_code);
    
    if (success) {
        printf("\nSUCCESS! Code accepted.\n");
    } else {
        printf("\nFAILURE! Incorrect code entered.\n");
    }
}

int main() {
    stdio_init_all();
    sleep_ms(1000); // Wait for serial connection

    srand(time(NULL));
    GameState state = {0};
    state.start_time = time(NULL);
    state.timed_out = false;

    // Generate control string
    generate_control_string(state.control_string);
    printf("=== CONTROL STRING: %s ===\n", state.control_string);
    printf("You have 3 minutes to complete all games!\n");

    // DDR Game (Logs arrow counts)
    if (check_time_remaining(&state)) {
        printf("\n[1/3] DDR GAME (Mode: %c)\n", state.control_string[0]);
        state.ddr_score = DDR_v3_game(state.control_string[0]);
    }

    // Potentiometer Game (No scoring)
    if (!state.timed_out && check_time_remaining(&state)) {
        printf("\n[2/3] POTENTIOMETER GAME\n");
        potentiometer_v3_game();
    }

    // Doom Game (Logs target counts)
    if (!state.timed_out && check_time_remaining(&state)) {
        printf("\n[3/3] DOOM GAME (Targets: %c)\n", state.control_string[2]);
        state.doom_score = Doom_v8_game(state.control_string[2]);
    }

    // Final output
    if (!state.timed_out) {
        printf("\n=== ALL GAMES COMPLETED ===\n");
        printf("Control String: %s\n", state.control_string);
        printf("DDR Arrows: %d\n", state.ddr_score);
        printf("Doom Targets: %d\n", state.doom_score);
        
        // Enter final code
        printf("\n=== FINAL CODE ENTRY ===\n");
        int success = enter_final_code(state.ddr_score, state.doom_score);
        
        if (success) {
            printf("\nSUCCESS! Code accepted.\n");
        } else {
            printf("\nFAILURE! Incorrect code entered.\n");
        }
    } else {
        printf("Partial Results:\n");
        printf("DDR Arrows: %d\n", state.ddr_score);
        printf("Doom Targets: %d\n", state.doom_score);
    }

    return 0;
}
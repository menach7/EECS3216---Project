#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

// Simulated potentiometer readings
int read_pot_left();   // Replace with actual ADC read
int read_pot_right();  // Replace with actual ADC read

void draw_gauge(int left, int right); // Represent both potentiometers
bool is_aligned(int l, int r, int margin);

void prompt_1();
void prompt_2();
void prompt_3();

int main() {
    srand(time(NULL));
    printf("Return both potentiometers to 0 to begin.\n");
    
    while (read_pot_left() > 5 || read_pot_right() > 5); // Wait until both are at 0
    printf("Starting game...\n");

    int prompt = rand() % 3 + 1;
    switch (prompt) {
        case 1: prompt_1(); break;
        case 2: prompt_2(); break;
        case 3: prompt_3(); break;
    }

    return 0;
}

void prompt_1() {
    printf("Prompt 1: Toggle both potentiometers back and forth in sync (5 times).\n");
    int toggles = 0;
    bool moving_forward = true;
    int last_l = read_pot_left();
    int last_r = read_pot_right();

    while (toggles < 5) {
        int l = read_pot_left();
        int r = read_pot_right();
        draw_gauge(l, r);

        if (l < 15 || l > 85 || r < 15 || r > 85) {
            printf("Out of bounds!\n");
            return;
        }

        if (is_aligned(l, r, 5)) {
            // Detect toggle point by direction change
            if ((moving_forward && l < last_l && r < last_r) ||
                (!moving_forward && l > last_l && r > last_r)) {
                toggles++;
                moving_forward = !moving_forward;
                printf("Toggle %d complete.\n", toggles);
            }
        }

        last_l = l;
        last_r = r;
    }

    printf("Prompt 1 complete!\n");
}

void prompt_2() {
    printf("Prompt 2: Left pot: toggle within 30-70 (10 times), Right pot: full range toggle.\n");
    int left_toggles = 0;
    int right_toggles = 0;
    bool l_dir = true;
    bool r_dir = true;
    int last_l = read_pot_left();
    int last_r = read_pot_right();

    while (left_toggles < 10 || right_toggles < 2) {
        int l = read_pot_left();
        int r = read_pot_right();
        draw_gauge(l, r);

        if (l < 30 || l > 70) {
            printf("Left pot out of bounds!\n");
            return;
        }

        // Left toggle detection
        if ((l_dir && l < last_l) || (!l_dir && l > last_l)) {
            l_dir = !l_dir;
            left_toggles++;
            printf("Left toggle %d\n", left_toggles);
        }

        // Right toggle detection (full range)
        if ((r_dir && r > 95) || (!r_dir && r < 5)) {
            r_dir = !r_dir;
            right_toggles++;
            printf("Right toggle %d\n", right_toggles / 2);
        }

        last_l = l;
        last_r = r;
    }

    printf("Prompt 2 complete!\n");
}

void prompt_3() {
    printf("Prompt 3: Pull right pot to 0 to start. Then move left pot to max within 3 seconds.\n");

    while (read_pot_right() > 5); // Wait for right to go back

    clock_t start = clock();
    clock_t deadline = start + 3 * CLOCKS_PER_SEC;

    while (clock() < deadline) {
        int l = read_pot_left();
        draw_gauge(read_pot_left(), read_pot_right());

        if (l > 95) {
            printf("Success! Left pot maxed in time.\n");
            return;
        }
    }

    printf("Time's up! You failed to max the left pot.\n");
}


bool is_aligned(int l, int r, int margin) {
    return abs(l - r) <= margin;
}

void draw_gauge(int l, int r) {
    // Placeholder: You can create an ASCII-style speedometer or connect to a display
    printf("L: %3d | R: %3d\n", l, r);
}

// Simulated analog reads
int read_pot_left() {
    // Replace with analogRead() or ADC value
    return rand() % 101;
}

int read_pot_right() {
    return rand() % 101;
}


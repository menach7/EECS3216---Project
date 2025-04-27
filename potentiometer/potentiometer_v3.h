#ifndef POTENTIOMETER_V3_H
#define POTENTIOMETER_V3_H

#include <stdbool.h>

/**
 * @brief Runs the potentiometer target game
 * 
 * Features:
 * - Displays semicircular gauge on OLED
 * - Random target zone (20° wide)
 * - Must reach target within 5 seconds
 * - Must maintain position for 10 seconds
 * - Uses GP26 for potentiometer input
 * - No return value (just completion)
 */
void potentiometer_v3_game(void);

#endif
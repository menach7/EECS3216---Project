#ifndef DDR_V3_H
#define DDR_V3_H

/**
 * @brief Runs the DDR arrow game tracking specific arrows
 * @param mode_char The control character (A/B/C/D) specifying which arrow to track
 * @return int Count of successfully hit arrows of the specified type
 * 
 * Arrow Mapping:
 * A - Up
 * B - Down
 * C - Left
 * D - Right
 */
int DDR_v3_game(char mode_char);

#endif
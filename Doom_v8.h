#ifndef DOOM_V8_H
#define DOOM_V8_H

/**
 * @brief Runs the Doom-style target shooting game
 * 
 * @param target_type 'E' to track squares or 'F' to track circles
 * @return int Number of correct targets destroyed during gameplay
 * 
 * Features:
 * - 15-second timed gameplay
 * - OLED display with crosshair
 * - Analog joystick control
 * - Tracks only specified target type (squares or circles)
 * - Returns hit count for scorekeeping
 * 
 * Hardware Requirements:
 * - Pico-compatible OLED (128x64, I2C)
 * - Analog joystick (VRx/VRy) on GP26/GP27
 * - Button on GP15
 */
int Doom_v8_game(char target_type);

#endif
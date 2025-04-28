#ifndef DDR_H
#define DDR_H

#include <stdint.h>

#define BUTTON_UP_PIN     7
#define BUTTON_RIGHT_PIN  8
#define BUTTON_DOWN_PIN   9
#define BUTTON_LEFT_PIN   6

void ddr_init(void);
void run_ddr(void);

#endif // DDR_H
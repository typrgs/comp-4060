#pragma once

#include "sam.h"
#include <stdbool.h>

// NOTE: display must be in bus port 3; SERCOM1 is used; generic clock 3 is used

// all pixel location calculations assume an origin (0,0) at top-left

#define DISPLAY_SIZE 96

#define FONT_SIZE 12

#define BLACK     0x0000
#define WHITE     0xffff
#define RED       0xf800
#define PINK      0x88cc
#define ORANGE    0xff22
#define BLUE      0x01ff

// if a pixel was previously drawn using this routine, it will be erased and replaced by a pixel at the given x-y coordinate
void displayReplacePixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour);

// draws a pixel of the given colour at the given x-y coordinate
void displayDrawPixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour);

// draws the given digit, using the given colour, starting at the given row-column coordinates
void displayDrawDigit(uint8_t startRow, uint8_t startCol, uint16_t colour, uint8_t number);

// erases the display by making all pixels black
void displayErase();

// MUST be called prior to any other display usage
void displayInit();

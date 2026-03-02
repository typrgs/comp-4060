#ifndef _MY_DISPLAY_H
#define _MY_DISPLAY_H

#include "common.h"

// NOTE: display defaults to be in bus port 3, to use port 1 define USE_DISPLAY_PORT_1
//#define USE_DISPLAY_PORT_1

// all pixel location calculations assume an origin (0,0) at top-left

#define DISPLAY_SIZE 96UL

#define FONT_SIZE 12

#define BLACK     0x0000
#define WHITE     0xffff
#define LIGHT_RED 0xf000
#define RED       0xf800
#define PINK      0x88cc
#define YELLOW    0xff22
#define ORANGE    0xfae0
#define DARK_ORANGE 0x3000
#define DARK_RED  0x1000
#define DARK_BLUE 0x0088
#define BLUE      0x01ff
#define GREEN     0x07e0
#define DARK_GREEN 0x0100

// replace operations assume a draw operation has already occurred
void displayReplacePixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour);

void displayDrawPixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour);

void displayDrawFont(uint8_t startRow, uint8_t startCol, uint16_t colour, uint8_t number);

void displayWipe(uint16_t colour);
void displayInit();

#endif
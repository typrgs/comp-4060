#include "display.h"
#include "spi.h"

// display commands
#define DISPLAY_WRITE 0x5C
#define DISPLAY_WAKE  0xAF
#define DISPLAY_SLEEP 0xAE
#define DISPLAY_FUNC  0xAB
#define DISPLAY_REMAP 0xA0
#define DISPLAY_START_LINE 0xA1
#define DISPLAY_X_ADDR 0x15
#define DISPLAY_Y_ADDR 0x75
#define DISPLAY_ENHANCE 0xB2

// where RAM starts relative to screen pixels
#define DISPLAY_X_OFFSET 16
#define DISPLAY_Y_OFFSET 32

// this gives us a 12MHz SCK frequency
#define DISPLAY_BAUD 1

#define FONT_COLS 8
#define NUM_CHARS 13
static const uint8_t fontTable[NUM_CHARS][FONT_SIZE] = 
{
// 0
{ 0x7e, 0x81, 0x81, 0x81, 0x99, 0x99, 0x99, 0x99, 0x81, 0x81, 0x81, 0x7e},
// 1
{ 0x7f, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x48, 0x28, 0x18, 0x08},
// 2
{ 0x7e, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7e, 0x01, 0x01, 0x01, 0x01, 0x7e},
// 3
{ 0x7e, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e, 0x01, 0x01, 0x01, 0x01, 0x7e},
// 4
{ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e, 0x81, 0x81, 0x81, 0x81, 0x81},
// 5
{ 0x7e, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e, 0x80, 0x80, 0x80, 0x80, 0x7e},
// 6
{ 0x7e, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7e, 0x80, 0x80, 0x80, 0x80, 0x7e},
// 7
{ 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e},
// 8
{ 0x7e, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7e, 0x81, 0x81, 0x81, 0x81, 0x7e},
// 9
{ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x7e, 0x81, 0x81, 0x81, 0x81, 0x7e},
// % symbol
{ 0x00, 0x00, 0x87, 0x45, 0x27, 0x10, 0x08, 0xe4, 0xa2, 0xe1, 0x00, 0x00},
// degree symbol
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xa0, 0xe0},
// dash symbol
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00}
};

#ifdef USE_DISPLAY_PORT_1
  #define CMD_PIN PORT_PA10
  #define CS_PIN PORT_PA18
  #define RESET_PIN PORT_PA07
  #define RW_PIN PORT_PA02
  #define ENABLE_PIN PORT_PA04

  #define PORT_GROUP 0
#else
  #define CMD_PIN PORT_PB09
  #define CS_PIN PORT_PB04
  #define RESET_PIN PORT_PB06
  #define RW_PIN PORT_PB08
  #define ENABLE_PIN PORT_PB07

  #define PORT_GROUP 1
#endif

static void displayCmd(uint8_t cmd)
{
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTCLR = CMD_PIN;
  spiWriteByte(cmd);
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = CMD_PIN;
}

static void activate()
{
  // setup spi for our ops
  spiActivate(DISPLAY_BAUD, false, false, true);

  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTCLR = CS_PIN;
}

static void deactivate()
{
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = CS_PIN;

  spiDeactivate();
}

void displayDrawDigit(uint8_t startRow, uint8_t startCol, uint16_t colour, uint8_t value)
{
  uint8_t currBits;
  uint16_t currPixel;

  // activate chip select
  activate();

  // draw the pixels for the character
  for (int row=0; row<FONT_SIZE; row++)
  {
    // set the location of the character on the screen
    displayCmd(DISPLAY_X_ADDR);
    spiWriteByte(startCol+DISPLAY_X_OFFSET);
    spiWriteByte(startCol+DISPLAY_X_OFFSET+FONT_COLS);

    displayCmd(DISPLAY_Y_ADDR);
    spiWriteByte(startRow-row+FONT_SIZE);
    spiWriteByte(startRow-row+FONT_SIZE);

    // start write to VRAM 
    displayCmd(DISPLAY_WRITE);

    currBits = fontTable[value][row];

    for (int col=0; col<FONT_COLS; col++)
    {
      if ((currBits & 0x80) == 0x80)
        currPixel = colour;
      else
        currPixel = 0;

      spiWriteByte((uint8_t)(currPixel>>8));
      spiWriteByte((uint8_t)currPixel);

      currBits <<= 1;
    }
  }

  // deactive chip select  
  deactivate();
}

void displayDrawPixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour)
{
  // activate chip select
  activate();

  // now write the new pixel
  displayCmd(DISPLAY_X_ADDR);
  spiWriteByte(pixelX+DISPLAY_X_OFFSET);
  spiWriteByte(pixelX+DISPLAY_X_OFFSET);

  displayCmd(DISPLAY_Y_ADDR);
  spiWriteByte(pixelY);
  spiWriteByte(pixelY);

  // start write to VRAM 
  displayCmd(DISPLAY_WRITE);

  // "activate" pixel
  spiWriteByte((uint8_t)(colour>>8));
  spiWriteByte((uint8_t)colour);

  // deactive chip select  
  deactivate();
}

void displayReplacePixel(uint8_t pixelX, uint8_t pixelY, uint16_t colour)
{
  static bool firstPixel = true;
  static uint8_t prevX = 0;
  static uint8_t prevY = 0;

  // erase the previous pixel, if there is one
  if (!firstPixel)
  {
    displayDrawPixel(prevX, prevY, BLACK);
  }
  else
    firstPixel = false;

  displayDrawPixel(pixelX, pixelY, colour);

  // remember where we drew last
  prevX = pixelX;
  prevY = pixelY;
}

void displayWipe(uint16_t colour)
{
  // activate chip select
  activate();

  // setup so we write *all* of VRAM with the same pixel value

  displayCmd(DISPLAY_X_ADDR);
  spiWriteByte(0);
  spiWriteByte(127);

  displayCmd(DISPLAY_Y_ADDR);
  spiWriteByte(0);
  spiWriteByte(127);

  // start write to VRAM 
  displayCmd(DISPLAY_WRITE);

  for ( int x=0 ; x<128; x++)
  {
    for ( int y=0; y<128; y++)
    {
      spiWriteByte((uint8_t)(colour>>8));
      spiWriteByte((uint8_t)colour);
    }
  }

  // deactive chip select  
  deactivate();
}

static void displayWake()
{
  // activate chip select
  activate();

  // turn it on!
  displayCmd(DISPLAY_WAKE);

  // deactive chip select  
  deactivate();
}

void displaySleep()
{
  // activate chip select
  activate();

  // turn it off
  displayCmd(DISPLAY_SLEEP);

  // deactive chip select  
  deactivate();
}

void displayInit()
{
  // reset; reset it on initialization
  PORT_REGS->GROUP[PORT_GROUP].PORT_DIRSET = RESET_PIN;
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTCLR = RESET_PIN;
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = RESET_PIN;

  // enable pin
  PORT_REGS->GROUP[PORT_GROUP].PORT_DIRSET = ENABLE_PIN;
  // chip select pin; default to not selected
  // NOTE: the silkscreen on the board is WRONG! it's PB4 and PB5 NOT PA4 and PA5 (for CS 3 and 2!)
  PORT_REGS->GROUP[PORT_GROUP].PORT_DIRSET = CS_PIN;
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = CS_PIN;
  // R/W pin
  PORT_REGS->GROUP[PORT_GROUP].PORT_DIRSET = RW_PIN;
  // data/command pin; default to data
  PORT_REGS->GROUP[PORT_GROUP].PORT_DIRSET = CMD_PIN;
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = CMD_PIN;

  // display is always enabled and set to write only (can't read via SPI)
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTCLR = RW_PIN;
  PORT_REGS->GROUP[PORT_GROUP].PORT_OUTSET = ENABLE_PIN;

  displayWake();

  // activate chip select
  activate();

  // set bottom-left as location 0,0 and allow pixel perfect control
  displayCmd(DISPLAY_REMAP);
  spiWriteByte(0x32);

  // offset by the first 32 lines of VRAM, which are offscreen
  displayCmd(DISPLAY_START_LINE);
  spiWriteByte(DISPLAY_Y_OFFSET);

  // activate enhanced display (cleaner refreshes?)
  displayCmd(DISPLAY_ENHANCE);
  spiWriteByte(0xA4);
  spiWriteByte(0);
  spiWriteByte(0);

  // clear the display (by making it all black)
  displayWipe(0);

  // deactive chip select  
  deactivate();
}

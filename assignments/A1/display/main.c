#include "common.h"
#include "heart.h"
#include "display.h"
#include "spi.h"
#include "net.h"
#include "uart485.h"

/*
enum MODE
{
  WEATHER, 
  GYRO_WEATHER,
  ANALOG,
  CURSOR,
  NUM_MODES
};
typedef enum MODE Mode;
*/
#define EXTINT15_MASK 0x8000

// scheduling intervals with ms granularity

// flash twice a second
#define LED_FLASH_MS        500UL
// number of millisecond between button checks
#define CHECK_BUTTON_MS     101UL

// we're checking the weather twice a second to make sure we get the latest sampling
#define DISPLAY_WEATHER_MS  100UL

#define DISPLAY_ANALOG_MS   100UL

static uint8_t deviceIDs[NUM_SENSORS];

volatile bool nextMode = false;

volatile bool newDisplayData = false;
volatile bool firstFail = true;
volatile Packet msg;

volatile uint8_t sensorMsg[PROTOCOL_PACKET_SIZE];

volatile uint32_t msCount = 0;

volatile Sensor currMode = TEMP_HUMIDITY;
volatile uint16_t refreshRate = DISPLAY_WEATHER_MS;

static uint8_t distance = DISPLAY_SIZE/2 - 1;

// parameterized in the assignment code that is the source (so students can experiment)
static uint8_t cycleStep = 4;

// sample inputs and process the FSM every 50ms, or 20x/s
#define PROCESS_KEYS_MS  50

// our FSM increments timestamps every 50ms, so that's our baseline for determining how long something takes
// we repeat 10x a second
#define KEY_REPEAT   2

// input ports
#define KEY1 PORT_PA02
#define KEY2 PORT_PA07

#define NUM_KEYS 2

enum STATES
{
  NONE,
  KEY1_ON,
  KEY1_OFF,
  KEY2_ON,
  KEY2_OFF,
  NUM_STATES
};

typedef enum STATES State;

// function pointer for defining our state machine lookup table
// note: not required in submitted solutions, just showing how it's done
typedef State(*getNextState)(bool *keyState, uint8_t *timestamp, uint8_t *currDuty);

void buttonInit()
{
  // switch input on PA15, processed as an external interrupt
  PORT_REGS->GROUP[0].PORT_DIRCLR = (PORT_PA15 | KEY1 | KEY2);
  PORT_REGS->GROUP[0].PORT_PINCFG[2] = PORT_PINCFG_INEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[7] = PORT_PINCFG_INEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[15] = PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_PULLEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[7] = PORT_PMUX_PMUXO_A;

  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA15;

  // have to enable the interrupt line in the system level REG
  NVIC_EnableIRQ(EIC_EXTINT_15_IRQn);

  MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_EIC_Msk;

  EIC_REGS->EIC_CONFIG[1] = EIC_CONFIG_SENSE7_RISE;
  EIC_REGS->EIC_INTENSET = EXTINT15_MASK;
  EIC_REGS->EIC_CTRLA = EIC_CTRLA_CKSEL_CLK_ULP32K | EIC_CTRLA_ENABLE_Msk;
}

// no need for hysteresis with the click inputs, but let's keep the nice abstraction
void sampleInputs(bool *keyState)
{
 keyState[0] = (PORT_REGS->GROUP[0].PORT_IN & KEY1) == KEY1;
 keyState[1] = (PORT_REGS->GROUP[0].PORT_IN & KEY2) == KEY2;
}

// ISR for  external interrupt 15
void EIC_EXTINT_15_Handler()
{
  // clear the interrupt! and go to the next operating mode
  EIC_REGS->EIC_INTFLAG = EXTINT15_MASK;
  nextMode = true;
}

void loadParameters()
{
  uint8_t *parms = (uint8_t *)PARAMETER_ADDR;

  for (int i=0; i<NUM_SENSORS; i++)
    deviceIDs[i] = parms[i];
}

///////////////////////////////////////////////
// Handlers for processing state machine states
// note: not required in submitted solutions, just showing how it's done

State currStateNone(bool *keyState, uint8_t *timestamp, uint8_t *currDuty)
{
  State nextState = NONE;

  // start at the repeat so we detect single hits
  *timestamp = KEY_REPEAT;
  if ( keyState[0] )
  {
    nextState = KEY1_ON;
  }
  
  else if ( keyState[1] )
  {
    nextState = KEY2_ON;
  }

  return nextState;
}

State currStateKey1On(bool *keyState, unsigned char *timestamp, uint8_t *currDuty)
{
  State nextState = KEY1_ON; 

  (*timestamp)++;
  if ( !keyState[0] )
  {
    nextState = KEY1_OFF;
  }
  else if ( *timestamp > KEY_REPEAT )
  {
    // make sure we don't roll over...
    if ( *currDuty > cycleStep )
      *currDuty -= cycleStep;
    else
      *currDuty = 0;
    
    *timestamp = 0;
  }

  return nextState;
}

State currStateKey1Off(bool *keyState, unsigned char *timestamp, uint8_t *currDuty)
{
  return NONE;
}

State currStateKey2On(bool *keyState, unsigned char *timestamp, uint8_t *currDuty)
{
  State nextState = KEY2_ON; 

  (*timestamp)++;
  if ( !keyState[1] )
  {
    nextState = KEY2_OFF;
  }
  else if ( *timestamp > KEY_REPEAT )
  {
    // make sure we don't roll over...
    if ( *currDuty < (0xff - cycleStep) )
      *currDuty += cycleStep;
    else
      *currDuty = 0xff;
    
    *timestamp = 0;
  }

  return nextState;
}

State currStateKey2Off(bool *keyState, unsigned char *timestamp, uint8_t *currDuty)
{
  return NONE;
}

//
///////////////////////////////////////////////

void initWeatherDisplay()
{
  displayWipe(BLACK);

  // initially we have no data so show that fact
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE, (DISPLAY_SIZE/2) - 12, RED, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE, (DISPLAY_SIZE/2), RED, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 + FONT_SIZE, (DISPLAY_SIZE/2) - 12, BLUE, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 + FONT_SIZE, (DISPLAY_SIZE/2), BLUE, DASH_SYMBOL);

  // make a degree symbol, then a percent sign, to give meaning to the values
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE, (DISPLAY_SIZE/2) + 12, RED, DEGREE_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 + FONT_SIZE, (DISPLAY_SIZE/2) + 12, BLUE, PERCENT_SYMBOL);

  refreshRate = DISPLAY_WEATHER_MS;
}

void initAnalogDisplay()
{
  displayWipe(BLACK);

  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 20, PINK, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 8, PINK, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 4, PINK, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 16, PINK, DASH_SYMBOL);

  refreshRate = DISPLAY_ANALOG_MS;
}

void initRPMDisplay()
{
  displayWipe(BLACK);

  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 20, BLUE, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 8, BLUE, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 4, BLUE, DASH_SYMBOL);
  displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 16, BLUE, DASH_SYMBOL);

  refreshRate = DISPLAY_ANALOG_MS;
}

void drawDistance()
{
  displayWipe(BLACK);
  uint16_t colour = ORANGE;

  if (distance == DISPLAY_SIZE/2 - 1)
    colour = BLUE;

  for (int i=0; i<DISPLAY_SIZE; i++)
  {
    displayDrawPixel(i, DISPLAY_SIZE/2 - distance - 1, colour);
    displayDrawPixel(i, DISPLAY_SIZE/2 + distance, colour);
  }

  for (int i=0; i<DISPLAY_SIZE; i++)
  {
    displayDrawPixel(DISPLAY_SIZE/2 - distance - 1, i, colour);
    displayDrawPixel(DISPLAY_SIZE/2 + distance, i, colour);
  }
}

void initDistanceDisplay()
{
  distance = DISPLAY_SIZE/2 - 1;
  drawDistance();

  refreshRate = DISPLAY_ANALOG_MS;
}

void clearDisplay()
{
  switch (currMode)
  {
    case TEMP_HUMIDITY:
      initWeatherDisplay();
      break;

    case RAW_ANALOG:
      initAnalogDisplay();
      break;
    
    case MAGNET_DISTANCE:
      initDistanceDisplay();
      break;

    case FAN_RPM:
      initRPMDisplay();
      break;

    default:
      break;
  }
}

void checkButton()
{
  // change modes if requested
  if (nextMode)
  {
    nextMode = false;
    currMode = (currMode + 1) % NUM_SENSORS;
    while (!deviceIDs[currMode])
      currMode = (currMode + 1) % NUM_SENSORS;

    clearDisplay();
    newDisplayData = false;
    firstFail = false;

    // set the count to the rate so we display data immediately
    msCount = refreshRate;
  }
}

void updateDisplay()
{
  uint8_t dataOffset = 0;

  if (newDisplayData)
  {
    newDisplayData = false;
    firstFail = true;

    switch (currMode)
    {
      case TEMP_HUMIDITY: 
      {
        uint32_t avgTemperature = (msg.data[dataOffset]<<24) + (msg.data[dataOffset+1]<<16) + (msg.data[dataOffset+2]<<8) + msg.data[dataOffset+3];
        uint32_t avgHumidity = (msg.data[dataOffset+4]<<24) + (msg.data[dataOffset+5]<<16) + (msg.data[dataOffset+6]<<8) + msg.data[dataOffset+7];

        if (avgTemperature != 0)
        {
          // add 5 (fixed point with one decimal value) to round instead of truncate
          avgTemperature += 5;
          displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE, (DISPLAY_SIZE/2) - 12, RED, (avgTemperature/100)%10);
          displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE, (DISPLAY_SIZE/2), RED, (avgTemperature/10)%10);
        }

        if (avgHumidity != 0)
        {
          // add 5 (fixed point with one decimal value) to round instead of truncate
          avgHumidity += 5;
          displayDrawDigit(DISPLAY_SIZE/2 + FONT_SIZE, (DISPLAY_SIZE/2) - 12, BLUE, (avgHumidity/100)%10);
          displayDrawDigit(DISPLAY_SIZE/2 + FONT_SIZE, (DISPLAY_SIZE/2), BLUE, (avgHumidity/10)%10);
        }

        break;
      }

      case RAW_ANALOG: 
      {
        uint16_t analogStrength = (msg.data[dataOffset]<<8) + msg.data[dataOffset+1];

        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 20, PINK, analogStrength/1000);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 8, PINK, (analogStrength/100)%10);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 4, PINK, (analogStrength/10)%10);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 16, PINK, analogStrength%10);

        break;
      }

      case MAGNET_DISTANCE: 
      {
        uint16_t analogStrength = (msg.data[dataOffset]<<8) + msg.data[dataOffset+1];
        static uint8_t lastDistance = DISPLAY_SIZE/2 - 1;

        if (analogStrength<1500)
        {
          distance = analogStrength/(DISPLAY_SIZE-1);
        }
        else
        {
          distance = DISPLAY_SIZE/2 - 1;
        }

        if (lastDistance != distance)
        {
          lastDistance = distance;
          drawDistance();
        }

        break;
      }

      case FAN_RPM: 
      {
        uint16_t fanRPM = (msg.data[dataOffset]<<24) + (msg.data[dataOffset+1]<<16) + (msg.data[dataOffset+2]<<8) + msg.data[dataOffset+3];

        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 20, BLUE, fanRPM/1000);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) - 8, BLUE, (fanRPM/100)%10);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 4, BLUE, (fanRPM/10)%10);
        displayDrawDigit(DISPLAY_SIZE/2 - FONT_SIZE/2, (DISPLAY_SIZE/2) + 16, BLUE, fanRPM%10);

        break;
      }

      default: 
        break;
    }
  }

  // we can't talk to the sensor module so don't display stale data, but only once to avoid flickering
  else if (firstFail)
  {
    firstFail = false;
    clearDisplay();
  }
}

void processMsg()
{
  msg.deviceID = sensorMsg[0];
  msg.typeID = sensorMsg[1];
  msg.sensorID = sensorMsg[2];

  for (int i=0; i<PROTOCOL_DATA_BYTES; i++)
    msg.data[i] = sensorMsg[i+PROTOCOL_HEADER_BYTES];

  // make sure we have a reply for the request we made
  if (msg.typeID==MSG_REPLY && msg.sensorID==currMode) 
    newDisplayData = true;
  else
    newDisplayData = false;
}

void requestData()
{
  Packet msg;

  // clear the data buffer to avoid leaking data due to reuse
  for (int i=0; i<PROTOCOL_DATA_BYTES; i++)
    msg.data[i] = 0;

  msg.typeID = MSG_REQUEST;
  msg.deviceID = deviceIDs[currMode];
  msg.sensorID = currMode;

  // send the request and wait for a response
  uart485SendBytes((uint8_t *)&msg, PROTOCOL_HEADER_BYTES);
  if (uart485ReceiveBytes((uint8_t *)sensorMsg, PROTOCOL_PACKET_SIZE, 100))
    processMsg();
}

void sendFanCommand(uint8_t newDuty)
{
  Packet msg;

  // clear the data buffer to avoid leaking data due to reuse
  for (int i=0; i<PROTOCOL_DATA_BYTES; i++)
    msg.data[i] = 0;

  msg.typeID = MSG_COMMAND;
  msg.deviceID = deviceIDs[FAN_RPM];
  msg.sensorID = FAN_RPM;
  msg.data[0] = newDuty;

  // send the request and wait for a response
  uart485SendBytes((uint8_t *)&msg, PROTOCOL_PACKET_SIZE);
}

int main(void)
{
#ifndef NDEBUG
  for (int i=0; i<100000; i++)
  ;
#endif
  uint8_t noDataCount = 0;
  bool firstNoData = true;

  // state machine lookup table
  // note: not required in submitted solutions, just showing how it's done
  getNextState keyProcessing[NUM_STATES] = {currStateNone, currStateKey1On, currStateKey1Off, currStateKey2On, currStateKey2Off};
  State currState = NONE;
  uint8_t timestamp = 0;
  uint8_t currDuty = 0xff;
  uint8_t prevDuty = currDuty;

  bool keyState[NUM_KEYS] = {false, false};

  // NOTE: the silkscreen on the curiosity board is WRONG! it's PB4 and PB5 NOT PA4 and PA5 (for CS 3 and 2!)

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;

  // sleep to idle (wake on interrupts)
  PM_REGS->PM_SLEEPCFG = PM_SLEEPCFG_SLEEPMODE_IDLE;
  
  loadParameters();
  buttonInit();
  spiInit();
  displayInit();
  uart485Init(NULL, 0, NULL);
  heartInit();

  // we want interrupts!
  __enable_irq();

  initWeatherDisplay();

  sendFanCommand(currDuty);

  // sleep until we have an interrupt
  while (1) 
  {
    __WFI();

    msCount = elapsedMS();

    if ((msCount % LED_FLASH_MS) == 0)
    {
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
    }

    if ((msCount % CHECK_BUTTON_MS) == 0)
    {
      checkButton();
    }

    if ((msCount % PROCESS_KEYS_MS) == 0)
    {
      sampleInputs(keyState);

      // the core of the state machine is now a simple index into a table
      currState = keyProcessing[currState](keyState, &timestamp, &currDuty);

      // don't change the output generator unnecessarily.
      if (currDuty != prevDuty)
      {
        sendFanCommand(currDuty);
        prevDuty = currDuty;
      }
    }

    if ((msCount % refreshRate) == 0)
    {
      requestData();

      if (newDisplayData)
      {
        noDataCount = 0;
        firstNoData = true;
        updateDisplay();
      }
      // timeout and clear screen due to a lack of broadcast data for us to use
      else
      {
        if (noDataCount<8)
          noDataCount++;
        else if (firstNoData)
        {
          firstNoData = false;
          clearDisplay();
        }
      }
    }
  }
}
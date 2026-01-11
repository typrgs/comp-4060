#include "common.h"
#include "can.h"
#include "heart.h"
#include "net.h"

#define SENTINEL 0x7E
#define FRAME_START 0xA5
#define FRAME_END 0xA6

#define CS_PIN PORT_PB05

#define TX_POLL_TIMEOUT 50 // ms

#define CRC_INIT_SEED 0xFFFFFFFF

#define TC0_INIT_VALUE 41536

uint8_t *asyncBuf = NULL;
uint8_t asyncBufPos = 0;
CANCallback asyncDone = NULL;
uint8_t asyncSize = 0;

uint16_t tcOvfCount = 0;

typedef enum RX_RESULT
{
  NONE,
  VALID,
  INVALID,
  NUM_RESULTS
} RxResult;

volatile RxResult asyncProcessResult = NONE;

typedef enum RX_STATE
{
  IDLE,
  SEARCH,
  START,
  STUFF,
  DATA,
  END,
  NUM_STATES
} RxState;

volatile RxState rxCurrState = IDLE;

// state function prototypes
RxState rxIdle(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxSearch(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxStart(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxStuff(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxData(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxEnd(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);

// state function array
RxState (*rxStates[NUM_STATES])(uint8_t, uint8_t *, uint8_t *, uint8_t) = {rxIdle, rxSearch, rxStart, rxStuff, rxData, rxEnd};

static void tc0Init()
{
  // enables main clock to TC0 over advanced peripheral bus
  MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_TC0_Msk;

  // enables generic clock 5 to TC0 over peripheral channel
  GCLK_REGS->GCLK_PCHCTRL[9] = GCLK_PCHCTRL_CHEN_Msk | GCLK_PCHCTRL_GEN(5);

  // set data register for timing
  // by default, we have a 16-bit timer clocked at 24MHz
  // the timer overflows every 65536 ticks
  // we set the data register value of the timer to 41536 so it overflows every 1ms
  TC0_REGS->COUNT16.TC_COUNT = TC0_INIT_VALUE;
  
  // enable the overflow interrupt
  TC0_REGS->COUNT16.TC_INTENSET |= TC_INTENSET_OVF_Msk;
  
  // set higher priority than SERCOM0 RXC interrupt
  NVIC_SetPriority(TC0_IRQn, 5);

  // now we enable TC0 interrupts
  NVIC_EnableIRQ(TC0_IRQn);
}

void CANInit(uint8_t *data, uint8_t expectedSize, CANCallback done)
{
  // enable and setup generic clock 5
  GCLK_REGS->GCLK_GENCTRL[5] = GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(2);
  while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK5) == GCLK_SYNCBUSY_GENCTRL_GCLK5);

  // enable generic clock 5 to SERCOM0
  GCLK_REGS->GCLK_PCHCTRL[7] = GCLK_PCHCTRL_CHEN_Msk | GCLK_PCHCTRL_GEN(5);

  // enable main clock to SERCOM0
  MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_SERCOM0_Msk;

  // mux RX and TX pins for SERCOM0
  // PA8 corresponds to pad 0, PA9 corresponds to pad 1
  PORT_REGS->GROUP[0].PORT_PMUX[4] |= PORT_PMUX_PMUXE_C;
  PORT_REGS->GROUP[0].PORT_PMUX[4] |= PORT_PMUX_PMUXO_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[8] = PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[9] = PORT_PINCFG_PMUXEN_Msk;

  // setup CAN GPIO pins to set standard mode
  PORT_REGS->GROUP[1].PORT_DIRSET = CS_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = CS_PIN;

  // save data buffer and callback function
  asyncBuf = data;
  asyncDone = done;
  asyncSize = expectedSize;

  // setup RXC interrupt
  SERCOM0_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk;

  NVIC_SetPriority(SERCOM0_2_IRQn, 6);
  NVIC_EnableIRQ(SERCOM0_2_IRQn);

  // enable RX by default
  SERCOM0_REGS->USART_INT.SERCOM_CTRLB |= SERCOM_USART_INT_CTRLB_RXEN_Msk;

  // set mode, cmode, dord, RXPO, and TXPO
  SERCOM0_REGS->USART_INT.SERCOM_CTRLA |= 
    SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK | 
    SERCOM_USART_INT_CTRLA_CMODE_ASYNC | 
    SERCOM_USART_INT_CTRLA_DORD_LSB | 
    SERCOM_USART_INT_CTRLA_RXPO_PAD1 | 
    SERCOM_USART_INT_CTRLA_TXPO_PAD0;

  // enable
  SERCOM0_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;
  while((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) == SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk);
  
  // set baud to 0
  SERCOM0_REGS->USART_INT.SERCOM_BAUD = 0;

  // init timer to use when doing async receives
  tc0Init();
}

static void crcInit()
{
  DMAC_REGS->DMAC_CRCCTRL = DMAC_CRCCTRL_RESETVALUE;
  DMAC_REGS->DMAC_CRCCHKSUM = CRC_INIT_SEED;
  DMAC_REGS->DMAC_CRCCTRL = DMAC_CRCCTRL_CRCSRC_IO;
}

static void crcNextByte(uint8_t next)
{
  DMAC_REGS->DMAC_CRCDATAIN = next;
  
  // spin on busy bit and clear when finished
  while((DMAC_REGS->DMAC_CRCSTATUS & DMAC_CRCSTATUS_CRCBUSY_Msk) == 0);
  DMAC_REGS->DMAC_CRCSTATUS = DMAC_CRCSTATUS_CRCBUSY_Msk;
}

static uint16_t crcResult()
{
  return (uint16_t)DMAC_REGS->DMAC_CRCCHKSUM;
}

static void tc0Enable()
{
  TC0_REGS->COUNT16.TC_COUNT = TC0_INIT_VALUE;
  tcOvfCount = 0;

  // enable timer
  TC0_REGS->COUNT16.TC_CTRLA |= TC_CTRLA_ENABLE_Msk;
  while((TC0_REGS->COUNT16.TC_SYNCBUSY & TC_SYNCBUSY_ENABLE_Msk) == TC_SYNCBUSY_ENABLE_Msk);
}

static void tc0Disable()
{
  TC0_REGS->COUNT16.TC_CTRLA = 0;
}

void TC0_Handler()
{
  TC0_REGS->COUNT16.TC_INTFLAG = TC_INTFLAG_OVF_Msk;

  TC0_REGS->COUNT16.TC_COUNT = TC0_INIT_VALUE;
  tcOvfCount++;
}

bool CANSendBytes(uint8_t const * const bytes, uint16_t size)
{
  bool result = true;

  

  return result;
}


/*************** RX State Machine ******************/

RxState rxIdle(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == SENTINEL)
  {
    nextState = SEARCH;
  }

  return nextState;
}

RxState rxSearch(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == FRAME_START)
  {
    nextState = START;
    crcInit(); // Reset CRC for incoming packet
    (*bufPos) = 0;
  }
  else if(nextByte == SENTINEL)
  {
    nextState = SEARCH;
  }

  return nextState;
}

RxState rxStart(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = DATA;

  if(nextByte == SENTINEL)
  {
    nextState = STUFF;
  }
  else
  {
    if((*bufPos) < expectedSize)
    {
      buf[(*bufPos)++] = nextByte;
    }
    crcNextByte(nextByte);
  }

  return nextState;
}

RxState rxStuff(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == FRAME_START)
  {
    nextState = START;
    crcInit();
    (*bufPos) = 0;
  }
  else if(nextByte == FRAME_END)
  {
    nextState = END;
  }
  else if(nextByte == SENTINEL)
  {
    nextState = DATA;

    if((*bufPos) < expectedSize)
    {
      buf[(*bufPos)++] = nextByte;
    }
    crcNextByte(nextByte);
  }

  return nextState;
}

RxState rxData(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = DATA;

  if(nextByte == SENTINEL)
  {
    nextState = STUFF;
  }
  else
  {
    if((*bufPos) < expectedSize)
    {
      buf[(*bufPos)++] = nextByte;
    }
    crcNextByte(nextByte);
  }

  return nextState;
}

RxState rxEnd(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == SENTINEL)
  {
    nextState = SEARCH;
  }

  return nextState;
}

RxResult rxProcessState(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxResult result = NONE;
  RxState nextState = rxStates[rxCurrState](nextByte, buf, bufPos, expectedSize);

  // verify the result of processing the state machine
  if(nextState == IDLE && (rxCurrState == SEARCH || rxCurrState == START || rxCurrState == STUFF || rxCurrState == DATA))
  {
    result = INVALID;
  }
  // ensure we finish with the correct transition
  else if(nextState == END && rxCurrState == STUFF)
  {
    // verify computed CRC
    if(crcResult() == 0)
    {
      result = VALID;
    }
    else
    {
      result = INVALID;
    }
  }

  // update current state
  rxCurrState = nextState;

  return result;
}

void SERCOM0_2_Handler()
{
  // read in data
  uint8_t nextByte = (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;

  // process RX state machine
  asyncProcessResult = rxProcessState(nextByte, asyncBuf, &asyncBufPos, asyncSize);

  // clear status register
  SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;

  // if full packet received, invoke callback handler
  if(asyncProcessResult == VALID)
  {
    asyncDone();
  }
}

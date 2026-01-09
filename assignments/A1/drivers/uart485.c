#include "uart485.h"
#include "heart.h"
#include "net.h"

#define SENTINEL 0x7E
#define FRAME_START 0xA5
#define FRAME_END 0xA6

#define DE_PIN PORT_PB14
#define RE_PIN PORT_PB05

#define TX_POLL_TIMEOUT 50 // ms

#define CRC_INIT_SEED 0xFFFFFFFF

uint8_t *asyncBuf = NULL;
uint8_t asyncBufPos = 0;
uart485Callback asyncDone = NULL;
uint8_t asyncSize = 0;

typedef enum RX_RESULT
{
  NONE,
  VALID,
  INVALID,
  NUM_RESULTS
} RxResult;

RxResult asyncProcessResult = NONE;

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

RxState rxCurrState = IDLE;

// state function prototypes
RxState rxIdle(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxSearch(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxStart(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxStuff(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxData(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
RxState rxEnd(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);

// state function array
RxState (*rxStates[NUM_STATES])(uint8_t, uint8_t *, uint8_t *, uint8_t) = {rxIdle, rxSearch, rxStart, rxStuff, rxData, rxEnd};


void uart485Init(uint8_t *data, uint8_t expectedSize, uart485Callback done)
{
  // enable and setup generic clock 5
  GCLK_REGS->GCLK_GENCTRL[5] = GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(1);
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

  // setup RS-485 GPIO pins for switching between RX and TX mode
  PORT_REGS->GROUP[1].PORT_DIRSET = RE_PIN;
  PORT_REGS->GROUP[1].PORT_DIRSET = DE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = RE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = DE_PIN;

  // if parameters are passed for async receive
  if(data != NULL && expectedSize > 0 && done != NULL)
  {
    // save data buffer and callback function
    asyncBuf = data;
    asyncDone = done;
    asyncSize = expectedSize;

    // setup RXC interrupt
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk;

    NVIC_EnableIRQ(SERCOM0_2_IRQn);
  }

  // enable RX and TX
  SERCOM0_REGS->USART_INT.SERCOM_CTRLB |= SERCOM_USART_INT_CTRLB_RXEN_Msk | SERCOM_USART_INT_CTRLB_TXEN_Msk; 

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

void uart485SendBytes(uint8_t const * const bytes, uint16_t size)
{
  // wait until network is idle to start transmitting
  uint32_t endTime = elapsedMS() + TX_POLL_TIMEOUT;

  while (endTime>elapsedMS())
  {
    // if data comes in while waiting for the network to go idle, reset timeout and throw away the data
    if((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) == SERCOM_USART_INT_INTFLAG_RXC_Msk)
    {
      uint32_t data = SERCOM0_REGS->USART_INT.SERCOM_DATA;
      (void) data;
      SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;
      endTime = elapsedMS() + TX_POLL_TIMEOUT;
    }
  }

  // set RS-485 GPIO pins to enter transmit mode
  PORT_REGS->GROUP[1].PORT_OUTSET = RE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTSET = DE_PIN;

  // spin on "data register empty" flag
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0);

  // setup CRC computation
  crcInit();

  // transmit frame start byte sequence
  SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  SERCOM0_REGS->USART_INT.SERCOM_DATA = FRAME_START;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  // go through byte array, sending 1 byte at a time
  for(uint16_t i=0; i<size; i++)
  {
    // stuff a byte if equal to the sentinel
    if(bytes[i] == SENTINEL)
    {
      SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
      while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);
    }

    SERCOM0_REGS->USART_INT.SERCOM_DATA = bytes[i];
    crcNextByte(bytes[i]);
    while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);
  }

  // if the given bytes is smaller than a packet, transmit 0x00 to fill in remaining bytes
  if(size < PROTOCOL_PACKET_SIZE)
  {
    for(uint16_t i=0; i<(PROTOCOL_PACKET_SIZE - size); i++)
    {
      SERCOM0_REGS->USART_INT.SERCOM_DATA = 0;
      crcNextByte(0);
      while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);
    }
  }

  // transmit CRC
  uint16_t crc = crcResult();

  SERCOM0_REGS->USART_INT.SERCOM_DATA = crc >> 8;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  SERCOM0_REGS->USART_INT.SERCOM_DATA = crc & 0x00FF;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  // transmit frame end byte sequence
  SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);
  
  SERCOM0_REGS->USART_INT.SERCOM_DATA = FRAME_END;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  // clear RS-485 GPIO pins to enter receive mode
  PORT_REGS->GROUP[1].PORT_OUTCLR = RE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = DE_PIN;
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
  RxState nextState = DATA;

  if(nextByte == FRAME_START)
  {
    nextState = START;
  }
  else if(nextByte == FRAME_END)
  {
    nextState = END;
  }
  else if(nextByte == SENTINEL)
  {
    if((*bufPos) < expectedSize)
    {
      buf[(*bufPos)++] = nextByte;
    }
    crcNextByte(nextByte);
  }
  else
  {
    nextState = IDLE;
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
  if(nextState == IDLE && (rxCurrState == START || rxCurrState == STUFF || rxCurrState == DATA))
  {
    result = INVALID;
  }
  else if(nextState == END && rxCurrState == STUFF)
  {
    result = VALID;
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

bool uart485ReceiveBytes(uint8_t * const bytes, uint16_t size, uint16_t timeoutMS)
{
  bool result = false;
  
  RxResult processResult = NONE;
  uint8_t bytesPos = 0;
  
  while(processResult == NONE)
  {
    uint32_t currTime = elapsedMS();
    uint32_t endTime = currTime + (uint32_t)timeoutMS;

    while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) == 0 && (timeoutMS == 0 || endTime > currTime))
    {
      currTime = elapsedMS();
    }
  
    if(timeoutMS > 0 && currTime >= endTime)
    {
      processResult = INVALID;
    }
    else
    {
      // read data
      uint8_t nextByte = (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;
      
      // pass data to state machine and process
      processResult = rxProcessState(nextByte, bytes, &bytesPos, size);

      // clear status register
      SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;

    }
  }

  dbg_write_u8(bytes, size);

  if(processResult == VALID)
  {
    result = true;
  }

  return result;
}

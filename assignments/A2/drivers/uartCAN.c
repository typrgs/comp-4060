#include "common.h"
#include "uartCAN.h"
#include "heart.h"
#include "net.h"

#define SENTINEL 0x7E
#define FRAME_START 0xA5
#define FRAME_END 0xA6

#define CS_PIN PORT_PB05

#define CRC_INIT_SEED 0xFFFFFFFF

uint8_t *asyncBuf = NULL;
uint8_t asyncBufPos = 0;
uartCANCallback asyncDone = NULL;
uint8_t asyncSize = 0;

typedef enum RX_RESULT
{
  HDR_NONE,
  VALID,
  INVALID,
  NUM_RESULTS
} RxResult;

volatile RxResult asyncProcessResult = HDR_NONE;

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


void uartCANInit(uint8_t *data, uint8_t expectedSize, uartCANCallback done)
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

  // if parameters are passed for async receive
  if(data != NULL && expectedSize > 0 && done != NULL)
  {
    // save data buffer and callback function
    asyncBuf = data;
    asyncDone = done;
    asyncSize = expectedSize;

    // setup RXC interrupt
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk;

    NVIC_SetPriority(SERCOM0_2_IRQn, 6);
    NVIC_EnableIRQ(SERCOM0_2_IRQn);
  }

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

static void transmitByte(uint8_t byte)
{
  SERCOM0_REGS->USART_INT.SERCOM_DATA = byte;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);
}

static void doCarrierSensing()
{
  uint32_t rejectData;

  // wait for data
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) != 0)
    rejectData = SERCOM0_REGS->USART_INT.SERCOM_DATA;
  
  (void) rejectData;
  SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;
}

void uartCANSendBytes(uint8_t const * const bytes, uint16_t size)
{
  // disable interrupts so we aren't receiving asynchronously while trying to transmit
  if(asyncBuf != NULL && asyncSize > 0 && asyncDone != NULL)
  {
    SERCOM0_REGS->USART_INT.SERCOM_INTENCLR = SERCOM_USART_INT_INTENCLR_RXC_Msk;
  }

  doCarrierSensing();

  // switch to TX mode in CTRLB
  SERCOM0_REGS->USART_INT.SERCOM_CTRLB = SERCOM_USART_INT_CTRLB_TXEN_Msk;

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
      transmitByte(SENTINEL);
    }

    crcNextByte(bytes[i]);
    transmitByte(bytes[i]);
  }

  // if the given bytes is smaller than a packet, transmit 0x00 to fill in remaining bytes
  if(size < PROTOCOL_PACKET_SIZE)
  {
    for(uint16_t i=0; i<(PROTOCOL_PACKET_SIZE - size); i++)
    {
      crcNextByte(0);
      transmitByte(0);
    }
  }

  // transmit CRC
  uint16_t crc = crcResult();

  // make sure to stuff sentinel bytes if CRC bytes are equal to sentinel
  if((crc >> 8) == SENTINEL)
  {
    transmitByte(SENTINEL);
  }
  transmitByte((uint8_t)(crc >> 8));

  if((crc & 0x00FF) == SENTINEL)
  {
    transmitByte(SENTINEL);
  }
  transmitByte((uint8_t)(crc & 0x00FF));

  // transmit frame end byte sequence
  transmitByte(SENTINEL);
  transmitByte(FRAME_END);

  // make sure everything finishes sending
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0);

  // switch to RX mode in CTRLB
  SERCOM0_REGS->USART_INT.SERCOM_CTRLB = SERCOM_USART_INT_CTRLB_RXEN_Msk;

  // reenable interrupts
  if(asyncBuf != NULL && asyncSize > 0 && asyncDone != NULL)
  {
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk;
  }
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
  RxResult result = HDR_NONE;
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

bool uartCANReceiveBytes(uint8_t * const bytes, uint16_t size, uint16_t timeoutMS)
{
  bool result = false;
  
  RxResult processResult = HDR_NONE;
  uint8_t bytesPos = 0;
  
  // continue processing RX state machine until either a valid or invalid result is given
  while(processResult == HDR_NONE)
  {
    // set time variables for measuring timeout
    uint32_t currTime = elapsedMS();
    uint32_t endTime = currTime + (uint32_t)timeoutMS;

    while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_RXC_Msk) == 0 && (timeoutMS == 0 || endTime > currTime))
    {
      currTime = elapsedMS();
    }

    // read data
    uint8_t nextByte = (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;
    
    // clear status register
    SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;
    
    // return invalid if we timed out
    if(timeoutMS > 0 && currTime >= endTime)
    {
      processResult = INVALID;
    }
    else
    {
      // pass data to state machine and process
      processResult = rxProcessState(nextByte, bytes, &bytesPos, size);
    }
  }

  // set return value on valid receive
  if(processResult == VALID)
  {
    result = true;
  }

  return result;
}

#include "common.h"
#include "can.h"
#include "heart.h"
#include "net.h"

#define SENTINEL 0x7E
#define FRAME_START 0xA5
#define FRAME_END 0xA6

#define CS_PIN PORT_PB05

#define TX_POLL_TIMEOUT 10 // ms

#define CRC_INIT_SEED 0xFFFFFFFF

#define TC0_INIT_VALUE 41536

static uint8_t *asyncBuf = NULL;
static uint8_t asyncBufPos = 0;
static CANCallback asyncDone = NULL;
static uint8_t asyncSize = 0;

static bool sensing = false;
static bool transmitting = false;
static uint8_t byteReceived = 0;
static bool newByteReceived = false;
static bool receivedWhileSensing = false;

// doing linear backoff, start at 1 and increment on each collision
static uint8_t backoffMult = 1;
#define BACKOFF_MULT_CAP 10
#define BACKOFF_MAX 10
#define BACKOFF_MIN 1

static uint16_t tcOvfCount = 0;

typedef enum RX_RESULT
{
  NONE,
  VALID,
  INVALID,
  NUM_RESULTS
} RxResult;

static volatile RxResult asyncProcessResult = NONE;

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

static volatile RxState rxCurrState = IDLE;

// state function prototypes
static RxState rxIdle(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
static RxState rxSearch(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
static RxState rxStart(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
static RxState rxStuff(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
static RxState rxData(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);
static RxState rxEnd(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize);

// state function array
static RxState (*rxStates[NUM_STATES])(uint8_t, uint8_t *, uint8_t *, uint8_t) = {rxIdle, rxSearch, rxStart, rxStuff, rxData, rxEnd};

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

static void trngInit()
{
  TRNG_REGS->TRNG_CTRLA = TRNG_CTRLA_ENABLE_Msk;
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

  // enable both TX and RX
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

  // init timer to use when doing async receives
  tc0Init();

  // init TRNG unit to use for random backoff time
  trngInit();
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

static bool transmitByte(uint8_t byte)
{
  bool result = true;
  newByteReceived = false;

  SERCOM0_REGS->USART_INT.SERCOM_DATA = byte;
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk) == 0);

  // start timer for timeout
  tc0Enable();

  // wait for a new byte to come in, or timeout if nothing comes in
  while(!newByteReceived && tcOvfCount < TX_POLL_TIMEOUT);

  // disable timer
  tc0Disable();

  // if we timed out or the byte we got back is not what we sent, return false to indicate collision
  if(tcOvfCount >= TX_POLL_TIMEOUT || byte != byteReceived)
  {
    result = false;
  }

  return result;
}

void backoff()
{
  transmitting = false;

  // do backoff
  // wait for random data to be ready
  while((TRNG_REGS->TRNG_INTFLAG & TRNG_INTFLAG_DATARDY_Msk) == 0);
  uint32_t randVal = TRNG_REGS->TRNG_DATA;

  // get number from 1 to 10, used for ms delay
  randVal = (randVal % BACKOFF_MAX) + BACKOFF_MIN;
  randVal *= backoffMult;

  // increase backoff multiplier if not at the cap
  if(backoffMult < BACKOFF_MULT_CAP)
  {
    backoffMult++;
  }

  // start timer for timeout
  tc0Enable();

  // wait for timeout
  while(tcOvfCount < randVal);

  // disable timer when done
  tc0Disable();
}

bool CANSendBytes(uint8_t const * const bytes, uint16_t size)
{
  bool result = true;

  // wait to do carrier sensing until state machine reaches the idle RX state 
  while(rxCurrState != IDLE && rxCurrState != END)
  {
    // set flag to indicate doing carrier sensing and start timeout timer
    if(rxCurrState == IDLE || rxCurrState == END)
    {
      sensing = true;
    }
    else
    {
      sensing = false;
    }
  }  
  
  sensing = true;
  tc0Enable();

  // do carrier sensing
  while(tcOvfCount < TX_POLL_TIMEOUT)
  {
    receivedWhileSensing = false;

    // wait for a byte to come in or reach timeout
    while(!receivedWhileSensing && tcOvfCount < TX_POLL_TIMEOUT);

    // stop timer when we exit the above loop to check the ovf value without it potentially changing
    tc0Disable();

    // reset flag and ovf count if a byte is received before reaching timeout, start timer again
    if(tcOvfCount < TX_POLL_TIMEOUT)
    {
      tcOvfCount = 0;
      tc0Enable();
    }
  }
  
  // update flags in this order to prevent RX interrupt from processing any stray byte, causing the state machine to transition
  transmitting = true;
  sensing = false;

  // begin transmitting bytes
  crcInit();

  // transmit frame start byte sequence
  if(!transmitByte(SENTINEL))
    goto backoff;

  if(!transmitByte(FRAME_START))
    goto backoff;

  // go through byte array, sending 1 byte at a time
  for(uint16_t i=0; i<size; i++)
  {
    // stuff a byte if equal to the sentinel
    if(bytes[i] == SENTINEL)
    {
      if(!transmitByte(SENTINEL))
        goto backoff;
    }

    crcNextByte(bytes[i]);
    if(!transmitByte(bytes[i]))
      goto backoff;
  }

  // transmit CRC
  uint16_t crc = crcResult();

  // make sure to stuff sentinel bytes if CRC bytes are equal to sentinel
  if((crc >> 8) == SENTINEL)
  {
    if(!transmitByte(SENTINEL))
      goto backoff;
  }
  if(!transmitByte((crc >> 8)))
    goto backoff;

  if((crc & 0x00FF) == SENTINEL)
  {
    if(!transmitByte(SENTINEL))
      goto backoff;
  }
  if(!transmitByte(crc & 0x00FF))
    goto backoff;

  // transmit frame end byte sequence
  if(!transmitByte(SENTINEL))
    goto backoff;

  if(!transmitByte(FRAME_END))
    goto backoff;

  // make sure everything finishes sending
  while((SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk) == 0);

  // reset flag to finish transmitting
  transmitting = false;

  // backoff multiplier gets reset upon transmitting with no collision
  backoffMult = 1;

  // skip to end
  goto finish;

backoff:
  backoff();
  result = false;

finish:
  return result;
}


/*************** RX State Machine ******************/

static RxState rxIdle(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == SENTINEL)
  {
    nextState = SEARCH;
  }

  return nextState;
}

static RxState rxSearch(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
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

static RxState rxStart(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
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

static RxState rxStuff(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
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

static RxState rxData(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
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

static RxState rxEnd(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
{
  RxState nextState = IDLE;

  if(nextByte == SENTINEL)
  {
    nextState = SEARCH;
  }

  return nextState;
}

static RxResult rxProcessState(uint8_t nextByte, uint8_t *buf, uint8_t *bufPos, uint8_t expectedSize)
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
  byteReceived = (uint8_t)SERCOM0_REGS->USART_INT.SERCOM_DATA;
  newByteReceived = true;

  // clear status register
  SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;

  // only process bytes when not sensing or transmitting
  if(!sensing && !transmitting)
  {
    // process RX state machine
    asyncProcessResult = rxProcessState(byteReceived, asyncBuf, &asyncBufPos, asyncSize);

    // if full packet received, invoke callback handler
    if(asyncProcessResult == VALID)
    {
      asyncDone();
    }
  }
  else if(sensing)
  {
    receivedWhileSensing = true;
  }
}

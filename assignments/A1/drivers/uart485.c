#include "uart485.h"
#include "heart.h"
#include "net.h"

#define SENTINEL 0x7E
#define FRAME_START 0xA5
#define FRAME_END 0xA6

#define DE_PIN PORT_PB14
#define RE_PIN PORT_PB05

#define TX_POLL_TIMEOUT 100 // ms

uint8_t *asyncBuf = NULL;
uart485Callback asyncDone = NULL;
uint8_t asyncSize = 0;

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
  PORT_REGS->GROUP[0].PORT_PMUX[4] = PORT_PMUX_PMUXE_C;
  PORT_REGS->GROUP[0].PORT_PMUX[4] = PORT_PMUX_PMUXO_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[8] = PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[9] = PORT_PINCFG_PMUXEN_Msk;

  // setup RS-485 GPIO pins for switching between RX and TX mode
  PORT_REGS->GROUP[1].PORT_DIRSET = DE_PIN;
  PORT_REGS->GROUP[1].PORT_DIRSET = RE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = DE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = RE_PIN;

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
  SERCOM0_REGS->USART_INT.SERCOM_CTRLB = SERCOM_USART_INT_CTRLB_RXEN_Msk | SERCOM_USART_INT_CTRLB_TXEN_Msk; 

  // set mode, cmode, dord, RXPO, and TXPO
  SERCOM0_REGS->USART_INT.SERCOM_CTRLA = 
    SERCOM_USART_INT_CTRLA_MODE_USART_INT_CLK | 
    SERCOM_USART_INT_CTRLA_CMODE_ASYNC | 
    SERCOM_USART_INT_CTRLA_DORD_LSB | 
    SERCOM_USART_INT_CTRLA_RXPO_PAD1 | 
    SERCOM_USART_INT_CTRLA_TXPO_PAD0;

  // set baud to 0
  SERCOM0_REGS->USART_INT.SERCOM_BAUD = 0;

  // enable
  SERCOM0_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;
  while((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) == SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk);
}

void SERCOM0_2_Handler()
{
  // clear interrupt flag
  SERCOM0_REGS->USART_INT.SERCOM_INTFLAG = SERCOM_USART_INT_INTFLAG_RXC_Msk;

  // read in data 

  // process RX state machine

  // clear status register
  SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;

  // if full packet received, invoke callback handler
  asyncDone();
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
      SERCOM0_REGS->USART_INT.SERCOM_STATUS = 0xFF;
      endTime = elapsedMS() + TX_POLL_TIMEOUT;
    }
  }

  // spin on "data register empty" flag
  while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_DRE_Msk == 0);

  // set RS-485 GPIO pins to enter transmit mode
  PORT_REGS->GROUP[1].PORT_OUTSET = DE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTSET = RE_PIN;

  // transmit frame start byte sequence
  SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
  while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);

  SERCOM0_REGS->USART_INT.SERCOM_DATA = FRAME_START;
  while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);

  // go through byte array, sending 1 byte at a time
  for(uint16_t i=0; i<size; i++)
  {
    // stuff a byte if equal to the sentinel
    if(bytes[i] == SENTINEL)
    {
      SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
      while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);
    }

    SERCOM0_REGS->USART_INT.SERCOM_DATA = bytes[i];
    while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);
  }

  // if the given bytes is smaller than a packet, transmit 0x00 to fill in remaining bytes
  if(size < PROTOCOL_PACKET_SIZE)
  {
    for(uint16_t i=0; i<(PROTOCOL_PACKET_SIZE - size); i++)
    {
      SERCOM0_REGS->USART_INT.SERCOM_DATA = 0;
      while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);
    }
  }

  // transmit frame end byte sequence
  SERCOM0_REGS->USART_INT.SERCOM_DATA = SENTINEL;
  while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);
  
  SERCOM0_REGS->USART_INT.SERCOM_DATA = FRAME_END;
  while(SERCOM0_REGS->USART_INT.SERCOM_INTFLAG & SERCOM_USART_INT_INTFLAG_TXC_Msk == 0);

  // clear RS-485 GPIO pins to enter receive mode
  PORT_REGS->GROUP[1].PORT_OUTCLR = DE_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = RE_PIN;
}

bool uart485ReceiveBytes(uint8_t * const bytes, uint16_t size, uint16_t timeoutMS)
{
  bool result = false;

  

  return result;
}

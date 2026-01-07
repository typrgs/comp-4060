#include "uart485.h"

uint8_t *asyncBuf = NULL;
uart485Callback asyncDone= NULL;
uint8_t asyncSize = 0;

void uart485Init(uint8_t *data, uint8_t expectedSize, uart485Callback done)
{
  // enable and setup generic clock 2
  GCLK_REGS->GCLK_GENCTRL[5] = GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(1);
  while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK5) == GCLK_SYNCBUSY_GENCTRL_GCLK5);

  // enable generic clock 2 to SERCOM0
  GCLK_REGS->GCLK_PCHCTRL[7] = GCLK_PCHCTRL_CHEN_Msk | GCLK_PCHCTRL_GEN(5);

  // enable main clock to SERCOM0
  MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_SERCOM0_Msk;

  // mux RX and TX pins for SERCOM0
  // PA8 corresponds to pad 0, PA9 corresponds to pad 1
  PORT_REGS->GROUP[0].PORT_PMUX[4] = PORT_PMUX_PMUXE_C;
  PORT_REGS->GROUP[0].PORT_PMUX[4] = PORT_PMUX_PMUXO_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[8] = PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PINCFG[9] = PORT_PINCFG_PMUXEN_Msk;

  // if parameters passed for async receive
  if(data != NULL && expectedSize > 0 && done != NULL)
  {
    // save data buffer and callback function
    asyncBuf = data;
    asyncDone = done;
    asyncSize = expectedSize;

    // setup RXC interrupt
    SERCOM0_REGS->USART_INT.SERCOM_INTENSET = SERCOM_USART_INT_INTENSET_RXC_Msk;
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

  // enable
  SERCOM0_REGS->USART_INT.SERCOM_CTRLA |= SERCOM_USART_INT_CTRLA_ENABLE_Msk;
  while((SERCOM0_REGS->USART_INT.SERCOM_SYNCBUSY & SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk) == SERCOM_USART_INT_SYNCBUSY_ENABLE_Msk);
}

void uart485SendBytes(uint8_t const * const bytes, uint16_t size)
{

}

bool uart485ReceiveBytes(uint8_t * const bytes, uint16_t size, uint16_t timeoutMS)
{
  bool result = false;

  

  return result;
}

#include <assert.h>
#include <stm32g4xx_hal.h>
#include "pcanpro_timestamp.h"

/*
 * 32-bit microsecond timestamp.
 *
 * F4 port used TIM3 (1 MHz) + TIM9 (slave counting TIM3 overflows).
 * G4 has no TIM9, so we use TIM2 (1 MHz low word) + TIM3 (slave counting
 * TIM2 update events, high word).
 *
 * TIM clock = 160 MHz -> PSC = 159 gives 1 MHz tick.
 */

void pcan_timestamp_init( void )
{
  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  /* master TIM2: 1 MHz, generate update on overflow (TRGO = UPDATE) */
  TIM2->PSC = (160-1);               /* 160 MHz / 160 = 1 MHz */
  TIM2->CR1 &= (uint16_t)(~TIM_CR1_CKD);
  TIM2->CR1 |= TIM_CLOCKDIVISION_DIV1;
  TIM2->CR2 |= TIM_TRGO_UPDATE;

  /* slave TIM3: counts TIM2 update events (each = 65536 us) */
  TIM3->PSC = 0x0000;
  TIM3->CR1 &= (uint16_t)(~TIM_CR1_CKD);
  TIM3->CR1 |= TIM_CLOCKDIVISION_DIV1;
  TIM3->SMCR |= (TIM_TS_ITR1 | TIM_SLAVEMODE_EXTERNAL1);

  /* enable both counters */
  TIM3->CR1 |= TIM_CR1_CEN;
  TIM2->CR1 |= TIM_CR1_CEN;

  //HAL_GetTick must not return 0 to use LED events in early stage.
  HAL_IncTick();
}

uint32_t pcan_timestamp_millis( void )
{
  return HAL_GetTick();
}

uint32_t pcan_timestamp_us( void )
{
  uint32_t tim_lo, tim_hi;
  do
  {
    tim_hi = TIM3->CNT;
    tim_lo = TIM2->CNT;
  }
  while( tim_hi != TIM3->CNT );

  return (tim_hi<<16u)|tim_lo;
}

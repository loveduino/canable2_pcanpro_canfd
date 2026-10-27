#include <stm32g4xx_hal.h>
#include "pcanpro_timestamp.h"
#include "pcanpro_can.h"
#include "pcanpro_led.h"
#include "pcanpro_protocol.h"
#include "usb_device.h"
#include "io_macro.h"
#include "pcanpro_variant.h"
#include "rtt_log.h"

void Error_Handler(void)
{
  /* Unrecoverable error: wait briefly, then reboot the device. */
  HAL_Delay( 250 );
  HAL_NVIC_SystemReset();
  for(;;);
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void HAL_MspInit(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
}

static void pcan_io_config(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
}

/*
 * CANable 2.0 clock tree (no HSE crystal):
 *   HSI16 -> PLL (x /4 *80 = 320 MHz VCO)
 *     PLLR /2 -> 160 MHz SYSCLK
 *     PLLQ /4 ->  80 MHz FDCAN kernel clock (matches PEAK PCAN_UFD_CRYSTAL_HZ)
 *   HSI48 -> USB FS (CRS synchronized from USB SOF)
 */
void pcan_clock_config( void )
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /* Configure the main internal regulator output voltage */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 80;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if( HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK )
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if( HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_8 ) != HAL_OK )
  {
    Error_Handler();
  }

  /* Peripherals clocks: USB from HSI48, FDCAN from PLLQ (80 MHz) */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_FDCAN;
  PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if( HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK )
  {
    Error_Handler();
  }

  /* CRS: synchronize HSI48 from USB SOF for crystal-less USB operation */
  RCC_CRSInitTypeDef crs = {0};
  crs.Prescaler = RCC_CRS_SYNC_DIV1;
  crs.Source = RCC_CRS_SYNC_SOURCE_USB;
  crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
  crs.ErrorLimitValue = 34;
  crs.HSI48CalibrationValue = 32;
  HAL_RCCEx_CRSConfig(&crs);

  /* SysTick at highest priority so HAL_Delay/tick stays accurate while
   * USB SOF interrupts fire (matches official canable2-fw). */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/*
 * Configure the user option bytes to work around the CANable 2.0 hardware
 * design flaw where PB8 is shared between CAN_RX and BOOT0.
 *
 * The "VID_0000&PID_0005 / failed enumeration / bLength=0" symptom happens
 * because, at reset, the CAN transceiver (ADM3050) can drive CAN_RX/PB8 high
 * before the MCU samples BOOT0. That makes the MCU enter the built-in DFU
 * bootloader instead of running this application (VID_0000&PID_0005 is the ST
 * DFU bootloader ID, not this firmware's 0x0483).
 *
 * Two fixes are applied:
 *   1. nBOOT0=1, nSWBOOT0=0  -> always boot from main Flash, ignore the PB8
 *      BOOT0 pin entirely (per ST AN5094 Table 6). This is the definitive fix.
 *   2. BoR level 4 (2.8V)    -> avoid a brown-out glitch from a dipping 5V rail
 *      (poor USB hub) causing a spurious reset into the bootloader.
 */
static void pcan_option_bytes_config( void )
{
  FLASH_OBProgramInitTypeDef cfg = {0};
  HAL_FLASHEx_OBGetConfig( &cfg );

  /* Check current nBOOT0 / nSWBOOT0 / BoR state; skip programming if already correct.
   * FLASH_OPTR is a "1 = value programmed" register (readable, non-inverted). */
  uint32_t need_boot0 = ( cfg.USERConfig & FLASH_OPTR_nBOOT0_Msk ) == 0UL;          /* want nBOOT0 = 1 */
  uint32_t need_swboot = ( cfg.USERConfig & FLASH_OPTR_nSWBOOT0_Msk ) != 0UL;       /* want nSWBOOT0 = 0 */
  uint32_t need_bor    = ( cfg.USERConfig & FLASH_OPTR_BOR_LEV_Msk ) != OB_BOR_LEVEL_4;

  if( !need_boot0 && !need_swboot && !need_bor )
    return; /* nothing to do */

  if( HAL_FLASH_Unlock() != HAL_OK )
    return;
  if( HAL_FLASH_OB_Unlock() != HAL_OK )
    return;

  FLASH_OBProgramInitTypeDef prog = {0};
  prog.OptionType = OPTIONBYTE_USER;

  /* nBOOT0 = 1, nSWBOOT0 = 0 (BOOT0 taken from option bit, not the PB8 pin) */
  if( need_boot0 || need_swboot )
  {
    prog.USERType  |= OB_USER_nBOOT0 | OB_USER_nSWBOOT0;
    prog.USERConfig |= OB_nBOOT0_SET | OB_BOOT0_FROM_OB;  /* nBOOT0=1, nSWBOOT0=0 */
  }

  /* BoR level 4 (2.8V) */
  if( need_bor )
  {
    prog.USERType  |= OB_USER_BOR_LEV;
    prog.USERConfig |= OB_BOR_LEVEL_4;
  }

  HAL_FLASHEx_OBProgram( &prog );

  HAL_FLASH_OB_Lock();
  HAL_FLASH_Lock();
}

int main(void)
{
  HAL_Init();

  RTT_LOG_INIT();
  RTT_LOG( "=== PCAN-USB FD (CANable 2.0) boot ===\n" );

  pcan_clock_config();
  pcan_io_config();
  RTT_LOG( "clock + io configured\n" );

  pcan_timestamp_init();
  pcan_option_bytes_config();

  pcan_led_init();

  pcan_led_set_mode( LED_STAT, LED_MODE_BLINK_FAST, 0xFFFFFFFF );

  pcan_protocol_init();
  pcan_usb_device_init();
  RTT_LOG( "protocol + USB device init done, entering main loop\n" );

  for(;;)
  {
    pcan_usb_device_poll();
    pcan_can_poll();
    pcan_protocol_poll();
    pcan_led_poll();
  }
}

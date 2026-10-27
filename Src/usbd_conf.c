#include <stm32g4xx_hal.h>
#include <usbd_def.h>
#include <usbd_core.h>

PCD_HandleTypeDef hpcd_usb;
void Error_Handler(void);

USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status);

/* USB interrupt handlers (interrupt-driven, matches official canable2-fw).
 * CRS relies on SOF which is delivered via the USB LP interrupt. */
void USB_LP_IRQHandler(void)
{
  HAL_PCD_IRQHandler( &hpcd_usb );
}

void USB_HP_IRQHandler(void)
{
  HAL_PCD_IRQHandler( &hpcd_usb );
}

/* MSP Init */
void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB)
  {
    /* Peripheral clock enable.
     * NOTE: G4 USB DRD-FS DM/DP pins (PA11/PA12) are driven directly by the
     * USB peripheral; no GPIO alternate-function setup is required (matches
     * the official CANable 2.0 firmware). */
    __HAL_RCC_USB_CLK_ENABLE();

    HAL_NVIC_SetPriority(USB_LP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_LP_IRQn);
    HAL_NVIC_SetPriority(USB_HP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_HP_IRQn);
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB)
  {
    __HAL_RCC_USB_CLK_DISABLE();

    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
    HAL_NVIC_DisableIRQ(USB_HP_IRQn);
  }
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  /* PCD_HandleTypeDef::Setup is uint32_t[12], NOT uint8_t[8].
   *   Setup[0] = bmRequestType | bRequest<<8 | wValue<<16
   *   Setup[1] = wIndex        | wLength<<16
   * Indexing it as bytes (Setup[1] == 9) silently compares wIndex instead of
   * bRequest, which is why every earlier LED diagnosis pointed at the wrong
   * request. Unpack it properly. */
  uint32_t  w0 = hpcd->Setup[0];
  uint32_t  w1 = hpcd->Setup[1];
  uint8_t   bmRequestType = (uint8_t)( w0 & 0xFFu );
  uint8_t   bRequest      = (uint8_t)( ( w0 >> 8 ) & 0xFFu );
  uint16_t  wValue        = (uint16_t)( ( w0 >> 16 ) & 0xFFFFu );
  uint16_t  wIndex        = (uint16_t)( w1 & 0xFFFFu );
  uint16_t  wLength       = (uint16_t)( ( w1 >> 16 ) & 0xFFFFu );

  (void)wIndex; (void)wLength; (void)wValue;

  USBD_LL_SetupStage((USBD_HandleTypeDef*)hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataOutStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SOF((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;

  if ( hpcd->Init.speed != PCD_SPEED_FULL )
  {
    Error_Handler();
  }

  USBD_LL_SetSpeed((USBD_HandleTypeDef*)hpcd->pData, speed);

  USBD_LL_Reset((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Suspend((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Resume((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevConnected((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevDisconnected((USBD_HandleTypeDef*)hpcd->pData);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  hpcd_usb.pData = pdev;
  pdev->pData = &hpcd_usb;

  /* Init USB Ip. */
  hpcd_usb.Instance = USB;
  hpcd_usb.Init.dev_endpoints = 8;
  hpcd_usb.Init.speed = PCD_SPEED_FULL;
  hpcd_usb.Init.phy_itface = PCD_PHY_EMBEDDED;
  /* SOF MUST be enabled: CANable 2.0 runs crystal-less, and the CRS
   * (Clock Recovery System) relies on SOF interrupts to keep HSI48
   * locked to the host's USB frame rate. Disabling it makes the USB
   * clock drift and enumeration fail (matches official canable2-fw). */
  hpcd_usb.Init.Sof_enable = ENABLE;
  hpcd_usb.Init.low_power_enable = DISABLE;
  hpcd_usb.Init.lpm_enable = DISABLE;
  hpcd_usb.Init.battery_charging_enable = DISABLE;

  if( HAL_PCD_Init( &hpcd_usb ) != HAL_OK )
  {
    Error_Handler( );
  }

  /*
   * G4 USB device uses a shared 1K packet buffer (PMA).  CRITICAL: the start
   * of the PMA (offset 0) holds the BTABLE (endpoint buffer-descriptor table:
   * 8 bytes per endpoint, one entry per endpoint direction), which the USB
   * peripheral uses internally.  Data buffers MUST NOT start at offset 0 or
   * they overwrite BTABLE and EP0 fails -> the host can never read the device
   * descriptor (bLength=0).  The official canable2-fw starts at 0x18 for this
   * reason; we start at 0x40 to fully clear the 8-endpoint BTABLE area.
   *
   * Layout (each buffer is 64 bytes = max bulk packet size):
   *   EP0 OUT @ 0x40,  EP0 IN @ 0x80,
   *   EP1 OUT (CMDOUT) @ 0xC0, EP1 IN (CMDIN) @ 0x100,
   *   EP2 OUT (MSGOUT) @ 0x140, EP2 IN (MSGIN) @ 0x180,
   *   EP3 OUT (MSGOUT2) @ 0x1C0, EP3 IN (MSGIN2) @ 0x200.
   */
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x00, PCD_SNG_BUF, 0x40);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x80, PCD_SNG_BUF, 0x80);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x01, PCD_SNG_BUF, 0xC0);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x81, PCD_SNG_BUF, 0x100);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x02, PCD_SNG_BUF, 0x140);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x82, PCD_SNG_BUF, 0x180);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x03, PCD_SNG_BUF, 0x1C0);
  HAL_PCDEx_PMAConfig(&hpcd_usb, 0x83, PCD_SNG_BUF, 0x200);

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_DeInit(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_Start(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_Stop(pdev->pData);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Close(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Flush(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_SetStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef*) pdev->pData;

  if((ep_addr & 0x80) == 0x80)
  {
    return hpcd->IN_ep[ep_addr & 0x7F].is_stall;
  }
  else
  {
    return hpcd->OUT_ep[ep_addr & 0x7F].is_stall;
  }
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_SetAddress(pdev->pData, dev_addr);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
  HAL_StatusTypeDef hal_status = HAL_OK;
  USBD_StatusTypeDef usb_status = USBD_OK;

  hal_status = HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);

  usb_status =  USBD_Get_USB_Status(hal_status);

  return usb_status;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

USBD_StatusTypeDef USBD_Get_USB_Status(HAL_StatusTypeDef hal_status)
{
  USBD_StatusTypeDef usb_status = USBD_OK;

  switch (hal_status)
  {
    case HAL_OK :
      usb_status = USBD_OK;
    break;
    case HAL_ERROR :
      usb_status = USBD_FAIL;
    break;
    case HAL_BUSY :
      usb_status = USBD_BUSY;
    break;
    case HAL_TIMEOUT :
      usb_status = USBD_FAIL;
    break;
    default :
      usb_status = USBD_FAIL;
    break;
  }
  return usb_status;
}

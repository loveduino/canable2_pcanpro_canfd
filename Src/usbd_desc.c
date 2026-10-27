#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"


#define USBD_VID                        0x0C72
#define USBD_LANGID_STRING              1033

/*
 * These strings are matched against what a genuine PEAK adapter reports
 * (verified from `lsusb -v` of a real device):
 *     iManufacturer 1  "PEAK-System Technik GmbH"
 *     iProduct      2  "PCAN-USB Pro FD" / "PCAN-USB FD" / ...
 *     iInterface    5  "PCAN-USB Pro FD CAN" / "PCAN-USB FD CAN"
 *
 * A single PCAN_USB.SYS serves six different products (PCAN-USB, PCAN-USB Hub,
 * PCAN-USB Pro FD, PCAN-USB FD, PCAN-Chip USB, PCAN-USB X6, PCAN-USB XL) and
 * the INF only names the service - there is no per-model registry key - so the
 * driver has to identify the model from the descriptors. The upstream project
 * shipped "XCAN..." placeholders here; with those the driver bailed out
 * (CM_PROB_FAILED_START / 0xC0000001) before ever selecting a configuration.
 */
#define USBD_MANUFACTURER_STRING        "PEAK-System Technik GmbH"
#if  PCAN_PRO_FD
#define USBD_PID_HS                     0x0011
#define USBD_PRODUCT_STRING_HS          "PCAN-USB Pro FD"
#define USBD_INTERFACE_CAN_STRING_HS    "PCAN-USB Pro FD CAN"
#define USBD_INTERFACE_LIN_STRING_HS    "PCAN-USB Pro FD LIN"
#elif PCAN_FD
#define USBD_PID_HS                     0x0012
#define USBD_PRODUCT_STRING_HS          "PCAN-USB FD"
#define USBD_INTERFACE_CAN_STRING_HS    "PCAN-USB FD CAN"
#define USBD_INTERFACE_LIN_STRING_HS    "PCAN-USB FD LIN"
#elif PCAN_PRO
#define USBD_PID_HS                     0x000d
#define USBD_PRODUCT_STRING_HS          "PCAN-USB-PRO"
#define USBD_INTERFACE_CAN_STRING_HS    "PCAN-USB-PRO CAN Device"
#define USBD_INTERFACE_LIN_STRING_HS    "PCAN-USB-PRO LIN Device"
#elif PCAN_X6
#define USBD_PID_HS                     0x0014
#define USBD_PRODUCT_STRING_HS          "PCAN-USB X6"
#define USBD_INTERFACE_CAN_STRING_HS    "PCAN-USB X6 CAN Device"
#define USBD_INTERFACE_LIN_STRING_HS    "PCAN-USB X6 LIN Device"
#else
#error Oops
#endif
#define USBD_CONFIGURATION_STRING_HS    "Config00"

/*
 * Serial number.
 *
 * The original moonglow code shipped with iSerialNumber = 0 (no serial at all).
 * Windows then builds a *location based* device instance ID
 *   USB\VID_0C72&PID_0012\5&56F4A43&0&1
 * instead of a serial based one, and the PEAK driver refused to start
 * (CM_PROB_FAILED_START / 0xC0000001) before it ever sent SET_CONFIGURATION.
 * A real PCAN-USB FD always exposes a unique serial, so we derive one from
 * the 96-bit STM32 device UID (8 hex digits, printable ASCII as required by
 * the USB-IF / Windows serial-number rules).
 */
#define USBD_SERIAL_STRING_LEN          8
static char USBD_SerialString[USBD_SERIAL_STRING_LEN + 1u] = "00000001";

static void USBD_BuildSerialString( void )
{
  uint32_t const *uid = (uint32_t const *)UID_BASE;   /* 0x1FFF7590 */
  uint32_t h = uid[0] ^ uid[1] ^ uid[2];
  static const char hexdig[] = "0123456789ABCDEF";

  for( uint32_t i = 0; i < USBD_SERIAL_STRING_LEN; i++ )
    USBD_SerialString[USBD_SERIAL_STRING_LEN - 1u - i] =
      hexdig[ ( h >> ( i * 4u ) ) & 0xFu ];

  USBD_SerialString[USBD_SERIAL_STRING_LEN] = '\0';
}


#define USB_SIZ_BOS_DESC                0x0C

#if defined ( __ICCARM__ )
  #pragma data_alignment=4
#endif
/** USB standard device descriptor. */
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
  0x12,                       /*bLength */
  USB_DESC_TYPE_DEVICE,       /*bDescriptorType*/
#if (USBD_LPM_ENABLED == 1)
  0x01,                       /*bcdUSB */ /* changed to USB version 2.01
                                             in order to support LPM L1 suspend
                                             resume test of USBCV3.0*/
#else
  0x00,                       /*bcdUSB */
#endif
  0x02,
  0,                          /*bDeviceClass*/
  0,                          /*bDeviceSubClass*/
  0,                          /*bDeviceProtocol*/
  USB_MAX_EP0_SIZE,           /*bMaxPacketSize*/
  LOBYTE(USBD_VID),           /*idVendor*/
  HIBYTE(USBD_VID),           /*idVendor*/
  LOBYTE(USBD_PID_HS),        /*idProduct*/
  HIBYTE(USBD_PID_HS),        /*idProduct*/
  0x00,                       /*bcdDevice rel. 0.00*/
  0x00,
  USBD_IDX_MFC_STR,           /*Index of manufacturer  string*/
  USBD_IDX_PRODUCT_STR,       /*Index of product string*/
  USBD_IDX_SERIAL_STR,        /*Index of serial number string*/
  USBD_MAX_NUM_CONFIGURATION  /*bNumConfigurations*/
};

/** BOS descriptor. */
#if (USBD_LPM_ENABLED == 1)
#if defined ( __ICCARM__ )
  #pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_BOSDesc[USB_SIZ_BOS_DESC] __ALIGN_END =
{
  0x5,
  USB_DESC_TYPE_BOS,
  0xC,
  0x0,
  0x1,  /* 1 device capability */
        /* device capability */
  0x7,
  USB_DEVICE_CAPABITY_TYPE,
  0x2,
  0x2,  /*LPM capability bit set */
  0x0,
  0x0,
  0x0
};
#endif

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */

/** USB lang indentifier descriptor. */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC,
     USB_DESC_TYPE_STRING,
     LOBYTE(USBD_LANGID_STRING),
     HIBYTE(USBD_LANGID_STRING)
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/* Internal string descriptor. */
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;


uint8_t * USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_DeviceDesc);
  return USBD_DeviceDesc;
}

uint8_t * USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

uint8_t * USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

uint8_t * USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

uint8_t * USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_BuildSerialString();
  USBD_GetString((uint8_t *)USBD_SerialString, USBD_StrDesc, length);
  return USBD_StrDesc;
}

uint8_t * USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == USBD_SPEED_HIGH)
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

uint8_t * USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if( speed == 0 )
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_CAN_STRING_HS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_CAN_STRING_HS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

#if (USBD_CLASS_USER_STRING_DESC == 1)
static uint8_t *USBD_UserStrDescriptor( USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length )
{
  __ALIGN_BEGIN static uint8_t USBD_StrDesc[64] __ALIGN_END;

  UNUSED( pdev );

  switch( index )
  {
    case 6:
      USBD_GetString((uint8_t *)USBD_INTERFACE_LIN_STRING_HS, USBD_StrDesc, length );
      break;
    default:
      USBD_GetString((uint8_t *)"UNHANDLED", USBD_StrDesc, length );
      break;
  }
  return USBD_StrDesc;
}
#endif

#if (USBD_LPM_ENABLED == 1)
static uint8_t * USBD_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_BOSDesc);
  return (uint8_t*)USBD_BOSDesc;
}
#endif

USBD_DescriptorsTypeDef usbd_desc =
{
  .GetDeviceDescriptor = USBD_DeviceDescriptor,
  .GetLangIDStrDescriptor = USBD_LangIDStrDescriptor,
  .GetManufacturerStrDescriptor = USBD_ManufacturerStrDescriptor,
  .GetProductStrDescriptor = USBD_ProductStrDescriptor,
  .GetSerialStrDescriptor = USBD_SerialStrDescriptor,
  .GetConfigurationStrDescriptor = USBD_ConfigStrDescriptor,
  .GetInterfaceStrDescriptor = USBD_InterfaceStrDescriptor,
#if (USBD_CLASS_USER_STRING_DESC == 1)
  .GetUserStrDescriptor = USBD_UserStrDescriptor,
#endif
#if (USBD_LPM_ENABLED == 1)
  USBD_USR_BOSDescriptor,
#endif
};

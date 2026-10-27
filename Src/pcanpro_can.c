#include <assert.h>
#include "io_macro.h"
#include "pcanpro_timestamp.h"
#include "pcanpro_can.h"
#include "pcanpro_variant.h"
#include "rtt_log.h"
#include "stm32g4xx_hal.h"

/*
 * CANable 2.0 port: single FDCAN1 channel.
 *
 * Only CAN_BUS_1 is backed by real hardware (FDCAN1).  The two-bus API
 * surface has been reduced to a single bus (CAN_BUS_TOTAL == 1).
 */

#define CAN_WITHOUT_ISR 1

/* FDCAN kernel clock: PLLQ = 80 MHz (matches PEAK PCAN_UFD_CRYSTAL_HZ) */
#define FDCAN_KERNEL_CLK (80000000u)

/* Nominal bit timing: 1 sync + 14 seg1 + 2 seg2 = 17 tq */
#define FDCAN_NOM_TS1 (14u)
#define FDCAN_NOM_TS2 (2u)
#define FDCAN_NOM_TQ  (1u + FDCAN_NOM_TS1 + FDCAN_NOM_TS2)

static FDCAN_HandleTypeDef hfdcan;

static struct t_can_dev
{
  void *dev;
  uint32_t tx_msgs;
  uint32_t tx_errs;
  uint32_t tx_ovfs;

  uint32_t rx_msgs;
  uint32_t rx_errs;
  uint32_t rx_ovfs;

  struct t_can_msg tx_fifo[64];
  uint32_t tx_head;
  uint32_t tx_tail;
  uint32_t esr_reg;
  int (*rx_isr)( uint8_t, struct  t_can_msg* );
  int (*tx_isr)( uint8_t, struct  t_can_msg* );
  void (*err_handler)( int bus, uint32_t esr );

  uint32_t nominal_prescaler;
  uint32_t data_prescaler;
  uint8_t  nom_tseg1;
  uint8_t  nom_tseg2;
  uint8_t  nom_sjw;
  uint8_t  data_tseg1;
  uint8_t  data_tseg2;
  uint8_t  data_sjw;
  uint8_t  silent;
  uint8_t  loopback;
  uint8_t  bus_active;
  uint8_t  started;
}
can_dev_array[CAN_BUS_TOTAL] =
{
  [CAN_BUS_1] = { .dev = &hfdcan,
                  .nom_tseg1 = FDCAN_NOM_TS1, .nom_tseg2 = FDCAN_NOM_TS2, .nom_sjw = 1,
                  .data_tseg1 = FDCAN_NOM_TS1, .data_tseg2 = FDCAN_NOM_TS2, .data_sjw = 1 },
};

uint32_t pcan_can_msg_time( const struct t_can_msg *pmsg, uint32_t nt, uint32_t dt )
{
  const uint32_t data_bits = pmsg->size<<3;
  const uint32_t control_bits = ( pmsg->flags & MSG_FLAG_EXT ) ? 67:47;

  if( pmsg->flags & MSG_FLAG_BRS )
    return (control_bits*nt) + (data_bits*dt);
  else
    return (control_bits+data_bits)*nt;
}

static uint32_t _nominal_prescaler_for( uint32_t bitrate )
{
  /* prescaler = FDCAN_KERNEL_CLK / (bitrate * tq_per_bit) */
  return ( FDCAN_KERNEL_CLK / ( bitrate * FDCAN_NOM_TQ ) );
}

int pcan_can_set_filter_mask( int bus, int num, int format, uint32_t id, uint32_t mask )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;
  FDCAN_FilterTypeDef filter = { 0 };

  (void)num;

  if( !p_can )
    return 0;

  if( format == MSG_FLAG_EXT )
  {
    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = id;
    filter.FilterID2 = mask;
  }
  else
  {
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = id;
    filter.FilterID2 = mask;
  }

  if( HAL_FDCAN_ConfigFilter( p_can, &filter ) != HAL_OK )
    return -1;

  /* Accept ALL non-matching frames into FIFO0 (bridge/sniffer behaviour).
   * Using FDCAN_REJECT here would silently drop every frame because the
   * standard/extended filter is a single accept-everything entry. */
  if( HAL_FDCAN_ConfigGlobalFilter( p_can, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                    FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE ) != HAL_OK )
    return -1;

  return 0;
}

int pcan_can_filter_init_stdid_list( int bus, const uint16_t *id_list, int id_len  )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;
  FDCAN_FilterTypeDef filter = { 0 };
  int i;

  (void)id_len;

  if( !p_can )
    return 0;

  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0;
  filter.FilterType = FDCAN_FILTER_RANGE;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = 0;
  filter.FilterID2 = 0x7FF;

  /* accept-everything fallback when list is empty; otherwise install first id */
  if( id_len > 0 && id_list )
  {
    filter.FilterType = FDCAN_FILTER_RANGE;
    filter.FilterID1 = id_list[0];
    filter.FilterID2 = id_list[0];
  }

  (void)i;

  if( HAL_FDCAN_ConfigFilter( p_can, &filter ) != HAL_OK )
    return -1;

  return id_len;
}

static int _can_send( FDCAN_HandleTypeDef *p_can, struct t_can_msg *p_msg )
{
  FDCAN_TxHeaderTypeDef hdr = { 0 };

  if( HAL_FDCAN_GetTxFifoFreeLevel( p_can ) == 0 )
    return -1;

  if( p_msg->flags & MSG_FLAG_EXT )
  {
    hdr.IdType = FDCAN_EXTENDED_ID;
    hdr.Identifier = p_msg->id & 0x1FFFFFFF;
  }
  else
  {
    hdr.IdType = FDCAN_STANDARD_ID;
    hdr.Identifier = p_msg->id & 0x7FF;
  }

  hdr.TxFrameType = (p_msg->flags & MSG_FLAG_RTR) ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
  hdr.DataLength = (p_msg->size > 8) ? ((p_msg->size == 12) ? FDCAN_DLC_BYTES_12 :
                  (p_msg->size == 16) ? FDCAN_DLC_BYTES_16 :
                  (p_msg->size == 20) ? FDCAN_DLC_BYTES_20 :
                  (p_msg->size == 24) ? FDCAN_DLC_BYTES_24 :
                  (p_msg->size == 32) ? FDCAN_DLC_BYTES_32 :
                  (p_msg->size == 48) ? FDCAN_DLC_BYTES_48 :
                  FDCAN_DLC_BYTES_64) : (uint32_t)p_msg->size;

  hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  hdr.BitRateSwitch = (p_msg->flags & MSG_FLAG_BRS) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
  hdr.FDFormat = (p_msg->flags & MSG_FLAG_FD) ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
  hdr.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  hdr.MessageMarker = 0;

  if( HAL_FDCAN_AddMessageToTxFifoQ( p_can, &hdr, p_msg->data ) != HAL_OK )
    return -1;

  return 0;
}

static void pcan_can_flush_tx( int bus )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  struct t_can_msg *p_msg;

  if( p_dev->tx_head == p_dev->tx_tail )
    return;

  if( !p_dev->dev )
    return;

  if( !p_dev->bus_active )
    return;

  p_msg = &p_dev->tx_fifo[p_dev->tx_tail];
  if( _can_send( p_dev->dev, p_msg ) < 0 )
    return;

  if( p_dev->tx_isr )
  {
    (void)p_dev->tx_isr( bus, p_msg );
  }

  p_dev->tx_tail = (p_dev->tx_tail+1)&(64-1);
}

int pcan_can_write( int bus, struct t_can_msg *p_msg )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];

  if( !p_dev->dev )
    return 0;

  if( !p_msg )
    return 0;

  uint32_t  tx_head_next = (p_dev->tx_head+1)&(64-1);
  if( tx_head_next == p_dev->tx_tail )
  {
    ++p_dev->tx_ovfs;
    return -1;
  }

  p_dev->tx_fifo[p_dev->tx_head] = *p_msg;
  p_dev->tx_head = tx_head_next;

  return 0;
}

void pcan_can_install_rx_callback( int bus, int (*cb)( uint8_t, struct  t_can_msg* ) )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  p_dev->rx_isr = cb;
}

void pcan_can_install_tx_callback( int bus, int (*cb)( uint8_t, struct  t_can_msg* ) )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  p_dev->tx_isr = cb;
}

void pcan_can_install_err_callback( int bus, void (*cb)( int , uint32_t ) )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  p_dev->err_handler = cb;
}

/*
 * Transmitter delay compensation (TDC).
 *
 * Without TDC the controller checks the bit it just transmitted at the
 * primary sample point of the data phase.  When the data bit time gets
 * shorter than the CAN transceiver loop delay (~200-300 ns for common
 * 3.3 V transceivers), the transmitted bit has not yet travelled back
 * through the transceiver at that moment and every FD/BRS frame raises a
 * bit error -> TX (and with it the whole exchange) breaks.
 *
 * A 5 Mbit/s data phase @ 80 MHz kernel clock is 16 mtq = 200 ns, i.e.
 * shorter than the loop delay -> TDC mandatory.  2 Mbit/s is 500 ns and
 * still exceeds the loop delay, which is why it worked all along.
 *
 * Follows the Linux m_can driver ("Bit Time Requirements for CAN FD",
 * iCAN Conference 2013): enable TDC above 2.5 Mbit/s and place the
 * secondary sample point at the data-phase sample point.
 */
#define FDCAN_TDC_MIN_BITRATE   (2500000u)

static void _fdcan_apply_tdc( FDCAN_HandleTypeDef *p_can, struct t_can_dev *p_dev )
{
  uint32_t dtq, bitrate, ssp_permille, tdco;

  if( p_dev->data_prescaler == 0 )
    return;

  /* data bit time in mtq (one fdcan_tq_ck period = 12.5 ns @ 80 MHz) */
  dtq = p_dev->data_prescaler * ( 1u + p_dev->data_tseg1 + p_dev->data_tseg2 );
  bitrate = FDCAN_KERNEL_CLK / dtq;

  if( bitrate <= FDCAN_TDC_MIN_BITRATE )
    return;   /* DBTP.TDC stays 0 (cleared by HAL_FDCAN_Init) */

  /* data-phase sample point in permille, Linux can_bittiming convention */
  ssp_permille = ( 1000u * ( 1u + p_dev->data_tseg1 ) ) / ( 1u + p_dev->data_tseg1 + p_dev->data_tseg2 );

  /* Bosch M_CAN TDC equation: SSP offset = measured loop delay + TDCO,
   * with TDCO chosen so the SSP lands at the data sampling point. */
  tdco = ( FDCAN_KERNEL_CLK / 1000u ) * ssp_permille / bitrate;

  if( tdco > 0x7Fu )
    tdco = 0x7Fu;

  /* TDCF (measurement filter window) left at 0, as in Linux m_can */
  if( HAL_FDCAN_ConfigTxDelayCompensation( p_can, tdco, 0u ) != HAL_OK )
    return;

  if( HAL_FDCAN_EnableTxDelayCompensation( p_can ) != HAL_OK )
    return;

  RTT_LOG( "TDC on: data_bitrate=%u dtq=%u ssp=%u/1000 tdco=%u\n",
           bitrate, dtq, ssp_permille, tdco );
}

static int _fdcan_apply_config( FDCAN_HandleTypeDef *p_can, struct t_can_dev *p_dev )
{
  p_can->Instance = FDCAN1;
  p_can->Init.ClockDivider = FDCAN_CLOCK_DIV1;
  p_can->Init.FrameFormat = FDCAN_FRAME_FD_BRS;
  p_can->Init.Mode = p_dev->loopback ? FDCAN_MODE_INTERNAL_LOOPBACK :
                     (p_dev->silent ? FDCAN_MODE_BUS_MONITORING : FDCAN_MODE_NORMAL);
  p_can->Init.AutoRetransmission = DISABLE;
  p_can->Init.TransmitPause = DISABLE;
  p_can->Init.ProtocolException = DISABLE;
  p_can->Init.NominalPrescaler = p_dev->nominal_prescaler;
  p_can->Init.NominalSyncJumpWidth = p_dev->nom_sjw;
  p_can->Init.NominalTimeSeg1 = p_dev->nom_tseg1;
  p_can->Init.NominalTimeSeg2 = p_dev->nom_tseg2;
  p_can->Init.DataPrescaler = p_dev->data_prescaler;
  p_can->Init.DataSyncJumpWidth = p_dev->data_sjw;
  p_can->Init.DataTimeSeg1 = p_dev->data_tseg1;
  p_can->Init.DataTimeSeg2 = p_dev->data_tseg2;
  /* One standard + one extended filter element, so HAL_FDCAN_ConfigFilter can
   * actually write them (it asserts FilterIndex+1 <= StdFiltersNbr).  The
   * global filter below routes non-matching frames to FIFO0 anyway, so every
   * frame on the bus is delivered to the host (correct for a protocol bridge).
   */
  p_can->Init.StdFiltersNbr = 1;
  p_can->Init.ExtFiltersNbr = 1;
  p_can->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

  {
    HAL_StatusTypeDef ret = HAL_FDCAN_Init( p_can );
    if( ret != HAL_OK )
      return -1;
  }

  /* Transmitter delay compensation must be configured after Init (which
   * resets DBTP.TDC) and before Start, while the handle is in READY state. */
  _fdcan_apply_tdc( p_can, p_dev );

  return 0;
}

int pcan_can_init_ex( int bus, uint32_t bitrate )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return 0;

  if( p_dev->nominal_prescaler == 0 )
    p_dev->nominal_prescaler = _nominal_prescaler_for( bitrate );
  if( p_dev->data_prescaler == 0 )
    p_dev->data_prescaler = _nominal_prescaler_for( 2000000u );

  p_dev->silent = 0;
  p_dev->loopback = 0;
  p_dev->bus_active = 1;

  if( _fdcan_apply_config( p_can, p_dev ) != HAL_OK )
    return -1;

  if( HAL_FDCAN_Start( p_can ) != HAL_OK )
    return -1;

  p_dev->started = 1;

  return 0;
}

void pcan_can_set_silent( int bus, uint8_t silent_mode )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  p_dev->silent = silent_mode;
  if( p_dev->started )
  {
    HAL_FDCAN_Stop( p_can );
    _fdcan_apply_config( p_can, p_dev );
    HAL_FDCAN_Start( p_can );
  }
}

/* FDCAN supports ISO mode switch natively via ISO bit timing */
void pcan_can_set_iso_mode( int bus, uint8_t iso_mode )
{
  (void)bus;
  (void)iso_mode;
}

void pcan_can_set_loopback( int bus, uint8_t loopback )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  p_dev->loopback = loopback;
  if( p_dev->started )
  {
    HAL_FDCAN_Stop( p_can );
    _fdcan_apply_config( p_can, p_dev );
    HAL_FDCAN_Start( p_can );
  }
}

void pcan_can_set_bus_active( int bus, uint16_t mode )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  if( mode )
  {
    p_dev->bus_active = 1;
    if( !p_dev->started )
    {
      _fdcan_apply_config( p_can, p_dev );
      HAL_FDCAN_Start( p_can );
      p_dev->started = 1;
    }
  }
  else
  {
    p_dev->bus_active = 0;
    HAL_FDCAN_Stop( p_can );
    p_dev->started = 0;
  }
}

void pcan_can_set_bitrate( int bus, uint32_t bitrate, int is_data_bitrate )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  if( is_data_bitrate )
  {
    p_dev->data_prescaler = _nominal_prescaler_for( bitrate );
    return;
  }

  p_dev->nominal_prescaler = _nominal_prescaler_for( bitrate );

  if( p_dev->started )
  {
    HAL_FDCAN_Stop( p_can );
    _fdcan_apply_config( p_can, p_dev );
    HAL_FDCAN_Start( p_can );
  }
}

void pcan_can_set_bitrate_ex( int bus, uint16_t brp, uint8_t tseg1, uint8_t tseg2, uint8_t sjw )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  /*
   * The driver's brp/tseg1/tseg2 are expressed against an 80 MHz reference
   * (PCAN_UFD_CRYSTAL_HZ), and our FDCAN kernel clock is now also 80 MHz
   * (PLLQ), so the values apply verbatim with no rescaling.
   */
  if( brp == 0 )
    brp = 1;

  p_dev->nominal_prescaler = brp;
  p_dev->nom_tseg1 = tseg1;
  p_dev->nom_tseg2 = tseg2;
  p_dev->nom_sjw = sjw ? sjw : 1;

  if( p_dev->started )
  {
    HAL_FDCAN_Stop( p_can );
    _fdcan_apply_config( p_can, p_dev );
    HAL_FDCAN_Start( p_can );
  }
}

void pcan_can_set_data_bitrate_ex( int bus, uint16_t brp, uint8_t tseg1, uint8_t tseg2, uint8_t sjw )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];
  FDCAN_HandleTypeDef *p_can = p_dev->dev;

  if( !p_can )
    return;

  if( brp == 0 )
    brp = 1;

  p_dev->data_prescaler = brp;
  p_dev->data_tseg1 = tseg1;
  p_dev->data_tseg2 = tseg2;
  p_dev->data_sjw = sjw ? sjw : 1;

  if( p_dev->started )
  {
    HAL_FDCAN_Stop( p_can );
    _fdcan_apply_config( p_can, p_dev );
    HAL_FDCAN_Start( p_can );
  }
}

int pcan_can_stats( int bus, struct t_can_stats *p_stats )
{
  struct t_can_dev *p_dev = &can_dev_array[bus];

  p_stats->tx_msgs = p_dev->tx_msgs;
  p_stats->tx_errs = p_dev->tx_errs;
  p_stats->rx_msgs = p_dev->rx_msgs;
  p_stats->rx_errs = p_dev->rx_errs;
  p_stats->rx_ovfs = p_dev->rx_ovfs;

  return sizeof( struct t_can_stats );
}

static uint32_t _dlc_to_size( uint32_t dlc )
{
  switch( dlc )
  {
    case FDCAN_DLC_BYTES_0:  return 0;
    case FDCAN_DLC_BYTES_1:  return 1;
    case FDCAN_DLC_BYTES_2:  return 2;
    case FDCAN_DLC_BYTES_3:  return 3;
    case FDCAN_DLC_BYTES_4:  return 4;
    case FDCAN_DLC_BYTES_5:  return 5;
    case FDCAN_DLC_BYTES_6:  return 6;
    case FDCAN_DLC_BYTES_7:  return 7;
    case FDCAN_DLC_BYTES_8:  return 8;
    case FDCAN_DLC_BYTES_12: return 12;
    case FDCAN_DLC_BYTES_16: return 16;
    case FDCAN_DLC_BYTES_20: return 20;
    case FDCAN_DLC_BYTES_24: return 24;
    case FDCAN_DLC_BYTES_32: return 32;
    case FDCAN_DLC_BYTES_48: return 48;
    case FDCAN_DLC_BYTES_64: return 64;
    default: return 8;
  }
}

static void pcan_can_isr_frame( FDCAN_HandleTypeDef *hcan )
{
  FDCAN_RxHeaderTypeDef hdr = { 0 };
  const int bus = CAN_BUS_1;
  struct t_can_dev * const p_dev = &can_dev_array[bus];
  struct t_can_msg  msg = { 0 };

  (void)hcan;

  if( HAL_FDCAN_GetRxMessage( &hfdcan, FDCAN_RX_FIFO0, &hdr, msg.data ) != HAL_OK )
    return;

  msg.size = _dlc_to_size( hdr.DataLength );

  if( hdr.IdType == FDCAN_STANDARD_ID )
  {
    msg.id = hdr.Identifier;
  }
  else
  {
    msg.id = hdr.Identifier;
    msg.flags |= MSG_FLAG_EXT;
  }

  if( hdr.RxFrameType == FDCAN_REMOTE_FRAME )
  {
    msg.flags |= MSG_FLAG_RTR;
  }
  if( hdr.BitRateSwitch == FDCAN_BRS_ON )
  {
    msg.flags |= MSG_FLAG_BRS;
  }
  if( hdr.FDFormat == FDCAN_FD_CAN )
  {
    msg.flags |= MSG_FLAG_FD;
  }
  if( hdr.ErrorStateIndicator == FDCAN_ESI_PASSIVE )
  {
    msg.flags |= MSG_FLAG_ESI;
  }

  msg.timestamp = pcan_timestamp_us();

  if( p_dev->rx_isr )
  {
    if( p_dev->rx_isr( bus, &msg ) < 0 )
    {
      ++p_dev->rx_ovfs;
      return;
    }
  }
  ++p_dev->rx_msgs;
}

void pcan_can_poll( void )
{
  static uint32_t err_last_check = 0;
  uint32_t ts_ms;

  ts_ms = pcan_timestamp_millis();

  /* drain RX FIFO (polling mode) */
  while( can_dev_array[CAN_BUS_1].started &&
         HAL_FDCAN_GetRxFifoFillLevel( &hfdcan, FDCAN_RX_FIFO0 ) > 0 )
  {
    pcan_can_isr_frame( &hfdcan );
  }

  pcan_can_flush_tx( CAN_BUS_1 );

  if( (uint32_t)( err_last_check - ts_ms ) > 250 )
  {
    err_last_check = ts_ms;
    for( int i = 0; i < CAN_BUS_TOTAL; i++ )
    {
      struct t_can_dev *p_dev = &can_dev_array[i];
      if( !p_dev->err_handler )
        continue;
      FDCAN_HandleTypeDef *pcan = p_dev->dev;
      if( !pcan )
        continue;
      uint32_t esr = HAL_FDCAN_GetError( pcan );
      if( p_dev->esr_reg != esr )
      {
        p_dev->esr_reg = esr;
        p_dev->err_handler( i, esr );
      }
    }
  }
}

/* --------------- HAL MSP PART ------------- */
void HAL_FDCAN_MspInit( FDCAN_HandleTypeDef *hfdcan )
{
  (void)hfdcan;

  __HAL_RCC_FDCAN_CLK_ENABLE();

#ifdef CAN1_RX
  PORT_ENABLE_CLOCK( PIN_PORT( CAN1_RX ), PIN_PORT( CAN1_TX ) );
  PIN_INIT( CAN1_RX );
  PIN_INIT( CAN1_TX );
#endif
}

void HAL_FDCAN_MspDeInit( FDCAN_HandleTypeDef *hfdcan )
{
  (void)hfdcan;

#ifdef CAN1_RX
  PIN_DEINIT( CAN1_RX );
  PIN_DEINIT( CAN1_TX );
#endif
  __HAL_RCC_FDCAN_CLK_DISABLE();
}

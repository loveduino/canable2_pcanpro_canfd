#pragma once

/*
 * SEGGER RTT logging helper.
 *
 * RTT (Real-Time Transfer) streams printf-style text over the J-Link debug
 * probe to the host, no extra UART/pins needed. In Ozone / J-Link RTT Viewer /
 * J-Link RTT Client open channel 0 to see the log.
 *
 * All logging is compiled out unless -DRTT_ENABLE=1 is defined, so the release
 * build carries zero overhead.
 */
#include <stdint.h>

#ifdef RTT_ENABLE
#include "SEGGER_RTT.h"

#define RTT_LOG_INIT()   SEGGER_RTT_Init()
#define RTT_LOG(...)     SEGGER_RTT_printf( 0, __VA_ARGS__ )

#else

#define RTT_LOG_INIT()            do { } while( 0 )
#define RTT_LOG(...)              do { } while( 0 )

#endif /* RTT_ENABLE */

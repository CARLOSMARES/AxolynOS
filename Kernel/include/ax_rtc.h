/*
 * AxolynOS - Reloj de tiempo real CMOS (RTC).
 */
#ifndef AX_RTC_H
#define AX_RTC_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;   /* 0 = domingo */
} AxRtcTime;

void ax_rtc_read(AxRtcTime* out);
uint64_t ax_rtc_unix(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_RTC_H */

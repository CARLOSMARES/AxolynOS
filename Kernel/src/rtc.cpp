/*
 * AxolynOS - RTC CMOS.
 */
#include "ax_rtc.h"
#include "ax_io.h"

static uint8_t rtc_read_reg(uint8_t reg)
{
    ax_outb(0x70, reg);
    return ax_inb(0x71);
}

static uint8_t rtc_bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

static bool rtc_updating(void)
{
    return (rtc_read_reg(0x0A) & 0x80) != 0;
}

void ax_rtc_read(AxRtcTime* out)
{
    if (!out) {
        return;
    }
    for (int guard = 0; guard < 100000 && rtc_updating(); ++guard) {
        ax_io_wait();
    }

    uint8_t sec = rtc_read_reg(0x00);
    uint8_t min = rtc_read_reg(0x02);
    uint8_t hour = rtc_read_reg(0x04);
    uint8_t day = rtc_read_reg(0x07);
    uint8_t mon = rtc_read_reg(0x08);
    uint8_t yr = rtc_read_reg(0x09);
    uint8_t century = rtc_read_reg(0x32);
    uint8_t weekday = rtc_read_reg(0x06);
    uint8_t regb = rtc_read_reg(0x0B);

    if ((regb & 0x04) == 0) {
        sec = rtc_bcd(sec);
        min = rtc_bcd(min);
        hour = (uint8_t)(((hour & 0x0F) + (((hour >> 4) & 0x07) * 10)) |
                         (hour & 0x80));
        day = rtc_bcd(day);
        mon = rtc_bcd(mon);
        yr = rtc_bcd(yr);
        century = rtc_bcd(century);
    }

    bool pm = (hour & 0x80) != 0;
    hour = (uint8_t)(hour & 0x7F);
    if (pm && hour < 12) {
        hour = (uint8_t)(hour + 12);
    }
    if (!pm && hour == 12 && (regb & 0x02)) {
        hour = 0;
    }

    uint16_t year;
    if (century >= 19 && century <= 21) {
        year = (uint16_t)((uint16_t)century * 100u + yr);
    } else {
        year = (uint16_t)(2000u + yr);
    }

    out->year = year;
    out->month = mon ? mon : 1;
    out->day = day ? day : 1;
    out->hour = hour;
    out->minute = min;
    out->second = sec;
    out->weekday = (uint8_t)(weekday & 0x07);
}

uint64_t ax_rtc_unix(void)
{
    AxRtcTime t;
    ax_rtc_read(&t);

    int year = (int)t.year;
    int month = (int)t.month;
    int day = (int)t.day;
    if (month <= 2) {
        year -= 1;
    }
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 +
                              day - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    int64_t seconds = days * 86400 + (int64_t)t.hour * 3600 +
                      (int64_t)t.minute * 60 + (int64_t)t.second;
    return seconds > 0 ? (uint64_t)seconds : 0;
}

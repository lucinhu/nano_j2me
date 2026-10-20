/* Shared optional wall-clock hook for the POSIX ports. */
#ifndef NJ_POSIX_CLOCK_H
#define NJ_POSIX_CLOCK_H

#include <time.h>

static int nj_posix_now(void *ud, nj_datetime *out)
{
    struct timespec ts;
    struct tm local, utc;
    int days;
    (void)ud;
    if (!out || clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
        !localtime_r(&ts.tv_sec, &local) || !gmtime_r(&ts.tv_sec, &utc)) {
        return 0;
    }
    /* The two calendar dates differ by at most one day, including across a
     * year boundary. This includes DST without nonstandard tm_gmtoff/timegm. */
    days = local.tm_yday - utc.tm_yday;
    if (days > 1) { days = -1; }
    if (days < -1) { days = 1; }
    out->year = local.tm_year + 1900;
    out->month = local.tm_mon;
    out->day = local.tm_mday;
    out->hour = local.tm_hour;
    out->minute = local.tm_min;
    out->second = local.tm_sec;
    out->millis = (nj_s4)(ts.tv_nsec / 1000000);
    out->weekday = local.tm_wday;
    out->tz_offset = days * 1440 + (local.tm_hour - utc.tm_hour) * 60
                   + local.tm_min - utc.tm_min;
    return 1;
}

#endif

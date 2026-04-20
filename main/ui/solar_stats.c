#include "solar_stats.h"
#include <string.h>

#define HOUR_BUCKET_MS  (60U * 1000U)         /* 1 minute */
#define DAY_BUCKET_MS   (5U * 60U * 1000U)    /* 5 minutes */

void solar_stats_init(solar_stats_t *s)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
}

static void push_hour(solar_stats_t *s, uint16_t w)
{
    if (s->hour_count < SOLAR_STATS_HOUR_SLOTS) {
        s->hour_buf[s->hour_count++] = w;
    } else {
        memmove(&s->hour_buf[0], &s->hour_buf[1],
                (SOLAR_STATS_HOUR_SLOTS - 1) * sizeof(uint16_t));
        s->hour_buf[SOLAR_STATS_HOUR_SLOTS - 1] = w;
    }
}

static void push_day(solar_stats_t *s, uint16_t w)
{
    if (s->day_count < SOLAR_STATS_DAY_SLOTS) {
        s->day_buf[s->day_count++] = w;
    } else {
        memmove(&s->day_buf[0], &s->day_buf[1],
                (SOLAR_STATS_DAY_SLOTS - 1) * sizeof(uint16_t));
        s->day_buf[SOLAR_STATS_DAY_SLOTS - 1] = w;
    }
}

void solar_stats_on_sample(solar_stats_t *s,
                           uint16_t pv_power_w,
                           uint32_t yield_today_centikwh,
                           uint32_t now_ms)
{
    if (s == NULL) return;

    /* Midnight rollover: Victron's today-yield drops to ~0 at local midnight.
     * Detect a significant decrease and reset today max + buffers. */
    if (s->has_data && yield_today_centikwh + 5 < s->last_yield_centikwh) {
        s->today_max_power_w = 0;
        s->hour_count = 0;
        s->day_count = 0;
    }
    s->last_yield_centikwh = yield_today_centikwh;
    s->today_yield_centikwh = yield_today_centikwh;

    if (pv_power_w > s->today_max_power_w) {
        s->today_max_power_w = pv_power_w;
    }

    if (!s->has_data) {
        s->has_data = true;
        s->hour_last_sample_ms = now_ms;
        s->day_last_sample_ms = now_ms;
        push_hour(s, pv_power_w);
        push_day(s, pv_power_w);
        return;
    }

    if ((uint32_t)(now_ms - s->hour_last_sample_ms) >= HOUR_BUCKET_MS) {
        push_hour(s, pv_power_w);
        s->hour_last_sample_ms = now_ms;
    }

    if ((uint32_t)(now_ms - s->day_last_sample_ms) >= DAY_BUCKET_MS) {
        push_day(s, pv_power_w);
        s->day_last_sample_ms = now_ms;
    }
}

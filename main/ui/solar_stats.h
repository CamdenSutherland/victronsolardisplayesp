#ifndef UI_SOLAR_STATS_H
#define UI_SOLAR_STATS_H

#include <stdbool.h>
#include <stdint.h>

#define SOLAR_STATS_HOUR_SLOTS 60   /* 1-minute buckets, ~1 hour */
#define SOLAR_STATS_DAY_SLOTS  288  /* 5-minute buckets, 24 hours */

typedef struct {
    uint16_t hour_buf[SOLAR_STATS_HOUR_SLOTS];
    uint8_t  hour_count;
    uint32_t hour_last_sample_ms;

    uint16_t day_buf[SOLAR_STATS_DAY_SLOTS];
    uint16_t day_count;
    uint32_t day_last_sample_ms;

    uint16_t today_max_power_w;
    uint32_t today_yield_centikwh;
    uint32_t last_yield_centikwh;
    bool     has_data;
} solar_stats_t;

void solar_stats_init(solar_stats_t *s);
void solar_stats_on_sample(solar_stats_t *s,
                           uint16_t pv_power_w,
                           uint32_t yield_today_centikwh,
                           uint32_t now_ms);

#endif /* UI_SOLAR_STATS_H */

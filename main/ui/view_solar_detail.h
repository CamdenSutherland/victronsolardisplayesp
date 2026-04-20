#ifndef UI_VIEW_SOLAR_DETAIL_H
#define UI_VIEW_SOLAR_DETAIL_H

#include <lvgl.h>
#include "ui_state.h"
#include "solar_stats.h"

typedef struct ui_solar_detail_view ui_solar_detail_view_t;

typedef void (*ui_solar_detail_back_cb_t)(void *user_data);

ui_solar_detail_view_t *ui_solar_detail_view_create(ui_state_t *ui,
                                                    lv_obj_t *parent,
                                                    ui_solar_detail_back_cb_t back_cb,
                                                    void *back_user_data);

void ui_solar_detail_view_show(ui_solar_detail_view_t *view,
                               const solar_stats_t *stats,
                               uint16_t current_pv_power_w);
void ui_solar_detail_view_hide(ui_solar_detail_view_t *view);
void ui_solar_detail_view_refresh(ui_solar_detail_view_t *view,
                                  const solar_stats_t *stats,
                                  uint16_t current_pv_power_w);
void ui_solar_detail_view_destroy(ui_solar_detail_view_t *view);

#endif /* UI_VIEW_SOLAR_DETAIL_H */

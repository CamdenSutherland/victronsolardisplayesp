#include "view_solar_detail.h"
#include <stdlib.h>
#include <string.h>

typedef enum {
    RANGE_HOUR = 0,
    RANGE_DAY  = 1,
} range_t;

struct ui_solar_detail_view {
    ui_state_t *ui;
    lv_obj_t *root;
    lv_obj_t *title_label;
    lv_obj_t *current_power_label;
    lv_obj_t *max_power_label;
    lv_obj_t *yield_label;
    lv_obj_t *chart;
    lv_obj_t *y_max_label;
    lv_chart_series_t *series;
    lv_obj_t *btn_hour;
    lv_obj_t *btn_day;
    lv_obj_t *btn_back;
    range_t range;
    ui_solar_detail_back_cb_t back_cb;
    void *back_user_data;

    /* Keep latest stats snapshot so tapping the toggle can re-render without new data */
    solar_stats_t latest_stats;
    uint16_t latest_power_w;
    bool has_latest;
};

static void apply_series(ui_solar_detail_view_t *v)
{
    if (v == NULL || v->chart == NULL || v->series == NULL) return;

    const solar_stats_t *s = &v->latest_stats;
    const uint16_t *buf;
    uint16_t count;
    uint16_t slots;

    if (v->range == RANGE_HOUR) {
        buf = s->hour_buf;
        count = s->hour_count;
        slots = SOLAR_STATS_HOUR_SLOTS;
    } else {
        buf = s->day_buf;
        count = s->day_count;
        slots = SOLAR_STATS_DAY_SLOTS;
    }

    lv_chart_set_point_count(v->chart, slots);

    uint16_t max_y = 10;
    for (uint16_t i = 0; i < count; i++) {
        if (buf[i] > max_y) max_y = buf[i];
    }
    /* round up to a nice ceiling */
    uint16_t ceiling = ((max_y / 50) + 1) * 50;
    lv_chart_set_range(v->chart, LV_CHART_AXIS_PRIMARY_Y, 0, ceiling);
    if (v->y_max_label) {
        lv_label_set_text_fmt(v->y_max_label, "%uW", ceiling);
    }

    /* Fill the chart: empty slots become LV_CHART_POINT_NONE so the line starts
     * from the right edge and extends left as samples accumulate. */
    lv_coord_t *points = lv_chart_get_y_array(v->chart, v->series);
    uint16_t empty = slots - count;
    for (uint16_t i = 0; i < empty; i++) {
        points[i] = LV_CHART_POINT_NONE;
    }
    for (uint16_t i = 0; i < count; i++) {
        points[empty + i] = (lv_coord_t)buf[i];
    }
    lv_chart_refresh(v->chart);
}

static void refresh_labels(ui_solar_detail_view_t *v)
{
    if (v == NULL) return;
    if (v->current_power_label) {
        lv_label_set_text_fmt(v->current_power_label, "Now: %uW", v->latest_power_w);
    }
    if (v->max_power_label) {
        lv_label_set_text_fmt(v->max_power_label, "Max: %uW",
                              v->latest_stats.today_max_power_w);
    }
    if (v->yield_label) {
        unsigned long wh = (unsigned long)v->latest_stats.today_yield_centikwh * 10UL;
        lv_label_set_text_fmt(v->yield_label, "Yield: %luWh", wh);
    }
    if (v->title_label) {
        lv_label_set_text(v->title_label,
                          v->range == RANGE_HOUR ? "Solar - Last Hour"
                                                 : "Solar - Today");
    }
}

static void btn_hour_cb(lv_event_t *e)
{
    ui_solar_detail_view_t *v = lv_event_get_user_data(e);
    if (v == NULL) return;
    v->range = RANGE_HOUR;
    lv_obj_add_state(v->btn_hour, LV_STATE_CHECKED);
    lv_obj_clear_state(v->btn_day, LV_STATE_CHECKED);
    refresh_labels(v);
    apply_series(v);
}

static void btn_day_cb(lv_event_t *e)
{
    ui_solar_detail_view_t *v = lv_event_get_user_data(e);
    if (v == NULL) return;
    v->range = RANGE_DAY;
    lv_obj_add_state(v->btn_day, LV_STATE_CHECKED);
    lv_obj_clear_state(v->btn_hour, LV_STATE_CHECKED);
    refresh_labels(v);
    apply_series(v);
}

static void btn_back_cb(lv_event_t *e)
{
    ui_solar_detail_view_t *v = lv_event_get_user_data(e);
    if (v == NULL) return;
    if (v->back_cb) {
        v->back_cb(v->back_user_data);
    }
}

ui_solar_detail_view_t *ui_solar_detail_view_create(ui_state_t *ui,
                                                    lv_obj_t *parent,
                                                    ui_solar_detail_back_cb_t back_cb,
                                                    void *back_user_data)
{
    if (ui == NULL || parent == NULL) return NULL;

    ui_solar_detail_view_t *v = calloc(1, sizeof(*v));
    if (v == NULL) return NULL;

    v->ui = ui;
    v->back_cb = back_cb;
    v->back_user_data = back_user_data;
    v->range = RANGE_HOUR;

    v->root = lv_obj_create(parent);
    lv_obj_set_size(v->root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(v->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->root, 0, 0);
    lv_obj_set_style_outline_width(v->root, 0, 0);
    lv_obj_set_style_pad_all(v->root, 8, 0);
    lv_obj_clear_flag(v->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->root, LV_OBJ_FLAG_HIDDEN);

    /* Top row: back button + title + range toggle */
    lv_obj_t *top = lv_obj_create(v->root);
    lv_obj_set_size(top, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    v->btn_back = lv_btn_create(top);
    lv_obj_set_size(v->btn_back, 50, 36);
    lv_obj_align(v->btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(v->btn_back, btn_back_cb, LV_EVENT_CLICKED, v);
    lv_obj_t *back_lbl = lv_label_create(v->btn_back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_center(back_lbl);

    /* Title sits between back button (left) and toggle buttons (right).
     * Back btn ends at x=50, hour btn starts at ~190 (right side, 54+54+6 offset),
     * so center the title around x=120 from left edge. */
    v->title_label = lv_label_create(top);
    lv_obj_add_style(v->title_label, &ui->styles.medium, 0);
    lv_label_set_text(v->title_label, "Solar");
    lv_obj_align(v->title_label, LV_ALIGN_LEFT_MID, 60, 0);

    v->btn_day = lv_btn_create(top);
    lv_obj_set_size(v->btn_day, 54, 32);
    lv_obj_align(v->btn_day, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(v->btn_day, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(v->btn_day, btn_day_cb, LV_EVENT_CLICKED, v);
    lv_obj_t *ld = lv_label_create(v->btn_day);
    lv_label_set_text(ld, "Day");
    lv_obj_center(ld);

    v->btn_hour = lv_btn_create(top);
    lv_obj_set_size(v->btn_hour, 54, 32);
    lv_obj_align_to(v->btn_hour, v->btn_day, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_add_flag(v->btn_hour, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_state(v->btn_hour, LV_STATE_CHECKED);
    lv_obj_add_event_cb(v->btn_hour, btn_hour_cb, LV_EVENT_CLICKED, v);
    lv_obj_t *lh = lv_label_create(v->btn_hour);
    lv_label_set_text(lh, "1H");
    lv_obj_center(lh);

    /* Chart - occupies the bulk of the middle area */
    v->chart = lv_chart_create(v->root);
    lv_obj_set_size(v->chart, lv_pct(100), lv_pct(65));
    lv_obj_align(v->chart, LV_ALIGN_TOP_MID, 0, 46);
    lv_chart_set_type(v->chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(v->chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(v->chart, 4, 0);
    lv_chart_set_point_count(v->chart, SOLAR_STATS_HOUR_SLOTS);
    lv_obj_set_style_size(v->chart, 0, LV_PART_INDICATOR);

    v->series = lv_chart_add_series(v->chart, lv_color_hex(0xFFC107),
                                    LV_CHART_AXIS_PRIMARY_Y);

    /* Y-axis max label overlaid at top-left of the chart */
    v->y_max_label = lv_label_create(v->chart);
    lv_obj_add_style(v->y_max_label, &ui->styles.small, 0);
    lv_label_set_text(v->y_max_label, "--W");
    lv_obj_align(v->y_max_label, LV_ALIGN_TOP_LEFT, 4, 2);

    /* Stats row - bottom, compact single row */
    lv_obj_t *stats_row = lv_obj_create(v->root);
    lv_obj_set_size(stats_row, lv_pct(100), 40);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_set_style_pad_all(stats_row, 0, 0);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(stats_row, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    v->current_power_label = lv_label_create(stats_row);
    lv_obj_add_style(v->current_power_label, &ui->styles.small, 0);
    lv_label_set_text(v->current_power_label, "--W");

    v->max_power_label = lv_label_create(stats_row);
    lv_obj_add_style(v->max_power_label, &ui->styles.small, 0);
    lv_label_set_text(v->max_power_label, "Max: --W");

    v->yield_label = lv_label_create(stats_row);
    lv_obj_add_style(v->yield_label, &ui->styles.small, 0);
    lv_label_set_text(v->yield_label, "Yield: --Wh");

    return v;
}

void ui_solar_detail_view_show(ui_solar_detail_view_t *view,
                               const solar_stats_t *stats,
                               uint16_t current_pv_power_w)
{
    if (view == NULL) return;
    ui_solar_detail_view_refresh(view, stats, current_pv_power_w);
    if (view->root) {
        lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_solar_detail_view_hide(ui_solar_detail_view_t *view)
{
    if (view == NULL || view->root == NULL) return;
    lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
}

void ui_solar_detail_view_refresh(ui_solar_detail_view_t *view,
                                  const solar_stats_t *stats,
                                  uint16_t current_pv_power_w)
{
    if (view == NULL) return;
    if (stats) {
        view->latest_stats = *stats;
    }
    view->latest_power_w = current_pv_power_w;
    view->has_latest = true;
    refresh_labels(view);
    apply_series(view);
}

void ui_solar_detail_view_destroy(ui_solar_detail_view_t *view)
{
    if (view == NULL) return;
    if (view->root) {
        lv_obj_del(view->root);
        view->root = NULL;
    }
    free(view);
}

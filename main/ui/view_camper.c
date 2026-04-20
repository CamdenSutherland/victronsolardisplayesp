#include "view_camper.h"
#include <stdlib.h>
#include <string.h>
#include "ui_format.h"
#include "solar_stats.h"
#include "view_solar_detail.h"

LV_FONT_DECLARE(font_awesome_solar_panel_40);
LV_FONT_DECLARE(font_awesome_bolt_40);

typedef struct {
    ui_device_view_t base;
    lv_obj_t *metrics_row;
    lv_obj_t *left_column;
    lv_obj_t *center_column;
    lv_obj_t *right_column;

    /* Center: SmartShunt */
    lv_obj_t *arc_container;
    lv_obj_t *soc_arc;
    lv_obj_t *soc_label;
    lv_obj_t *battery_voltage_label;
    lv_obj_t *battery_current_label;
    lv_obj_t *ttg_label;
    lv_obj_t *power_label;

    /* Left: Solar MPPT */
    lv_obj_t *solar_icon;
    lv_obj_t *solar_pv_label;
    lv_obj_t *solar_charge_label;
    lv_obj_t *solar_state_label;
    lv_obj_t *solar_yield_label;

    /* Right: AC Charger */
    lv_obj_t *ac_icon;
    lv_obj_t *ac_state_label;
    lv_obj_t *ac_charge_label;

    /* Persistent data from each device */
    struct {
        bool has_data;
        uint16_t soc_deci_percent;
        uint16_t battery_voltage_cv;
        uint32_t ttg_minutes;
        int32_t battery_current_milli;
        uint32_t last_update;
    } shunt;

    struct {
        bool has_data;
        uint16_t pv_power_w;
        uint16_t battery_current_deci;
        uint8_t device_state;
        uint32_t yield_today_centikwh;
        uint32_t last_update;
    } solar;

    struct {
        bool has_data;
        uint8_t device_state;
        uint16_t battery_current_1_deci;
        uint32_t last_update;
    } ac;

    /* Solar history/stats + detail sub-view */
    solar_stats_t solar_stats;
    ui_solar_detail_view_t *solar_detail;
    lv_obj_t *metrics_container;  /* wrapper around metrics_row so we can hide the main layout */
} ui_camper_view_t;

static void camper_view_update(ui_device_view_t *view, const victron_data_t *data);
static void camper_view_show(ui_device_view_t *view);
static void camper_view_hide(ui_device_view_t *view);
static void camper_view_destroy(ui_device_view_t *view);
static void refresh_display(ui_camper_view_t *v);
static void solar_column_click_cb(lv_event_t *e);
static void solar_detail_back_cb(void *user_data);

static const char *solar_state_str(uint8_t s)
{
    switch (s) {
    case 0:  return "Off";
    case 1:  return "Low Power";
    case 2:  return "Fault";
    case 3:  return "Bulk";
    case 4:  return "Absorption";
    case 5:  return "Float";
    case 6:  return "Storage";
    case 7:  return "Equalize";
    case 11: return "Starting";
    default: return "Unknown";
    }
}

static const char *ac_state_str(uint8_t s)
{
    switch (s) {
    case 0:  return "Off";
    case 1:  return "Low Power";
    case 2:  return "Fault";
    case 3:  return "Bulk";
    case 4:  return "Absorption";
    case 5:  return "Float";
    case 6:  return "Storage";
    case 7:  return "Equalize";
    case 11: return "Starting";
    default: return "Unknown";
    }
}

/* Helper: create a transparent flex-column container */
static lv_obj_t *make_column(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, width, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_outline_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 4, 0);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return col;
}

/* Helper: create a small centered label */
static lv_obj_t *make_label(lv_obj_t *parent, ui_state_t *ui, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, &ui->styles.small, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    return lbl;
}

ui_device_view_t *ui_camper_view_create(ui_state_t *ui, lv_obj_t *parent)
{
    if (ui == NULL || parent == NULL) {
        return NULL;
    }

    ui_camper_view_t *v = calloc(1, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }

    v->base.ui = ui;
    v->base.root = lv_obj_create(parent);
    lv_obj_set_size(v->base.root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(v->base.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->base.root, 0, 0);
    lv_obj_set_style_outline_width(v->base.root, 0, 0);
    lv_obj_set_style_pad_all(v->base.root, 0, 0);
    lv_obj_clear_flag(v->base.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->base.root, LV_OBJ_FLAG_HIDDEN);

    /* Main 3-column row */
    v->metrics_row = lv_obj_create(v->base.root);
    lv_obj_set_size(v->metrics_row, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(v->metrics_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->metrics_row, 0, 0);
    lv_obj_set_style_outline_width(v->metrics_row, 0, 0);
    lv_obj_set_style_pad_all(v->metrics_row, 5, 0);
    lv_obj_clear_flag(v->metrics_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(v->metrics_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(v->metrics_row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* ========== LEFT COLUMN: Solar MPPT ========== */
    v->left_column = make_column(v->metrics_row, lv_pct(28));

    v->solar_icon = lv_label_create(v->left_column);
    lv_obj_set_style_text_font(v->solar_icon, &font_awesome_solar_panel_40, 0);
    lv_label_set_text(v->solar_icon, "\xEF\x96\xBA");
    lv_obj_set_style_text_color(v->solar_icon, lv_color_hex(0xFFC107), 0);

    v->solar_charge_label = make_label(v->left_column, ui, "--A");
    lv_obj_add_style(v->solar_charge_label, &ui->styles.value, 0);
    lv_obj_set_style_text_align(v->solar_charge_label, LV_TEXT_ALIGN_CENTER, 0);

    v->solar_pv_label = make_label(v->left_column, ui, "--W");
    v->solar_yield_label = make_label(v->left_column, ui, "--Wh");
    v->solar_state_label = make_label(v->left_column, ui, "--");

    /* ========== CENTER COLUMN: SmartShunt Battery ========== */
    v->center_column = make_column(v->metrics_row, lv_pct(38));

    /* SOC arc */
    v->arc_container = lv_obj_create(v->center_column);
    lv_obj_set_size(v->arc_container, 160, 160);
    lv_obj_set_style_bg_opa(v->arc_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->arc_container, 0, 0);
    lv_obj_set_style_outline_width(v->arc_container, 0, 0);
    lv_obj_set_style_pad_all(v->arc_container, 0, 0);
    lv_obj_clear_flag(v->arc_container, LV_OBJ_FLAG_SCROLLABLE);

    v->soc_arc = lv_arc_create(v->arc_container);
    lv_obj_set_size(v->soc_arc, 160, 160);
    lv_obj_center(v->soc_arc);
    lv_arc_set_rotation(v->soc_arc, 292);
    lv_arc_set_bg_angles(v->soc_arc, 0, 315);
    lv_arc_set_angles(v->soc_arc, 0, 0);
    lv_obj_remove_style(v->soc_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(v->soc_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(v->soc_arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(v->soc_arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0x00C851), LV_PART_INDICATOR);

    v->soc_label = lv_label_create(v->arc_container);
    lv_label_set_text(v->soc_label, "---%");
    lv_obj_add_style(v->soc_label, &ui->styles.medium, 0);
    lv_obj_set_style_text_align(v->soc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(v->soc_label);

    /* Voltage and current side-by-side below the arc */
    lv_obj_t *va_row = lv_obj_create(v->center_column);
    lv_obj_set_size(va_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(va_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(va_row, 0, 0);
    lv_obj_set_style_outline_width(va_row, 0, 0);
    lv_obj_set_style_pad_all(va_row, 0, 0);
    lv_obj_set_style_pad_top(va_row, 8, 0);
    lv_obj_set_style_pad_column(va_row, 8, 0);
    lv_obj_clear_flag(va_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(va_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(va_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    v->battery_voltage_label = lv_label_create(va_row);
    lv_label_set_text(v->battery_voltage_label, "--V");
    lv_obj_add_style(v->battery_voltage_label, &ui->styles.small, 0);

    v->battery_current_label = lv_label_create(va_row);
    lv_label_set_text(v->battery_current_label, "--A");
    lv_obj_add_style(v->battery_current_label, &ui->styles.small, 0);

    /* Power then TTG at bottom */
    v->power_label = make_label(v->center_column, ui, "--W");
    lv_obj_set_style_pad_top(v->power_label, 2, 0);

    v->ttg_label = make_label(v->center_column, ui, "--");
    lv_obj_set_style_pad_top(v->ttg_label, 2, 0);

    /* ========== RIGHT COLUMN: AC Charger (Blue Smart IP22) ========== */
    v->right_column = make_column(v->metrics_row, lv_pct(28));

    v->ac_icon = lv_label_create(v->right_column);
    lv_obj_set_style_text_font(v->ac_icon, &font_awesome_bolt_40, 0);
    lv_label_set_text(v->ac_icon, "\xEF\x83\xA7");
    lv_obj_set_style_text_color(v->ac_icon, lv_color_hex(0x2196F3), 0);

    v->ac_charge_label = make_label(v->right_column, ui, "--A");
    lv_obj_add_style(v->ac_charge_label, &ui->styles.value, 0);
    lv_obj_set_style_text_align(v->ac_charge_label, LV_TEXT_ALIGN_CENTER, 0);

    v->ac_state_label = make_label(v->right_column, ui, "--");

    /* Init persistent state */
    v->shunt.ttg_minutes = 0xFFFFFFFF;
    solar_stats_init(&v->solar_stats);

    /* Make the solar (left) column tappable to open the solar detail view */
    lv_obj_add_flag(v->left_column, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v->left_column, solar_column_click_cb, LV_EVENT_CLICKED, v);

    /* Create the solar detail sub-view (hidden by default) */
    v->solar_detail = ui_solar_detail_view_create(ui, v->base.root,
                                                  solar_detail_back_cb, v);

    v->base.update = camper_view_update;
    v->base.show = camper_view_show;
    v->base.hide = camper_view_hide;
    v->base.destroy = camper_view_destroy;

    return &v->base;
}

static ui_camper_view_t *camper_from_base(ui_device_view_t *base)
{
    return (ui_camper_view_t *)base;
}

static void camper_view_update(ui_device_view_t *view, const victron_data_t *data)
{
    ui_camper_view_t *v = camper_from_base(view);
    if (v == NULL || data == NULL) {
        return;
    }

    uint32_t now = lv_tick_get();

    switch (data->type) {
    case VICTRON_BLE_RECORD_BATTERY_MONITOR: {
        const victron_record_battery_monitor_t *b = &data->record.battery;
        v->shunt.has_data = true;
        v->shunt.soc_deci_percent = b->soc_deci_percent;
        v->shunt.battery_voltage_cv = b->battery_voltage_centi;
        v->shunt.ttg_minutes = b->time_to_go_minutes;
        v->shunt.battery_current_milli = b->battery_current_milli;
        v->shunt.last_update = now;
        break;
    }
    case VICTRON_BLE_RECORD_SOLAR_CHARGER: {
        const victron_record_solar_charger_t *s = &data->record.solar;
        v->solar.has_data = true;
        v->solar.pv_power_w = s->pv_power_w;
        v->solar.battery_current_deci = s->battery_current_deci;
        v->solar.device_state = s->device_state;
        v->solar.yield_today_centikwh = s->yield_today_centikwh;
        v->solar.last_update = now;

        solar_stats_on_sample(&v->solar_stats,
                              s->pv_power_w,
                              s->yield_today_centikwh,
                              now);

        if (v->solar_detail) {
            ui_solar_detail_view_refresh(v->solar_detail,
                                         &v->solar_stats,
                                         s->pv_power_w);
        }
        break;
    }
    case VICTRON_BLE_RECORD_AC_CHARGER: {
        const victron_record_ac_charger_t *a = &data->record.ac_charger;
        v->ac.has_data = true;
        v->ac.device_state = a->device_state;
        v->ac.battery_current_1_deci = a->battery_current_1_deci;
        v->ac.last_update = now;
        break;
    }
    default:
        return;
    }

    refresh_display(v);
}

static void refresh_display(ui_camper_view_t *v)
{
    uint32_t now = lv_tick_get();
    const uint32_t TIMEOUT_MS = 30000;

    /* ===== CENTER: SmartShunt ===== */
    if (v->shunt.has_data && (now - v->shunt.last_update) < TIMEOUT_MS) {
        uint16_t pct = v->shunt.soc_deci_percent / 10;
        uint16_t dec = v->shunt.soc_deci_percent % 10;

        int16_t arc_angle = (int16_t)((v->shunt.soc_deci_percent * 315) / 1000);
        lv_arc_set_angles(v->soc_arc, 0, arc_angle);

        if (pct >= 50) {
            lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0x00C851), LV_PART_INDICATOR);
        } else if (pct >= 25) {
            lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0xFF9800), LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0xF44336), LV_PART_INDICATOR);
        }

        lv_label_set_text_fmt(v->soc_label, "%u.%u%%", pct, dec);

        if (v->shunt.battery_voltage_cv > 0) {
            lv_label_set_text_fmt(v->battery_voltage_label, "%u.%02uV",
                                  v->shunt.battery_voltage_cv / 100,
                                  v->shunt.battery_voltage_cv % 100);
        } else {
            lv_label_set_text(v->battery_voltage_label, "--V");
        }

        /* Battery current */
        {
            int32_t current_centi = (int32_t)v->shunt.battery_current_milli / 10;
            int32_t abs_centi = current_centi < 0 ? -current_centi : current_centi;
            int32_t i_int = abs_centi / 100;
            int32_t i_dec = abs_centi % 100;
            if (current_centi < 0) {
                lv_label_set_text_fmt(v->battery_current_label, "-%ld.%02ldA",
                                      (long)i_int, (long)i_dec);
            } else {
                lv_label_set_text_fmt(v->battery_current_label, "%ld.%02ldA",
                                      (long)i_int, (long)i_dec);
            }
        }

        /* Power (V * I) */
        if (v->shunt.battery_voltage_cv > 0 && v->shunt.battery_current_milli != 0) {
            int32_t watts = (int32_t)v->shunt.battery_voltage_cv *
                            v->shunt.battery_current_milli / 100000;
            lv_label_set_text_fmt(v->power_label, "%ldW", (long)watts);
        } else {
            lv_label_set_text(v->power_label, "0W");
        }

        /* TTG */
        uint32_t ttg = v->shunt.ttg_minutes;
        if (ttg != 0xFFFFFFFF && ttg > 0) {
            uint32_t h = ttg / 60;
            uint32_t m = ttg % 60;
            if (h >= 24) {
                lv_label_set_text_fmt(v->ttg_label, "%ud %uh",
                                      (unsigned)(h / 24), (unsigned)(h % 24));
            } else if (h > 0) {
                lv_label_set_text_fmt(v->ttg_label, "%uh %um",
                                      (unsigned)h, (unsigned)m);
            } else {
                lv_label_set_text_fmt(v->ttg_label, "%um", (unsigned)m);
            }
        } else {
            lv_label_set_text(v->ttg_label, "--");
        }
    } else {
        lv_arc_set_angles(v->soc_arc, 0, 0);
        lv_obj_set_style_arc_color(v->soc_arc, lv_color_hex(0x666666), LV_PART_INDICATOR);
        lv_label_set_text(v->soc_label, "---%");
        lv_label_set_text(v->battery_voltage_label, "--V");
        lv_label_set_text(v->battery_current_label, "--A");
        lv_label_set_text(v->power_label, "--W");
        lv_label_set_text(v->ttg_label, "--");
    }

    /* ===== LEFT: Solar MPPT ===== */
    if (v->solar.has_data && (now - v->solar.last_update) < TIMEOUT_MS) {
        uint16_t a_int = v->solar.battery_current_deci / 10;
        uint16_t a_dec = v->solar.battery_current_deci % 10;
        lv_label_set_text_fmt(v->solar_charge_label, "%u.%uA", a_int, a_dec);

        lv_label_set_text_fmt(v->solar_pv_label, "%uW", v->solar.pv_power_w);

        unsigned long yield_wh = (unsigned long)v->solar.yield_today_centikwh * 10UL;
        lv_label_set_text_fmt(v->solar_yield_label, "%luWh", yield_wh);

        lv_label_set_text(v->solar_state_label, solar_state_str(v->solar.device_state));

        /* Tint icon based on activity */
        if (v->solar.pv_power_w > 0) {
            lv_obj_set_style_text_color(v->solar_icon, lv_color_hex(0xFFC107), 0);
        } else {
            lv_obj_set_style_text_color(v->solar_icon, lv_color_hex(0x666666), 0);
        }
    } else {
        lv_label_set_text(v->solar_charge_label, "--A");
        lv_label_set_text(v->solar_pv_label, "--W");
        lv_label_set_text(v->solar_yield_label, "--Wh");
        lv_label_set_text(v->solar_state_label, "--");
        lv_obj_set_style_text_color(v->solar_icon, lv_color_hex(0x666666), 0);
    }

    /* ===== RIGHT: AC Charger ===== */
    if (v->ac.has_data && (now - v->ac.last_update) < TIMEOUT_MS) {
        uint16_t ca_int = v->ac.battery_current_1_deci / 10;
        uint16_t ca_dec = v->ac.battery_current_1_deci % 10;
        lv_label_set_text_fmt(v->ac_charge_label, "%u.%uA", ca_int, ca_dec);

        lv_label_set_text(v->ac_state_label, ac_state_str(v->ac.device_state));

        /* Tint icon based on activity */
        if (v->ac.device_state != 0) {
            lv_obj_set_style_text_color(v->ac_icon, lv_color_hex(0x2196F3), 0);
        } else {
            lv_obj_set_style_text_color(v->ac_icon, lv_color_hex(0x666666), 0);
        }
    } else {
        lv_label_set_text(v->ac_charge_label, "--A");
        lv_label_set_text(v->ac_state_label, "--");
        lv_obj_set_style_text_color(v->ac_icon, lv_color_hex(0x666666), 0);
    }
}

static void camper_view_show(ui_device_view_t *view)
{
    if (view && view->root) {
        lv_obj_clear_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void camper_view_hide(ui_device_view_t *view)
{
    if (view && view->root) {
        lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void camper_view_destroy(ui_device_view_t *view)
{
    ui_camper_view_t *v = camper_from_base(view);
    if (v == NULL) {
        return;
    }
    if (v->solar_detail) {
        ui_solar_detail_view_destroy(v->solar_detail);
        v->solar_detail = NULL;
    }
    if (view->root) {
        lv_obj_del(view->root);
        view->root = NULL;
    }
    free(view);
}

static void solar_column_click_cb(lv_event_t *e)
{
    ui_camper_view_t *v = lv_event_get_user_data(e);
    if (v == NULL || v->solar_detail == NULL) return;

    /* Hide the main 3-column layout, show the solar detail sub-view */
    lv_obj_add_flag(v->metrics_row, LV_OBJ_FLAG_HIDDEN);
    ui_solar_detail_view_show(v->solar_detail, &v->solar_stats,
                              v->solar.pv_power_w);
}

static void solar_detail_back_cb(void *user_data)
{
    ui_camper_view_t *v = user_data;
    if (v == NULL) return;
    if (v->solar_detail) {
        ui_solar_detail_view_hide(v->solar_detail);
    }
    if (v->metrics_row) {
        lv_obj_clear_flag(v->metrics_row, LV_OBJ_FLAG_HIDDEN);
    }
}

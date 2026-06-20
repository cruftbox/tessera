#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "ui.h"
#include "display.h"
#include "ha_client.h"
#include "config.h"

#define HEADER_H   56
#define TILE_W     142
#define TILE_H     121
#define TILE_GAP   11
#define COLS       3
#define MAX_PAGES  8

static lv_obj_t *tileview;
static lv_obj_t *tile_pages[MAX_PAGES];
static lv_obj_t *tile_btns[MOSAIC_COUNT];
static lv_obj_t *tile_labels[MOSAIC_COUNT];   // name
static lv_obj_t *tile_values[MOSAIC_COUNT];   // "On"/"Off"
static lv_obj_t *tile_pips[MOSAIC_COUNT];     // status pip top-right
static lv_obj_t *tile_icons[MOSAIC_COUNT];    // MDI glyph top-left

extern const lv_font_t mdi_icons;             // generated in src/mdi_icons.c

// ---- Thermostat (9th-cell tile + full-screen detail view) ----
#define THERMO_MIN 48
#define THERMO_MAX 90
static lv_obj_t *thermo_tile, *thermo_tile_val, *thermo_tile_pip, *thermo_tile_icon;
static ThermoState thermo = { "off", 0, 70, 74, 70, true, false };
static lv_obj_t *thermo_view;                 // full-screen overlay (hidden by default)
static lv_obj_t *tv_current;                  // big current-temp label
static lv_obj_t *tv_action;                   // hvac action status (Heating/Cooling/Idle)
static lv_obj_t *tv_low_row, *tv_high_row, *tv_single_row;
static lv_obj_t *tv_low_val, *tv_high_val, *tv_single_val;
static lv_obj_t *tv_fan_btns[5], *tv_fan_lbls[5];
static int       fan_active = 0;  // index of the fan button currently in effect (0 = Off)
static uint32_t  fan_off_at = 0;  // millis() when the timed fan run should stop (0 = none)
static bool      setpoint_pending = false;  // local setpoint edits waiting to be sent
static uint32_t  setpoint_tap_ms  = 0;      // time of last setpoint tap (for debounce)
static uint32_t  setpoint_send_ms = 0;      // time we last sent (ignore echoes briefly after)
static bool tile_is_on[MOSAIC_COUNT] = { false };
static lv_obj_t *status_dot;
static lv_obj_t *wifi_icon;        // WiFi link status (LV_SYMBOL_WIFI) — distinct from the HA dot
static lv_obj_t *time_label;
static lv_obj_t *indoor_temp_label;
static lv_obj_t *outdoor_temp_label;
static lv_obj_t *page_dots[MAX_PAGES];
static int page_count = 0;
static int current_page = 0;
static volatile bool suppress_click = false;  // set when a touch turns out to be a swipe

static uint32_t last_activity_ms = 0;
static bool is_dimmed = false;

// Background gradient stops (kept static — LVGL stores the pointer, not a copy).
static lv_grad_dsc_t bg_grad;

// Apply the uniform two-state tile styling (matches the design spec).
static void apply_tile_style(int idx, bool on) {
  lv_obj_t* b = tile_btns[idx];
  if (on) {
    // Solid white card, dark text, filled green pip, subtle shadow.
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(b, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(b, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(b, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_color(tile_labels[idx], lv_color_hex(0x1F2330), LV_PART_MAIN);
    lv_obj_set_style_text_color(tile_values[idx], lv_color_hex(0x8A8F99), LV_PART_MAIN);
    lv_obj_set_style_text_opa(tile_values[idx], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(tile_icons[idx], lv_color_hex(0x1F2330), LV_PART_MAIN);
    lv_obj_set_style_text_opa(tile_icons[idx], LV_OPA_COVER, LV_PART_MAIN);
  } else {
    // Frosted translucent card over the gradient, white text, hollow pip.
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, 38, LV_PART_MAIN);             // ~0.15
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(b, 72, LV_PART_MAIN);         // ~0.28
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(tile_labels[idx], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(tile_values[idx], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(tile_values[idx], 184, LV_PART_MAIN);  // ~0.72
    lv_obj_set_style_text_color(tile_icons[idx], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(tile_icons[idx], 230, LV_PART_MAIN);   // ~0.90
  }
}

// Status pip = entity availability: green reachable, amber unavailable/unknown.
static void set_pip_available(int idx, bool available) {
  lv_obj_set_style_border_width(tile_pips[idx], 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tile_pips[idx], LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(tile_pips[idx],
    available ? lv_color_hex(0x22C55E) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
}

static void tile_btn_cb(lv_event_t* e) {
  if (suppress_click) { suppress_click = false; return; }  // this touch was a swipe
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  const Tessera& t = MOSAIC[idx];
  // Fan with a configured start speed, currently off -> turn on at that %.
  // Otherwise (already on, or no on_pct) fall back to plain toggle.
  bool turn_on_at_pct = (t.on_pct > 0 && !tile_is_on[idx] && strncmp(t.entity_id, "fan.", 4) == 0);
  if (turn_on_at_pct) {
    ha_fan_turn_on_pct(t.entity_id, t.on_pct);
  } else {
    ha_toggle(t.entity_id);
  }
  // Optimistic update: restyle immediately to the predicted new state so the
  // tile feels instant. The real state_changed event will confirm or correct it.
  // Only do this if we're actually connected — otherwise the command wasn't sent
  // and the tile would lie about a change that didn't happen (header dot stays red).
  if (ha_is_connected()) {
    bool predicted_on = turn_on_at_pct ? true : !tile_is_on[idx];
    ui_update_tile_state(idx, predicted_on ? "on" : "off");
  }
}

// Lays out + styles the pill page dots, centered (active dot is a wider pill).
static void update_page_dots() {
  if (page_count <= 1) return;
  const int gap = 7, h = 5, y = 480 - 16;
  int total = gap * (page_count - 1);
  for (int i = 0; i < page_count; i++) total += (i == current_page) ? 18 : 5;
  int x = (480 - total) / 2;
  for (int i = 0; i < page_count; i++) {
    bool act = (i == current_page);
    int w = act ? 18 : 5;
    lv_obj_set_size(page_dots[i], w, h);
    lv_obj_set_pos(page_dots[i], x, y);
    lv_obj_set_style_radius(page_dots[i], 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page_dots[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_dots[i], act ? LV_OPA_COVER : 102, LV_PART_MAIN);  // ~0.40
    x += w + gap;
  }
}

void ui_touch_pressed() {
  suppress_click = false;  // reset at the start of every new touch
}

// Swipe flips the page instantly (LV_ANIM_OFF) — no animated slide, which this
// RGB panel renders too slowly. delta +1 = next page, -1 = previous page.
void ui_swipe_page(int delta) {
  int target = current_page + delta;
  if (target < 0 || target >= page_count) return;
  suppress_click = true;  // the touch that triggered this was a swipe, not a tap
  current_page = target;
  lv_obj_set_tile_id(tileview, current_page, 0, LV_ANIM_OFF);
  update_page_dots();
}

static int clampt(int v) { return v < THERMO_MIN ? THERMO_MIN : (v > THERMO_MAX ? THERMO_MAX : v); }

// Frosted translucent-white button to match the front-page tile language.
static void style_frosted_btn(lv_obj_t* b) {
  lv_obj_set_style_radius(b, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, 38, LV_PART_MAIN);
  lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_opa(b, 72, LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
}

// Highlight the fan button currently in effect: active = solid white + dark text,
// others = frosted + white text (same on/off language as the tiles).
static void update_fan_buttons() {
  for (int i = 0; i < 5; i++) {
    if (!tv_fan_btns[i]) continue;
    bool act = (i == fan_active);
    lv_obj_set_style_bg_opa(tv_fan_btns[i], act ? LV_OPA_COVER : 38, LV_PART_MAIN);
    lv_obj_set_style_text_color(tv_fan_lbls[i],
      act ? lv_color_hex(0x1F2330) : lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  }
}

// Tile value line = setpoint summary (distinct from the header's current-temp glance).
static void thermo_tile_refresh() {
  if (!thermo_tile_val) return;
  char b[16];
  if (!thermo.available)                       strcpy(b, "n/a");
  else if (strcmp(thermo.mode, "off") == 0)    strcpy(b, "Off");
  else if (thermo.dual)                        snprintf(b, sizeof(b), "%d-%d°", thermo.low, thermo.high);
  else                                         snprintf(b, sizeof(b), "%d°", thermo.target);
  lv_label_set_text(thermo_tile_val, b);
  lv_obj_set_style_bg_opa(thermo_tile_pip, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(thermo_tile_pip,
    thermo.available ? lv_color_hex(0x22C55E) : lv_color_hex(0xF59E0B), LV_PART_MAIN);
}

// Refresh the open detail view from `thermo`.
static void render_thermo_view() {
  if (!thermo_view) return;
  char b[16];
  snprintf(b, sizeof(b), "%d°", thermo.current);
  lv_label_set_text(tv_current, b);

  // hvac action status under the current temp
  if (strcmp(thermo.action, "heating") == 0) {
    lv_label_set_text(tv_action, "Heating");
    lv_obj_set_style_text_color(tv_action, lv_color_hex(0xFB923C), LV_PART_MAIN);   // warm orange
  } else if (strcmp(thermo.action, "cooling") == 0) {
    lv_label_set_text(tv_action, "Cooling");
    lv_obj_set_style_text_color(tv_action, lv_color_hex(0x22D3EE), LV_PART_MAIN);   // cool cyan
  } else if (strcmp(thermo.mode, "off") == 0) {
    lv_label_set_text(tv_action, "Off");
    lv_obj_set_style_text_color(tv_action, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
  } else {
    lv_label_set_text(tv_action, "Idle");
    lv_obj_set_style_text_color(tv_action, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
  }

  bool off = strcmp(thermo.mode, "off") == 0;
  lv_obj_add_flag(tv_low_row,    LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tv_high_row,   LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tv_single_row, LV_OBJ_FLAG_HIDDEN);
  if (thermo.dual) {
    lv_obj_clear_flag(tv_low_row,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(tv_high_row, LV_OBJ_FLAG_HIDDEN);
    snprintf(b, sizeof(b), "%d°", thermo.low);  lv_label_set_text(tv_low_val, b);
    snprintf(b, sizeof(b), "%d°", thermo.high); lv_label_set_text(tv_high_val, b);
  } else if (!off) {
    lv_obj_clear_flag(tv_single_row, LV_OBJ_FLAG_HIDDEN);
    snprintf(b, sizeof(b), "%d°", thermo.target); lv_label_set_text(tv_single_val, b);
  }
}

// user_data code: (which<<1)|dir  -> which 0=low 1=high 2=single, dir 1=up 0=down
static void setpoint_cb(lv_event_t* e) {
  int code = (int)(intptr_t)lv_event_get_user_data(e);
  int delta = (code & 1) ? +1 : -1;
  int which = code >> 1;
  if      (which == 0) thermo.low    = clampt(thermo.low + delta);
  else if (which == 1) thermo.high   = clampt(thermo.high + delta);
  else                 thermo.target = clampt(thermo.target + delta);
  if (thermo.low > thermo.high) {            // keep the band ordered
    if (which == 0) thermo.high = thermo.low;
    else            thermo.low  = thermo.high;
  }
  // Update only the affected value label(s) — a full render_thermo_view() per tap
  // is too heavy (re-blends the whole translucent/gradient view) and feels laggy.
  char vb[8];
  if (thermo.dual) {
    snprintf(vb, sizeof(vb), "%d°", thermo.low);  lv_label_set_text(tv_low_val, vb);
    snprintf(vb, sizeof(vb), "%d°", thermo.high); lv_label_set_text(tv_high_val, vb);
  } else {
    snprintf(vb, sizeof(vb), "%d°", thermo.target); lv_label_set_text(tv_single_val, vb);
  }
  // Defer the actual service call until the user stops tapping (debounce), and
  // mark setpoints as locally-owned so HA echoes don't fight the displayed number.
  setpoint_pending = true;
  setpoint_tap_ms  = millis();
}

// Fan buttons by index: 0=Off, 1=1h, 2=2h, 3=4h, 4=On(ongoing).
static void fan_cb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == 0)      { ha_thermo_fan(false); fan_off_at = 0; }
  else if (i == 4) { ha_thermo_fan(true);  fan_off_at = 0; }   // ongoing, no auto-off
  else {
    int hrs = (i == 1) ? 1 : (i == 2) ? 2 : 4;
    ha_thermo_fan(true);
    fan_off_at = millis() + (uint32_t)hrs * 3600000UL;
    if (fan_off_at == 0) fan_off_at = 1;
  }
  fan_active = i;
  update_fan_buttons();
}

static void thermo_close_cb(lv_event_t*) { lv_obj_add_flag(thermo_view, LV_OBJ_FLAG_HIDDEN); }

static void thermo_open_cb(lv_event_t*) {
  if (suppress_click) { suppress_click = false; return; }
  render_thermo_view();
  lv_obj_clear_flag(thermo_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(thermo_view);
}

static lv_obj_t* make_setpoint_row(lv_obj_t* parent, const char* caption,
                                   lv_obj_t** valOut, int minusCode, int plusCode, int y) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 440, 54);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* cap = lv_label_create(row);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(0xF0F6FC), LV_PART_MAIN);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(cap, LV_ALIGN_LEFT_MID, 4, 0);

  lv_obj_t* minus = lv_btn_create(row);
  lv_obj_set_size(minus, 50, 50);
  lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -160, 0);
  style_frosted_btn(minus);
  lv_obj_t* ml = lv_label_create(minus); lv_label_set_text(ml, "-");
  lv_obj_set_style_text_font(ml, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(ml, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_center(ml);
  lv_obj_add_event_cb(minus, setpoint_cb, LV_EVENT_CLICKED, (void*)(intptr_t)minusCode);

  *valOut = lv_label_create(row);
  lv_label_set_text(*valOut, "--°");
  lv_obj_set_style_text_color(*valOut, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(*valOut, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_align(*valOut, LV_ALIGN_RIGHT_MID, -96, 0);

  lv_obj_t* plus = lv_btn_create(row);
  lv_obj_set_size(plus, 50, 50);
  lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -4, 0);
  style_frosted_btn(plus);
  lv_obj_t* pl = lv_label_create(plus); lv_label_set_text(pl, "+");
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(pl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_center(pl);
  lv_obj_add_event_cb(plus, setpoint_cb, LV_EVENT_CLICKED, (void*)(intptr_t)plusCode);
  return row;
}

static void build_thermo_view(lv_obj_t* scr) {
  thermo_view = lv_obj_create(scr);
  lv_obj_set_size(thermo_view, 480, 480);
  lv_obj_set_pos(thermo_view, 0, 0);
  lv_obj_set_style_bg_color(thermo_view, lv_color_hex(0x4F7CFF), LV_PART_MAIN);
  lv_obj_set_style_bg_grad(thermo_view, &bg_grad, LV_PART_MAIN);   // match front page
  lv_obj_set_style_bg_opa(thermo_view, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(thermo_view, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(thermo_view, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(thermo_view, 0, LV_PART_MAIN);
  lv_obj_clear_flag(thermo_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(thermo_view, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* title = lv_label_create(thermo_view);
  lv_label_set_text(title, "Nest Thermostat");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 16);

  lv_obj_t* close = lv_btn_create(thermo_view);
  lv_obj_set_size(close, 44, 44);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -12, 8);
  style_frosted_btn(close);
  lv_obj_t* xl = lv_label_create(close); lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(xl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_center(xl);
  lv_obj_add_event_cb(close, thermo_close_cb, LV_EVENT_CLICKED, NULL);

  tv_current = lv_label_create(thermo_view);
  lv_label_set_text(tv_current, "--°");
  lv_obj_set_style_text_color(tv_current, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(tv_current, &lv_font_montserrat_32, LV_PART_MAIN);
  lv_obj_align(tv_current, LV_ALIGN_TOP_MID, 0, 56);

  tv_action = lv_label_create(thermo_view);
  lv_label_set_text(tv_action, "");
  lv_obj_set_style_text_font(tv_action, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(tv_action, LV_ALIGN_TOP_MID, 0, 102);

  tv_low_row    = make_setpoint_row(thermo_view, "Heat to", &tv_low_val,    (0 << 1) | 0, (0 << 1) | 1, 150);
  tv_high_row   = make_setpoint_row(thermo_view, "Cool to", &tv_high_val,   (1 << 1) | 0, (1 << 1) | 1, 214);
  tv_single_row = make_setpoint_row(thermo_view, "Set to",  &tv_single_val, (2 << 1) | 0, (2 << 1) | 1, 150);

  // Fan timer row
  lv_obj_t* fan_cap = lv_label_create(thermo_view);
  lv_label_set_text(fan_cap, "Fan");
  lv_obj_set_style_text_color(fan_cap, lv_color_hex(0xF0F6FC), LV_PART_MAIN);
  lv_obj_set_style_text_font(fan_cap, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(fan_cap, LV_ALIGN_TOP_LEFT, 20, 300);

  static const char* const FAN_LBL[5] = { "Off", "1h", "2h", "4h", "On" };
  int fbw = 83, fbg = 6, fx0 = (480 - (5 * fbw + 4 * fbg)) / 2;
  for (int i = 0; i < 5; i++) {
    tv_fan_btns[i] = lv_btn_create(thermo_view);
    lv_obj_set_size(tv_fan_btns[i], fbw, 44);
    lv_obj_set_pos(tv_fan_btns[i], fx0 + i * (fbw + fbg), 326);
    style_frosted_btn(tv_fan_btns[i]);
    tv_fan_lbls[i] = lv_label_create(tv_fan_btns[i]);
    lv_label_set_text(tv_fan_lbls[i], FAN_LBL[i]);
    lv_obj_set_style_text_font(tv_fan_lbls[i], &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(tv_fan_lbls[i]);
    lv_obj_add_event_cb(tv_fan_btns[i], fan_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  update_fan_buttons();
}

// Debounced setpoint send: ~500 ms after the last tap, push one service call.
static void setpoint_flush_cb(lv_timer_t*) {
  if (setpoint_pending && millis() - setpoint_tap_ms > 500) {
    setpoint_pending = false;
    setpoint_send_ms = millis();
    if (thermo.dual) ha_climate_set_temp_dual(thermo.low, thermo.high);
    else             ha_climate_set_temp_single(thermo.target);
  }
}

static void time_cb(lv_timer_t*) {
  struct tm t;
  if (!getLocalTime(&t, 100)) return;
  char buf[32];
  strftime(buf, sizeof(buf), "%I:%M %p", &t);
  lv_label_set_text(time_label, buf[0] == '0' ? buf + 1 : buf);

  // Auto-off for a timed fan run.
  if (fan_off_at != 0 && (int32_t)(millis() - fan_off_at) >= 0) {
    fan_off_at = 0;
    fan_active = 0;
    update_fan_buttons();
    ha_thermo_fan(false);
  }
}

static void dim_cb(lv_timer_t*) {
  if (!is_dimmed && millis() - last_activity_ms > IDLE_DIM_MS) {
    display_set_backlight(BRIGHT_DIM);
    is_dimmed = true;
  }
}

bool ui_is_dimmed() { return is_dimmed; }

void ui_notify_touch() {
  last_activity_ms = millis();
  if (is_dimmed) {
    display_set_backlight(BRIGHT_FULL);
    is_dimmed = false;
  }
}

void ui_init() {
  last_activity_ms = millis();

  // ---- Background: vertical blue -> violet -> pink gradient ----
  bg_grad.dir = LV_GRAD_DIR_VER;
  bg_grad.stops_count = 3;
  bg_grad.stops[0].color = lv_color_hex(0x4F7CFF); bg_grad.stops[0].frac = 0;
  bg_grad.stops[1].color = lv_color_hex(0x8B5CF6); bg_grad.stops[1].frac = 128;
  bg_grad.stops[2].color = lv_color_hex(0xE15AD6); bg_grad.stops[2].frac = 255;

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x4F7CFF), LV_PART_MAIN);
  lv_obj_set_style_bg_grad(scr, &bg_grad, LV_PART_MAIN);

  // ---- Header (transparent, over the gradient) ----
  lv_obj_t* header = lv_obj_create(scr);
  lv_obj_set_size(header, 480, HEADER_H);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  time_label = lv_label_create(header);
  lv_label_set_text(time_label, "--:-- --");
  lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 22, 0);

  // Center: indoor & outdoor temperatures side by side (replaces the date).
  lv_obj_t* temps = lv_obj_create(header);
  lv_obj_remove_style_all(temps);
  lv_obj_set_size(temps, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_clear_flag(temps, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(temps, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(temps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(temps, 5, LV_PART_MAIN);
  lv_obj_align(temps, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* in_icon = lv_label_create(temps);
  lv_label_set_text(in_icon, ICON_HOME_TEMP);
  lv_obj_set_style_text_font(in_icon, &mdi_icons, LV_PART_MAIN);
  lv_obj_set_style_text_color(in_icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  indoor_temp_label = lv_label_create(temps);
  lv_label_set_text(indoor_temp_label, "--°");
  lv_obj_set_style_text_font(indoor_temp_label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(indoor_temp_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  lv_obj_t* out_icon = lv_label_create(temps);
  lv_label_set_text(out_icon, ICON_WEATHER);
  lv_obj_set_style_text_font(out_icon, &mdi_icons, LV_PART_MAIN);
  lv_obj_set_style_text_color(out_icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_pad_left(out_icon, 12, LV_PART_MAIN);  // gap between the pairs

  outdoor_temp_label = lv_label_create(temps);
  lv_label_set_text(outdoor_temp_label, "--°");
  lv_obj_set_style_text_font(outdoor_temp_label, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_obj_set_style_text_color(outdoor_temp_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  status_dot = lv_obj_create(header);
  lv_obj_set_size(status_dot, 15, 15);   // match the tile status pips
  lv_obj_set_style_bg_color(status_dot, lv_color_hex(0xA6FFCB), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(status_dot, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(status_dot, 8, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(status_dot, 8, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(status_dot, lv_color_hex(0xA6FFCB), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(status_dot, LV_OPA_70, LV_PART_MAIN);
  lv_obj_align(status_dot, LV_ALIGN_RIGHT_MID, -16, 0);

  // WiFi (link-layer) status icon — separate meaning from the HA dot: this shows
  // whether the panel is associated with the access point. Starts red (not yet
  // connected); main.cpp polls WiFi.status() and flips it white once associated.
  wifi_icon = lv_label_create(header);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0xEF4444), LV_PART_MAIN);
  lv_obj_align_to(wifi_icon, status_dot, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  // ---- Count pages ----
  for (int i = 0; i < MOSAIC_COUNT; i++) {
    if ((int)MOSAIC[i].page + 1 > page_count)
      page_count = (int)MOSAIC[i].page + 1;
  }
  if (page_count > MAX_PAGES) page_count = MAX_PAGES;

  // ---- Tileview (transparent so the gradient shows through) ----
  tileview = lv_tileview_create(scr);
  lv_obj_set_pos(tileview, 0, HEADER_H);
  lv_obj_set_size(tileview, 480, 480 - HEADER_H);
  lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tileview, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);  // swipe (touch.cpp) flips pages instantly

  // ---- Tile pages ----
  for (int p = 0; p < page_count; p++) {
    lv_dir_t dir = LV_DIR_HOR;
    if (p == 0)              dir = LV_DIR_RIGHT;
    if (p == page_count - 1) dir = LV_DIR_LEFT;
    if (page_count == 1)     dir = LV_DIR_NONE;
    tile_pages[p] = lv_tileview_add_tile(tileview, p, 0, dir);
    lv_obj_set_style_bg_opa(tile_pages[p], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_pages[p], 0, LV_PART_MAIN);
    lv_obj_clear_flag(tile_pages[p], LV_OBJ_FLAG_SCROLLABLE);
  }

  // ---- Tile cards ----
  int x_start = (480 - COLS * TILE_W - (COLS - 1) * TILE_GAP) / 2;

  for (int i = 0; i < MOSAIC_COUNT; i++) {
    uint8_t pg = MOSAIC[i].page;
    if (pg >= MAX_PAGES) continue;
    int pos = 0;
    for (int j = 0; j < i; j++) {
      if (MOSAIC[j].page == pg) pos++;
    }
    int col = pos % COLS;
    int row = pos / COLS;
    int x = x_start + col * (TILE_W + TILE_GAP);
    int y = TILE_GAP + row * (TILE_H + TILE_GAP);

    tile_btns[i] = lv_btn_create(tile_pages[pg]);
    lv_obj_set_pos(tile_btns[i], x, y);
    lv_obj_set_size(tile_btns[i], TILE_W, TILE_H);
    lv_obj_set_style_radius(tile_btns[i], 22, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile_btns[i], 13, LV_PART_MAIN);
    // brief pressed feedback
    lv_obj_set_style_transform_width(tile_btns[i], -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(tile_btns[i], -3, LV_STATE_PRESSED);
    lv_obj_add_event_cb(tile_btns[i], tile_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    // icon (top-left)
    tile_icons[i] = lv_label_create(tile_btns[i]);
    lv_label_set_text(tile_icons[i], MOSAIC[i].icon);
    lv_obj_set_style_text_font(tile_icons[i], &mdi_icons, LV_PART_MAIN);
    lv_obj_align(tile_icons[i], LV_ALIGN_TOP_LEFT, 0, 0);

    // status pip (top-right)
    tile_pips[i] = lv_obj_create(tile_btns[i]);
    lv_obj_set_size(tile_pips[i], 15, 15);
    lv_obj_set_style_radius(tile_pips[i], 8, LV_PART_MAIN);
    lv_obj_clear_flag(tile_pips[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tile_pips[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(tile_pips[i], LV_ALIGN_TOP_RIGHT, 0, 0);

    // name (bottom-left)
    tile_labels[i] = lv_label_create(tile_btns[i]);
    lv_label_set_text(tile_labels[i], MOSAIC[i].label);
    lv_obj_set_style_text_font(tile_labels[i], &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(tile_labels[i], LV_LABEL_LONG_WRAP);
    lv_obj_set_width(tile_labels[i], TILE_W - 26);
    lv_obj_align(tile_labels[i], LV_ALIGN_BOTTOM_LEFT, 0, -18);

    // value (below name)
    tile_values[i] = lv_label_create(tile_btns[i]);
    lv_label_set_text(tile_values[i], "Off");
    lv_obj_set_style_text_font(tile_values[i], &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(tile_values[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);

    apply_tile_style(i, false);     // start in OFF style
    set_pip_available(i, true);     // assume available until first state arrives
  }

  // ---- Thermostat tile (9th cell on page 0) — tap opens the detail view ----
  {
    int col = 8 % COLS, row = 8 / COLS;   // -> col 2, row 2
    int tx = x_start + col * (TILE_W + TILE_GAP);
    int ty = TILE_GAP + row * (TILE_H + TILE_GAP);
    thermo_tile = lv_btn_create(tile_pages[0]);
    lv_obj_set_pos(thermo_tile, tx, ty);
    lv_obj_set_size(thermo_tile, TILE_W, TILE_H);
    lv_obj_set_style_radius(thermo_tile, 22, LV_PART_MAIN);
    lv_obj_set_style_pad_all(thermo_tile, 13, LV_PART_MAIN);
    lv_obj_set_style_bg_color(thermo_tile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(thermo_tile, 38, LV_PART_MAIN);            // frosted neutral card
    lv_obj_set_style_border_color(thermo_tile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(thermo_tile, 72, LV_PART_MAIN);
    lv_obj_set_style_border_width(thermo_tile, 1, LV_PART_MAIN);
    lv_obj_set_style_transform_width(thermo_tile, -3, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(thermo_tile, -3, LV_STATE_PRESSED);
    lv_obj_add_event_cb(thermo_tile, thermo_open_cb, LV_EVENT_CLICKED, NULL);

    thermo_tile_icon = lv_label_create(thermo_tile);
    lv_label_set_text(thermo_tile_icon, ICON_THERMOSTAT);
    lv_obj_set_style_text_font(thermo_tile_icon, &mdi_icons, LV_PART_MAIN);
    lv_obj_set_style_text_color(thermo_tile_icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(thermo_tile_icon, 230, LV_PART_MAIN);
    lv_obj_align(thermo_tile_icon, LV_ALIGN_TOP_LEFT, 0, 0);

    thermo_tile_pip = lv_obj_create(thermo_tile);
    lv_obj_set_size(thermo_tile_pip, 15, 15);
    lv_obj_set_style_radius(thermo_tile_pip, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(thermo_tile_pip, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(thermo_tile_pip, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_clear_flag(thermo_tile_pip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(thermo_tile_pip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(thermo_tile_pip, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t* nm = lv_label_create(thermo_tile);
    lv_label_set_text(nm, "Nest Thermostat");
    lv_obj_set_style_text_color(nm, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(nm, LV_ALIGN_BOTTOM_LEFT, 0, -18);

    thermo_tile_val = lv_label_create(thermo_tile);
    lv_label_set_text(thermo_tile_val, "--°");
    lv_obj_set_style_text_color(thermo_tile_val, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(thermo_tile_val, 184, LV_PART_MAIN);
    lv_obj_set_style_text_font(thermo_tile_val, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(thermo_tile_val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  // ---- Page indicator dots ----
  if (page_count > 1) {
    for (int i = 0; i < page_count; i++) {
      page_dots[i] = lv_obj_create(scr);
      lv_obj_set_style_border_width(page_dots[i], 0, LV_PART_MAIN);
      lv_obj_clear_flag(page_dots[i], LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(page_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    update_page_dots();
  }

  // ---- Thermostat detail view (hidden overlay) ----
  build_thermo_view(scr);

  // ---- Timers ----
  lv_timer_create(setpoint_flush_cb, 150, NULL);
  lv_timer_create(time_cb, 1000, NULL);
  lv_timer_create(dim_cb, 5000, NULL);
  time_cb(NULL);
}

void ui_update_tile_state(int idx, const char* state) {
  if (idx < 0 || idx >= MOSAIC_COUNT || !tile_btns[idx]) return;
  bool on = (strcmp(state, "on") == 0);
  bool available = (strcmp(state, "unavailable") != 0 && strcmp(state, "unknown") != 0);
  tile_is_on[idx] = on;
  apply_tile_style(idx, on);
  set_pip_available(idx, available);
  lv_label_set_text(tile_values[idx], available ? (on ? "On" : "Off") : "n/a");
}

void ui_update_tile_value(int idx, const char* value) {
  if (idx < 0 || idx >= MOSAIC_COUNT || !tile_values[idx]) return;
  lv_label_set_text(tile_values[idx], value);
}

void ui_set_indoor_temp(int deg) {
  if (!indoor_temp_label) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d°", deg);
  lv_label_set_text(indoor_temp_label, buf);
}

void ui_set_outdoor_temp(int deg) {
  if (!outdoor_temp_label) return;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d°", deg);
  lv_label_set_text(outdoor_temp_label, buf);
}

void ui_update_thermostat(const ThermoState* s) {
  // While the user is adjusting (or just sent), keep the locally-owned setpoints
  // so HA's lagging echoes don't make the number jump back and "catch up".
  bool editing = setpoint_pending || (millis() - setpoint_send_ms < 3000);
  int keep_low = thermo.low, keep_high = thermo.high, keep_target = thermo.target;
  thermo = *s;
  if (editing) { thermo.low = keep_low; thermo.high = keep_high; thermo.target = keep_target; }
  thermo_tile_refresh();
  render_thermo_view();
}

// Sync fan-button highlight with the real fan entity state from HA.
void ui_set_fan_state(bool on) {
  if (!on) { fan_active = 0; fan_off_at = 0; }
  else if (fan_active == 0) { fan_active = 4; }  // on (unknown duration) -> "On"
  update_fan_buttons();
}

void ui_set_ha_connected(bool connected) {
  if (status_dot) {
    lv_color_t c = connected ? lv_color_hex(0xA6FFCB) : lv_color_hex(0xEF4444);
    lv_obj_set_style_bg_color(status_dot, c, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(status_dot, c, LV_PART_MAIN);
  }
}

// WiFi link state (associated to the AP) — independent of the HA connection.
void ui_set_wifi_connected(bool connected) {
  if (!wifi_icon) return;
  lv_obj_set_style_text_color(wifi_icon,
    connected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xEF4444), LV_PART_MAIN);
  lv_obj_set_style_text_opa(wifi_icon, connected ? 230 : 255, LV_PART_MAIN);
}

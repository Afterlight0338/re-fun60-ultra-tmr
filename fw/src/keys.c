/*
 * keys.c - threshold + debounce + keymap. See keys.h.
 */
#include "keys.h"
#include "settings.h"

#define S(col, ch) ((col) * SCAN_CH + (ch))

/* HID keyboard usages */
enum {
  A = 0x04, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S_, T, U, V, W, X, Y, Z,
  N1 = 0x1E, N2, N3, N4, N5, N6, N7, N8, N9, N0,
  ENT = 0x28, ESC, BSPC, TAB, SPC, MINS, EQL, LBRC, RBRC, BSLS,
  SCLN = 0x33, QUOT, GRV, COMM, DOT, SLSH, CAPS,
  F1 = 0x3A, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  PSCR = 0x46, INS = 0x49, DEL = 0x4C, RGHT = 0x4F, LEFT, DOWN, UP, APP = 0x65,
  LCTL = 0xE0, LSFT, LALT, LGUI, RCTL, RSFT, RALT, RGUI
};

/* Stock v306's default layout (table @0x184b8 in stock/2307/fw_at32.bin, 4 bytes per key,
 * column-major, its row r = our mux channel r - 1), confirmed key by key with rawscan
 * captures. Space is the one switch read on mux channel 6. */
#define BASE_LAYER {                                                                        \
  [S(0,0)] = ESC,  [S(1,0)] = N1, [S(2,0)] = N2, [S(3,0)] = N3, [S(4,0)] = N4, [S(5,0)] = N5, \
  [S(6,0)] = N6,   [S(7,0)] = N7, [S(8,0)] = N8, [S(9,0)] = N9, [S(10,0)] = N0,               \
  [S(11,0)] = MINS, [S(12,0)] = EQL, [S(13,0)] = BSPC,                                       \
  [S(0,1)] = TAB,  [S(1,1)] = Q,  [S(2,1)] = W,  [S(3,1)] = E,  [S(4,1)] = R,  [S(5,1)] = T,   \
  [S(6,1)] = Y,    [S(7,1)] = U,  [S(8,1)] = I,  [S(9,1)] = O,  [S(10,1)] = P,               \
  [S(11,1)] = LBRC, [S(12,1)] = RBRC, [S(13,1)] = BSLS,                                      \
  [S(0,2)] = CAPS, [S(1,2)] = A,  [S(2,2)] = S_, [S(3,2)] = D,  [S(4,2)] = F,  [S(5,2)] = G,   \
  [S(6,2)] = H,    [S(7,2)] = J,  [S(8,2)] = K,  [S(9,2)] = L,  [S(10,2)] = SCLN,            \
  [S(11,2)] = QUOT, [S(13,2)] = ENT,                                                         \
  [S(0,3)] = LSFT, [S(2,3)] = Z,  [S(3,3)] = X,  [S(4,3)] = C,  [S(5,3)] = V,  [S(6,3)] = B,   \
  [S(7,3)] = N,    [S(8,3)] = M,  [S(9,3)] = COMM, [S(10,3)] = DOT, [S(11,3)] = SLSH,        \
  [S(12,3)] = RSFT,                                                                          \
  [S(0,4)] = LCTL, [S(2,4)] = LGUI, [S(3,4)] = LALT, [S(6,6)] = SPC,                         \
  [S(9,4)] = RALT, [S(10,4)] = KC_FN, [S(11,4)] = APP, [S(12,4)] = RCTL                      \
}

/* Fn layer: stock's (@0x188b8) keyboard and media entries in stock's places; its lighting keys
 * mapped to our effects (Enter on/off, [ ] brightness, \ next effect); its connection keys in
 * its places (E R T Bluetooth 1-3, Y 2.4G, U USB), Win lock on Win, battery level on Space;
 * its profile keys have no counterpart.
 * Arrows are ours (stock has none): the right-hand cluster. */
#define FN_LAYER {                                                                          \
  [S(0,0)] = GRV, [S(1,0)] = F1, [S(2,0)] = F2, [S(3,0)] = F3, [S(4,0)] = F4, [S(5,0)] = F5,   \
  [S(6,0)] = F6,  [S(7,0)] = F7, [S(8,0)] = F8, [S(9,0)] = F9, [S(10,0)] = F10,              \
  [S(11,0)] = F11, [S(12,0)] = F12, [S(13,0)] = DEL,                                         \
  [S(3,1)] = KC_BT1, [S(4,1)] = KC_BT2, [S(5,1)] = KC_BT3, [S(6,1)] = KC_24G,                \
  [S(7,1)] = KC_USB, [S(8,1)] = INS, [S(10,1)] = PSCR,                                       \
  [S(2,4)] = KC_WINLOCK, [S(6,6)] = KC_BATT,                                                 \
  [S(11,1)] = KC_LED_BRD, [S(12,1)] = KC_LED_BRU, [S(13,1)] = KC_LED_FX,                     \
  [S(13,2)] = KC_LED_TOG,                                                                    \
  [S(4,3)] = KC_CALC, [S(8,3)] = KC_MUTE, [S(9,3)] = KC_VOLD, [S(10,3)] = KC_VOLU,           \
  [S(11,3)] = UP, [S(9,4)] = LEFT, [S(11,4)] = DOWN, [S(12,4)] = RGHT                        \
}

static const uint16_t consumer_usage[] = {          /* indexed from KC_MUTE */
  0x00E2 /* Mute */, 0x00EA /* Volume Down */, 0x00E9 /* Volume Up */, 0x0192 /* Calculator */,
  0x00CD /* Play/Pause */, 0x00B5 /* Next Track */, 0x00B6 /* Previous Track */,
  0x0070 /* Brightness Down */, 0x006F /* Brightness Up */
};

const uint8_t keymap_default[2][SCAN_SITES] = { BASE_LAYER, FN_LAYER };

/* Fn with the switch at the top: stock's Mac layer (its table @0x18cb8 + 0x200, same format) is
 * the Fn layer with brightness, Mission Control, Spotlight and media on the number row 1 .. = */
const uint8_t mac_number_row[12] = {
  KC_BRID, KC_BRIU, KC_MCTL, KC_SPOT, F5, F6, KC_PREV, KC_PLAY, KC_NEXT, KC_MUTE, KC_VOLD, KC_VOLU
};

void keys_init(keys_t *k)
{
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    k->raw_pressed[i] = k->pressed[i] = 0;
    k->rest[i] = 0;
    k->bottom[i] = 0xFFFF;
    k->pct[i] = k->ext[i] = k->rt_live[i] = k->force[i] = k->eff[i] = 0;
    k->down_ms[i] = 0;
  }
  for (unsigned i = 0; i < 32; i++) k->held[i] = 0;
  for (unsigned i = 0; i < 8; i++) k->tap_code[i] = 0;
  for (unsigned i = 0; i < ADV_MAX; i++) k->adv[i].on = k->adv[i].latched = k->adv[i].bottom = 0;
  for (unsigned p = 0; p < PLAYERS; p++) k->play[p].on = 0;
  k->winlock = 0;
  k->actions = 0;
  debounce_init(&k->db);
}

/* the layer on top of the base one: 0 none, 1 Fn, 2 Fn with the switch at the top (Mac) */
static int fn_held(const keys_t *k)
{
  for (unsigned i = 0; i < SCAN_SITES; i++)
    if (k->pressed[i] && settings.keymap[0][i] == KC_FN) return k->mac ? 2 : 1;
  return 0;
}

/* what site i means right now: the Fn layer's entry while Fn is held, else the base one */
static uint8_t code_at(unsigned i, int fn)
{
  return fn && settings.keymap[fn][i] != KC_NO ? settings.keymap[fn][i] : settings.keymap[0][i];
}

void keys_reset_adv(keys_t *k)
{
  for (unsigned i = 0; i < 32; i++) k->held[i] = 0;
  for (unsigned i = 0; i < 8; i++) k->tap_code[i] = 0;
  for (unsigned i = 0; i < ADV_MAX; i++) k->adv[i].on = k->adv[i].latched = k->adv[i].bottom = 0;
  for (unsigned p = 0; p < PLAYERS; p++) k->play[p].on = 0;
}

int keys_boot_held(uint16_t v)
{
  unsigned span = settings.span[KEY_BOOT_SITE], rest = settings.esc_rest;
  return v < 700u || (span && rest && v + span / 2u < rest);   /* mechanical, or half its saved travel */
}

void keys_relearn(keys_t *k)
{
  for (unsigned i = 0; i < SCAN_SITES; i++) { k->rest[i] = 0; k->bottom[i] = 0xFFFF; }
}

uint16_t keys_bottom(const keys_t *k, unsigned i)
{
  unsigned rest = k->rest[i], bottom = k->bottom[i], span = settings.span[i];
  if (span && rest > span && rest - span < bottom) bottom = rest - span;
  if (bottom + KEY_MIN_TRAVEL > rest) bottom = rest > KEY_MIN_TRAVEL ? rest - KEY_MIN_TRAVEL : 0;
  return (uint16_t)bottom;
}

unsigned keys_store_travel(const keys_t *k)
{
  unsigned n = 0;
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    unsigned rest = k->rest[i], bottom = k->bottom[i];
    if (settings.keymap[0][i] == KC_NO || rest < KEY_REST_MIN || rest > KEY_REST_MAX ||
        bottom + KEY_MIN_TRAVEL > rest) continue;         /* not bottomed out this session */
    settings.span[i] = (uint16_t)(rest - bottom);
    if (i == KEY_BOOT_SITE) settings.esc_rest = (uint16_t)rest;
    n++;
  }
  return n;
}

/* travel of site i in percent with the dead zones applied; learns rest and bottom */
static unsigned travel_pct(keys_t *k, unsigned i, uint16_t v)
{
  if (v > k->rest[i]) k->rest[i] = v;
  if (v < k->bottom[i]) k->bottom[i] = v;
  unsigned rest = k->rest[i], bottom = keys_bottom(k, i);
  unsigned pct = rest > bottom ? (rest - v) * 100u / (rest - bottom) : 0;   /* rest >= v */
  if (pct > 100) pct = 100;                                /* below a saved bottom */
  if (pct <= settings.top_dz) return 0;
  if (pct + settings.bottom_dz >= 100) return 100;
  return pct;
}

/* threshold or rapid-trigger state of site i at travel p (percent) */
static uint8_t key_state(keys_t *k, unsigned i, unsigned p)
{
  unsigned press = settings.act[i] ? settings.act[i] : settings.press_pct;
  unsigned gap = settings.press_pct - settings.release_pct;   /* sanitize keeps it >= 5 */
  unsigned release = press > gap ? press - gap : press / 2;
  unsigned down = settings.rt_down[i], up = settings.rt_up[i] ? settings.rt_up[i] : down;
  uint8_t on = k->raw_pressed[i];

  if (!down) return on ? p > release : p >= press;        /* plain threshold with hysteresis */

  if (p == 0 || (!settings.rt_continuous && p <= release)) k->rt_live[i] = 0;
  if (on) {
    if (p > k->ext[i]) k->ext[i] = (uint8_t)p;
    if (k->rt_live[i] && p + up > k->ext[i]) return 1;    /* hasn't risen rt_up from the deepest */
    if (!k->rt_live[i] && p > release) return 1;          /* above the release point: reset */
    k->ext[i] = (uint8_t)p;
    return 0;
  }
  if (p < k->ext[i]) k->ext[i] = (uint8_t)p;
  if (k->rt_live[i] ? p >= k->ext[i] + down : p >= press) {
    k->rt_live[i] = 1;
    k->ext[i] = (uint8_t)p;
    return 1;
  }
  return 0;
}

/* ---- advanced keys ---- */
static void hold_code(keys_t *k, uint8_t c, int on)
{
  if (on) k->held[c >> 3] |= (uint8_t)(1u << (c & 7));
  else    k->held[c >> 3] &= (uint8_t)~(1u << (c & 7));
}

/* down now, up ADV_TAP_MS later (a code already tapped just gets its time renewed) */
static void tap_code(keys_t *k, uint8_t c, uint32_t now)
{
  unsigned slot = 0;
  for (unsigned i = 0; i < 8; i++) {
    if (k->tap_code[i] == c) { slot = i; break; }
    if (!k->tap_code[i]) slot = i;
  }
  /* ponytail: 8 slots; a 9th simultaneous tap reuses one, at worst a code comes up early */
  k->tap_code[slot] = c;
  k->tap_until[slot] = now + ADV_TAP_MS;
  hold_code(k, c, 1);
}

static void dks_point(keys_t *k, const adv_t *a, unsigned point, uint32_t now)
{
  for (unsigned i = 0; i < 4; i++) {
    unsigned act = a->act[i] >> (2 * point) & 3u;
    if (!a->code[i] || !act) continue;
    if (act == 3 || (act == 1 && point == 3)) tap_code(k, a->code[i], now);   /* down at release = tap */
    else hold_code(k, a->code[i], act == 1);
  }
}

/* step the macros playing (keys.h); a run ends at an empty step or MACRO_STEPS */
static void macros_play(keys_t *k, uint32_t now)
{
  for (unsigned p = 0; p < PLAYERS; p++) {
    __typeof__(k->play[0]) *pl = &k->play[p];
    while (pl->on && (int32_t)(now - pl->next) >= 0) {
      const macro_step_t *st = pl->pos < MACRO_STEPS ? &settings.macro[pl->macro][pl->pos] : 0;
      if (!st || (!st->code && !st->arg)) {                     /* the end */
        for (unsigned c = 0; c < 256; c++)
          if (pl->down[c >> 3] >> (c & 7) & 1u) hold_code(k, (uint8_t)c, 0);
        for (unsigned i = 0; i < 32; i++) pl->down[i] = 0;
        const adv_t *a = &settings.adv[pl->entry];
        if (a->type == ADV_MACRO && a->b && k->pressed[a->a]) { pl->pos = 0; pl->next = now + ADV_TAP_MS; }
        else pl->on = 0;
        break;
      }
      pl->pos++;
      if (!st->code) { pl->next = now + st->arg * 10u; continue; }
      uint8_t c = st->code, bit = (uint8_t)(1u << (c & 7));
      if (st->arg == 3) { tap_code(k, c, now); pl->next = now + 2u * ADV_TAP_MS; }
      else {
        hold_code(k, c, st->arg == 1);
        if (st->arg == 1) pl->down[c >> 3] |= bit; else pl->down[c >> 3] &= (uint8_t)~bit;
        pl->next = now + ADV_TAP_MS;
      }
    }
  }
}

static void adv_update(keys_t *k, const uint8_t *before, uint32_t now)
{
  int other_down = 0;                    /* a normal key went down this frame (mod-tap: hold) */
  for (unsigned i = 0; i < SCAN_SITES; i++)
    if (!before[i] && k->pressed[i]) {
      unsigned own = 0;
      for (unsigned j = 0; j < ADV_MAX; j++) own |= settings.adv[j].type == ADV_MODTAP && settings.adv[j].a == i;
      other_down |= !own;
    }

  for (unsigned i = 0; i < 8; i++)
    if (k->tap_code[i] && (int32_t)(now - k->tap_until[i]) >= 0) { hold_code(k, k->tap_code[i], 0); k->tap_code[i] = 0; }

  for (unsigned j = 0; j < ADV_MAX; j++) {
    const adv_t *a = &settings.adv[j];
    if (a->a >= SCAN_SITES) continue;
    unsigned s = a->a, down = k->pressed[s], was = before[s];
    __typeof__(k->adv[0]) *r = &k->adv[j];

    switch (a->type) {
    case ADV_MODTAP: {
      uint32_t limit = (a->p0 ? a->p0 : 20u) * 10u;
      if (down && !was) { r->on = 1; r->since = now; r->latched = 0; break; }   /* undecided */
      if (r->on && !r->latched && down && (now - r->since >= limit || other_down)) {
        r->latched = 1;                                                 /* it's a hold */
        hold_code(k, a->code[1], 1);
      }
      if (!down && was && r->on) {
        if (r->latched) hold_code(k, a->code[1], 0);
        else tap_code(k, a->code[0], now);
        r->on = r->latched = 0;
      }
      break;
    }
    case ADV_TOGGLE:
      if (down && !was) {
        if (r->latched) { r->latched = 0; r->on = 0; hold_code(k, a->code[0], 0); }
        else { r->on = 1; r->since = now; hold_code(k, a->code[0], 1); }
      } else if (!down && was && r->on) {
        r->on = 0;
        if (now - r->since < 200u) r->latched = 1;                     /* a tap: stay down */
        else hold_code(k, a->code[0], 0);
      }
      break;
    case ADV_DKS: {
      unsigned bottom = a->p0 ? a->p0 : 90u;
      if (down && !was) dks_point(k, a, 0, now);
      if (down && !r->bottom && k->pct[s] >= bottom) { r->bottom = 1; dks_point(k, a, 1, now); }
      if (r->bottom && (k->pct[s] + 5u < bottom || !down)) { r->bottom = 0; dks_point(k, a, 2, now); }
      if (!down && was) {
        for (unsigned i = 0; i < 4; i++) if (a->code[i]) hold_code(k, a->code[i], 0);   /* nothing stuck */
        dks_point(k, a, 3, now);
      }
      break;
    }
    case ADV_MACRO:
      if (down && !was) {
        unsigned p, busy = 0;
        for (p = 0; p < PLAYERS; p++) busy |= k->play[p].on && k->play[p].entry == j;   /* still playing */
        for (p = 0; p < PLAYERS && k->play[p].on; p++) { }
        /* ponytail: a third macro at once is dropped; PLAYERS is the knob */
        if (!busy && p < PLAYERS) {
          k->play[p].on = 1; k->play[p].macro = a->p0; k->play[p].entry = (uint8_t)j;
          k->play[p].pos = 0; k->play[p].next = now;
          for (unsigned i = 0; i < 32; i++) k->play[p].down[i] = 0;
        }
      }
      break;
    default:
      break;
    }
  }
  macros_play(k, now);
}

int keys_update(keys_t *k, const uint16_t *raw, uint32_t now_ms)
{
  uint8_t before[SCAN_SITES], hold[SCAN_SITES];
  int changed = 0;

  for (unsigned i = 0; i < SCAN_SITES; i++) {
    unsigned p = travel_pct(k, i, raw[i]);
    k->pct[i] = (uint8_t)p;
    k->raw_pressed[i] = key_state(k, i, p);
    if (k->rest[i] < KEY_REST_MIN || k->rest[i] > KEY_REST_MAX || settings.keymap[0][i] == KC_NO)
      k->raw_pressed[i] = 0;
    if (k->force[i]) k->raw_pressed[i] = 1;
    hold[i] = settings.rt_down[i] ? 0 : settings.debounce_ms;  /* rapid trigger: no release hold-off */
    before[i] = k->pressed[i];
  }
  debounce_process(&k->db, k->raw_pressed, now_ms, hold, k->pressed);

  uint8_t held_before[32], eff_before[SCAN_SITES], owned[SCAN_SITES] = {0};
  for (unsigned i = 0; i < 32; i++) held_before[i] = k->held[i];
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    eff_before[i] = k->eff[i];
    if (!before[i] && k->pressed[i]) k->down_ms[i] = now_ms;
  }
  for (unsigned j = 0; j < ADV_MAX; j++) {
    const adv_t *a = &settings.adv[j];
    if (a->type >= ADV_MODTAP && a->type <= ADV_MACRO && a->a < SCAN_SITES) owned[a->a] = 1;
  }

  int fn = fn_held(k);
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    if (before[i] == k->pressed[i] || owned[i]) continue;
    uint8_t code = k->pressed[i] ? code_at(i, fn) : KC_NO;   /* lighting acts on the press */
    if (code >= KC_LED_TOG && code <= KC_LED_FX) k->actions |= (uint8_t)(1u << (code - KC_LED_TOG));
    if (code == KC_WINLOCK) k->winlock = !k->winlock;
  }
  adv_update(k, before, now_ms);

  for (unsigned i = 0; i < SCAN_SITES; i++) k->eff[i] = k->pressed[i] && !owned[i];
  for (unsigned j = 0; j < ADV_MAX; j++) {                      /* SOCD on what's left */
    const adv_t *a = &settings.adv[j];
    if (a->type != ADV_SOCD || a->a >= SCAN_SITES || a->b >= SCAN_SITES || !k->eff[a->a] || !k->eff[a->b]) continue;
    unsigned lose = a->p0 == 1 ? a->b : a->p0 == 2 ? a->a
                  : a->p0 == 3 ? SCAN_SITES : (int32_t)(k->down_ms[a->a] - k->down_ms[a->b]) >= 0 ? a->b : a->a;
    if (lose == SCAN_SITES) k->eff[a->a] = k->eff[a->b] = 0;
    else k->eff[lose] = 0;
  }

  for (unsigned i = 0; i < SCAN_SITES; i++) changed |= eff_before[i] != k->eff[i] || before[i] != k->pressed[i];
  for (unsigned i = 0; i < 32; i++) changed |= held_before[i] != k->held[i];
  return changed;
}

#define HAS(set, c) ((set)[(c) >> 3] >> ((c) & 7) & 1u)

/* every code held right now, one bit per code: mapped keys plus advanced-key output */
static void keys_codes(const keys_t *k, uint8_t set[32])
{
  int fn = fn_held(k);
  for (unsigned i = 0; i < 32; i++) set[i] = k->held[i];
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    if (!k->eff[i]) continue;
    uint8_t code = code_at(i, fn);
    set[code >> 3] |= (uint8_t)(1u << (code & 7));
  }
  if (HAS(set, KC_MCTL)) { set[LCTL >> 3] |= 1u << (LCTL & 7); set[UP >> 3] |= 1u << (UP & 7); }    /* Ctrl + Up */
  if (HAS(set, KC_SPOT)) { set[LGUI >> 3] |= 1u << (LGUI & 7); set[SPC >> 3] |= 1u << (SPC & 7); }  /* Cmd + Space */
}

/* codes that go to the host as keyboard usages: not Fn, not the firmware's own (0xA5-0xDF) */
static int sent_code(unsigned c) { return c > KC_FN && (c < 0xA5u || c >= LCTL); }

void keys_report(const keys_t *k, uint8_t report[8])
{
  uint8_t set[32];
  unsigned n = 0;
  keys_codes(k, set);
  for (unsigned i = 0; i < 8; i++) report[i] = 0;
  for (unsigned c = 2; c < 256; c++) {
    if (!HAS(set, c) || !sent_code(c)) continue;
    if (c >= LCTL) { report[0] |= (uint8_t)(1u << (c - LCTL)); continue; }
    if (n < 6) report[2 + n] = (uint8_t)c;
    n++;
  }
  if (n > 6) for (unsigned i = 2; i < 8; i++) report[i] = 0x01;   /* ErrorRollOver */
  if (k->winlock) report[0] &= (uint8_t)~0x88u;                     /* LGUI, RGUI */
}

void keys_report_nkro(const keys_t *k, uint8_t report[NKRO_LEN])
{
  uint8_t set[32];
  keys_codes(k, set);
  for (unsigned i = 0; i < NKRO_LEN; i++) report[i] = 0;
  for (unsigned c = 2; c < 256; c++) {
    if (!HAS(set, c) || !sent_code(c)) continue;
    if (c >= LCTL) report[0] |= (uint8_t)(1u << (c - LCTL));
    else if (c < 8u * (NKRO_LEN - 1)) report[1 + (c >> 3)] |= (uint8_t)(1u << (c & 7));
  }
  if (k->winlock) report[0] &= (uint8_t)~0x88u;
}

uint16_t keys_consumer(const keys_t *k)
{
  uint8_t set[32];
  keys_codes(k, set);
  for (unsigned c = KC_MUTE; c <= KC_BRIU; c++)
    if (HAS(set, c)) return consumer_usage[c - KC_MUTE];
  return 0;
}

uint8_t keys_held(const keys_t *k, uint8_t lo, uint8_t hi)
{
  int fn = fn_held(k);
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    uint8_t code = k->eff[i] ? code_at(i, fn) : KC_NO;
    if (code >= lo && code <= hi) return code;
  }
  return 0;
}

uint8_t keys_take_actions(keys_t *k)
{
  uint8_t a = k->actions;
  k->actions = 0;
  return a;
}

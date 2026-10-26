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
  0x00CD /* Play/Pause */, 0x00B5 /* Next Track */, 0x00B6 /* Previous Track */
};

const uint8_t keymap_default[2][SCAN_SITES] = { BASE_LAYER, FN_LAYER };

void keys_init(keys_t *k)
{
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    k->raw_pressed[i] = k->pressed[i] = 0;
    k->rest[i] = 0;
    k->bottom[i] = 0xFFFF;
  }
  k->winlock = 0;
  k->actions = 0;
  debounce_init(&k->db);
}

static int fn_held(const keys_t *k)
{
  for (unsigned i = 0; i < SCAN_SITES; i++)
    if (k->pressed[i] && settings.keymap[0][i] == KC_FN) return 1;
  return 0;
}

/* what site i means right now: the Fn layer's entry while Fn is held, else the base one */
static uint8_t code_at(unsigned i, int fn)
{
  return fn && settings.keymap[1][i] != KC_NO ? settings.keymap[1][i] : settings.keymap[0][i];
}

int keys_update(keys_t *k, const uint16_t *raw, uint32_t now_ms)
{
  uint8_t before[SCAN_SITES];
  int changed = 0;

  for (unsigned i = 0; i < SCAN_SITES; i++) {
    uint16_t v = raw[i];
    if (v > k->rest[i]) k->rest[i] = v;
    if (v < k->bottom[i]) k->bottom[i] = v;
    unsigned rest = k->rest[i], bottom = k->bottom[i];
    if (bottom + KEY_MIN_TRAVEL > rest) bottom = rest > KEY_MIN_TRAVEL ? rest - KEY_MIN_TRAVEL : 0;
    unsigned pct = rest > bottom ? (rest - v) * 100u / (rest - bottom) : 0;   /* rest >= v */
    k->raw_pressed[i] = k->raw_pressed[i] ? pct > settings.release_pct : pct >= settings.press_pct;
    if (rest < KEY_REST_MIN || rest > KEY_REST_MAX || settings.keymap[0][i] == KC_NO)
      k->raw_pressed[i] = 0;
    before[i] = k->pressed[i];
  }
  debounce_process(&k->db, k->raw_pressed, now_ms, settings.debounce_ms, k->pressed);

  int fn = fn_held(k);
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    if (before[i] == k->pressed[i]) continue;
    changed = 1;
    uint8_t code = k->pressed[i] ? code_at(i, fn) : KC_NO;   /* lighting acts on the press */
    if (code >= KC_LED_TOG && code <= KC_LED_FX) k->actions |= (uint8_t)(1u << (code - KC_LED_TOG));
    if (code == KC_WINLOCK) k->winlock = !k->winlock;
  }
  return changed;
}

void keys_report(const keys_t *k, uint8_t report[8])
{
  unsigned n = 0;
  int fn = fn_held(k);
  for (unsigned i = 0; i < 8; i++) report[i] = 0;

  for (unsigned i = 0; i < SCAN_SITES; i++) {
    if (!k->pressed[i]) continue;
    uint8_t code = code_at(i, fn);
    if (code == KC_NO || code == KC_FN || (code >= 0xA5u && code < LCTL)) continue;  /* internal */
    if (code >= LCTL) { report[0] |= (uint8_t)(1u << (code - LCTL)); continue; }
    if (n < 6) report[2 + n] = code;
    n++;
  }
  if (n > 6) for (unsigned i = 2; i < 8; i++) report[i] = 0x01;   /* ErrorRollOver */
  if (k->winlock) report[0] &= (uint8_t)~0x88u;                     /* LGUI, RGUI */
}

uint16_t keys_consumer(const keys_t *k)
{
  int fn = fn_held(k);
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    uint8_t code = k->pressed[i] ? code_at(i, fn) : KC_NO;
    if (code >= KC_MUTE && code <= KC_PREV) return consumer_usage[code - KC_MUTE];
  }
  return 0;
}

uint8_t keys_held(const keys_t *k, uint8_t lo, uint8_t hi)
{
  int fn = fn_held(k);
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    uint8_t code = k->pressed[i] ? code_at(i, fn) : KC_NO;
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

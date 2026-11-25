// DS4 Bridge platform - reads BT gamepad, drives GPIO pins + PWM sticks
#include <stddef.h>
#include <string.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <uni.h>
#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// ---------------------------------------------------------------------------
// GPIO pin mapping (Pico W → GP2040-CE Pico)
// GP2040-CE reads active-LOW: LOW = pressed, HIGH = released
// ---------------------------------------------------------------------------
#define PIN_B1      2   // Cross    / A
#define PIN_B2      3   // Circle   / B
#define PIN_B3      4   // Square   / X
#define PIN_B4      5   // Triangle / Y
#define PIN_L1      6
#define PIN_R1      7
#define PIN_L2      8
#define PIN_R2      9
#define PIN_L3      10
#define PIN_R3      11
#define PIN_S1      12  // Share   / Select
#define PIN_S2      13  // Options / Start
#define PIN_A1      14  // PS / Guide
#define PIN_A2      15  // Touchpad click
#define PIN_UP      16
#define PIN_DOWN    17
#define PIN_LEFT    18
#define PIN_RIGHT   19

// ---------------------------------------------------------------------------
// PWM pin mapping (Pico W → RC фильтр 10кОм + 100нФ → ADC обычного Pico)
//
//  Pico W пин │ → │ обычный Pico пин (ADC)
//  ───────────┼───┼──────────────────────
//  GP21       │   │ GP26  (ADC0, Left  X)
//  GP22       │   │ GP27  (ADC1, Left  Y)
//  GP26       │   │ GP28  (ADC2, Right X)
//  GP28       │   │ GP29  (ADC3, Right Y)
//
// GP26/GP28 на Pico W — ADC пины, но gpio_set_function(PWM) корректно
// переключает их в PWM-выход без конфликтов.
// ---------------------------------------------------------------------------
#define PIN_PWM_LX  21  // Left  X → GP26 обычного Pico
#define PIN_PWM_LY  22  // Left  Y → GP27 обычного Pico
#define PIN_PWM_RX  26  // Right X → GP28 обычного Pico
#define PIN_PWM_RY  28  // Right Y → GP29 обычного Pico

// ---------------------------------------------------------------------------
// PWM: 125 МГц / 256 ≈ 488 кГц, 8 бит
// С R=10кОм, C=100нФ: τ = 1 мс, время отклика 5 мс, пульсации ~7 мВ ✓
// ---------------------------------------------------------------------------
#define PWM_WRAP    255

// Bluepad32: ось -512..+511, центр = 0
// → PWM 0..255, центр = 128
// 128/255 * 3.3В = 1.655В — центр диапазона ADC на GP2040-CE
static inline uint16_t axis_to_pwm(int32_t axis) {
    int32_t v = (int32_t)128 + (axis * 127) / 512;
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint16_t)v;
}

static void pwm_init_pin(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    uint chan  = pwm_gpio_to_channel(pin);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, PWM_WRAP);
    pwm_init(slice, &cfg, true);

    // Центр сразу — конденсаторы начинают заряжаться до 1.655В
    pwm_set_chan_level(slice, chan, 128);
}

static inline void set_stick(uint pin, int32_t axis) {
    pwm_set_chan_level(pwm_gpio_to_slice_num(pin),
                      pwm_gpio_to_channel(pin),
                      axis_to_pwm(axis));
}

// ---------------------------------------------------------------------------
// Digital GPIO
// ---------------------------------------------------------------------------
static const uint8_t ALL_PINS[] = {
    PIN_B1, PIN_B2, PIN_B3, PIN_B4,
    PIN_L1, PIN_R1, PIN_L2, PIN_R2,
    PIN_L3, PIN_R3,
    PIN_S1, PIN_S2, PIN_A1, PIN_A2,
    PIN_UP, PIN_DOWN, PIN_LEFT, PIN_RIGHT
};
#define PIN_COUNT (sizeof(ALL_PINS) / sizeof(ALL_PINS[0]))

static void gpio_bridge_init(void) {
    for (size_t i = 0; i < PIN_COUNT; i++) {
        gpio_init(ALL_PINS[i]);
        gpio_set_dir(ALL_PINS[i], GPIO_OUT);
        gpio_put(ALL_PINS[i], 1);  // HIGH = released
    }
}

static void pwm_bridge_init(void) {
    pwm_init_pin(PIN_PWM_LX);
    pwm_init_pin(PIN_PWM_LY);
    pwm_init_pin(PIN_PWM_RX);
    pwm_init_pin(PIN_PWM_RY);
}

static void release_all(void) {
    for (size_t i = 0; i < PIN_COUNT; i++)
        gpio_put(ALL_PINS[i], 1);
    set_stick(PIN_PWM_LX, 0);
    set_stick(PIN_PWM_LY, 0);
    set_stick(PIN_PWM_RX, 0);
    set_stick(PIN_PWM_RY, 0);
}

static inline void set_pin(uint pin, bool pressed) {
    gpio_put(pin, pressed ? 0 : 1);
}

// ---------------------------------------------------------------------------
// Platform callbacks
// ---------------------------------------------------------------------------
static void my_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    gpio_bridge_init();
    pwm_bridge_init();
    logi("ds4_bridge: GPIO + PWM pins ready\n");

    // Ждём 3 секунды — конденсаторы RC-фильтров заряжаются до 1.655В
    // ДО того как GP2040-CE сделает auto-calibration.
    // Без этого GP2040-CE калибрует стики по неустановившемуся напряжению
    // и они дрифтят в одну сторону.
    logi("ds4_bridge: waiting for RC caps to stabilize (3s)...\n");
    sleep_ms(3000);
    logi("ds4_bridge: ready, starting BT scan\n");
}

static void my_platform_on_init_complete(void) {
    logi("ds4_bridge: BT init complete, scanning...\n");
    uni_bt_del_keys_unsafe();
    uni_bt_start_scanning_and_autoconnect_unsafe();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

static uni_error_t my_platform_on_device_discovered(bd_addr_t addr,
                                                      const char* name,
                                                      uint16_t cod,
                                                      uint8_t rssi) {
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) ==
        UNI_BT_COD_MINOR_KEYBOARD)
        return UNI_ERROR_IGNORE_DEVICE;
    logi("ds4_bridge: discovered '%s' rssi=%d\n", name ? name : "?", rssi);
    return UNI_ERROR_SUCCESS;
}

static void my_platform_on_device_connected(uni_hid_device_t* d) {
    logi("ds4_bridge: connected %p\n", d);
}

static void my_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("ds4_bridge: disconnected %p\n", d);
    release_all();
}

static uni_error_t my_platform_on_device_ready(uni_hid_device_t* d) {
    logi("ds4_bridge: device ready %p\n", d);
    return UNI_ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Main data handler
// ---------------------------------------------------------------------------
static void my_platform_on_controller_data(uni_hid_device_t* d,
                                            uni_controller_t* ctl) {
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD)
        return;

    uni_gamepad_t* gp = &ctl->gamepad;
    uint32_t btns = gp->buttons;
    uint8_t  misc = gp->misc_buttons;
    uint8_t  dpad = gp->dpad;

    // Face buttons
    set_pin(PIN_B1, btns & BUTTON_A);
    set_pin(PIN_B2, btns & BUTTON_B);
    set_pin(PIN_B3, btns & BUTTON_X);
    set_pin(PIN_B4, btns & BUTTON_Y);

    // Shoulder / trigger
    set_pin(PIN_L1, btns & BUTTON_SHOULDER_L);
    set_pin(PIN_R1, btns & BUTTON_SHOULDER_R);
    set_pin(PIN_L2, btns & BUTTON_TRIGGER_L);
    set_pin(PIN_R2, btns & BUTTON_TRIGGER_R);

    // Stick clicks
    set_pin(PIN_L3, btns & BUTTON_THUMB_L);
    set_pin(PIN_R3, btns & BUTTON_THUMB_R);

    // System / misc
    set_pin(PIN_S1, misc & MISC_BUTTON_BACK);
    set_pin(PIN_S2, misc & MISC_BUTTON_HOME);
    set_pin(PIN_A1, misc & MISC_BUTTON_SYSTEM);
    set_pin(PIN_A2, misc & MISC_BUTTON_CAPTURE);

    // D-Pad
    set_pin(PIN_UP,    dpad & DPAD_UP);
    set_pin(PIN_DOWN,  dpad & DPAD_DOWN);
    set_pin(PIN_LEFT,  dpad & DPAD_LEFT);
    set_pin(PIN_RIGHT, dpad & DPAD_RIGHT);

    // Analog sticks
    set_stick(PIN_PWM_LX, gp->axis_x);
    set_stick(PIN_PWM_LY, gp->axis_y);
    set_stick(PIN_PWM_RX, gp->axis_rx);
    set_stick(PIN_PWM_RY, gp->axis_ry);

    if (btns || misc || dpad ||
        gp->axis_x || gp->axis_y || gp->axis_rx || gp->axis_ry) {
        logi("btns=%08lx misc=%02x dpad=%02x "
             "LX=%d LY=%d RX=%d RY=%d\n",
             (unsigned long)btns, misc, dpad,
             gp->axis_x, gp->axis_y,
             gp->axis_rx, gp->axis_ry);
    }
}

static const uni_property_t* my_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void my_platform_on_oob_event(uni_platform_oob_event_t event,
                                      void* data) {
    ARG_UNUSED(data);
    logi("ds4_bridge: oob event %d\n", event);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name                   = "DS4 Bridge",
        .init                   = my_platform_init,
        .on_init_complete       = my_platform_on_init_complete,
        .on_device_discovered   = my_platform_on_device_discovered,
        .on_device_connected    = my_platform_on_device_connected,
        .on_device_disconnected = my_platform_on_device_disconnected,
        .on_device_ready        = my_platform_on_device_ready,
        .on_oob_event           = my_platform_on_oob_event,
        .on_controller_data     = my_platform_on_controller_data,
        .get_property           = my_platform_get_property,
    };
    return &plat;
}

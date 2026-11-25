// DS4 Bridge - reads any BT gamepad, outputs to GPIO
#include <btstack_run_loop.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <uni.h>
#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

// Defined in my_platform.c
struct uni_platform* get_my_platform(void);

int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        loge("failed to initialise cyw43_arch\n");
        return -1;
    }

    // LED on during init
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    uni_platform_set_custom(get_my_platform());
    uni_init(0, NULL);

    // Does not return
    btstack_run_loop_execute();
    return 0;
}

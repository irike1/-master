#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_hid.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/sys/util.h>
#include "led_strip.h"
#include <errno.h>
#include <string.h>
#define LOG_LEVEL 4
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();
static uint8_t       key_report[8] = { 0 };
static uint8_t       pressed_key_usages[6] = { 0 };
static size_t        pressed_count = 0;
static const struct device *hid_dev = NULL;

// static const struct gpio_dt_spec led0  = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#define STRIP_NODE DT_ALIAS(led_strip)
#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
#define STRIP_NUM_PIXELS DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
#error Unable to determine length of LED strip
#endif
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

#define DELAY_TIME K_MSEC(CONFIG_SAMPLE_LED_UPDATE_DELAY)

#define RGB(_r, _g, _b, _w, _a) { .r = (_r), .g = (_g), .b = (_b), .w = (_w), .a = (_a) }

#define KBD_NODE DT_ALIAS(kbd)

static const struct led_rgb colors[] = {
    RGB(0x0f, 0x00, 0x00, 0x00, 0x00),
    RGB(0x00, 0x0f, 0x00, 0x00, 0x00),
    RGB(0x00, 0x00, 0x0f, 0x00, 0x00),
    RGB(0x00, 0x00, 0x00, 0x0f, 0x00),
    RGB(0x00, 0x00, 0x00, 0x00, 0x0f),
};

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static const struct device *const kbd = DEVICE_DT_GET(KBD_NODE);

/* Ensure keypad device is ready */
static int kbd_init_check(void)
{
    if (!device_is_ready(kbd)) {
        printk("kbd device not ready\n");
        return -ENODEV;
    }
    return 0;
}

static uint8_t mod_state = 0;  /* modifier bitmask */

/* Input -> HID keyboard (6KRO) */
void handle_key_event(struct input_event *evt, void *user_data)
{
    if (evt->type != INPUT_EV_KEY) {
        return;
    }

    uint16_t code = evt->code;
    bool press    = (evt->value == 1); /* 1=down, 0=up; ignore 2=repeat */

    uint8_t hid_mod_bit = input_to_hid_modifier(code);
    int16_t hid_usage   = input_to_hid_code(code);

    if (hid_usage < 0 && hid_mod_bit == 0) {
        /* not a keyboard usage or modifier */
        return;
    }

    /* update modifiers */
    if (hid_mod_bit) {
        if (press) {
            mod_state |= hid_mod_bit;
        } else {
            mod_state &= ~hid_mod_bit;
        }
    }

    /* update 6-key array for non-modifier usages */
    if (hid_usage >= 0) {
        if (press) {
            bool exists = false;
            for (size_t i = 0; i < pressed_count; ++i) {
                if (pressed_key_usages[i] == (uint8_t)hid_usage) { exists = true; break; }
            }
            if (!exists && pressed_count < 6) {
                pressed_key_usages[pressed_count++] = (uint8_t)hid_usage;
            }
        } else {
            for (size_t i = 0; i < pressed_count; ++i) {
                if (pressed_key_usages[i] == (uint8_t)hid_usage) {
                    for (size_t j = i; j + 1 < pressed_count; ++j) {
                        pressed_key_usages[j] = pressed_key_usages[j + 1];
                    }
                    pressed_count--;
                    break;
                }
            }
        }
    }

    if (!hid_dev) {
        return; /* HID not ready yet */
    }

    /* build and send 8-byte keyboard report */
    key_report[0] = mod_state;
    key_report[1] = 0x00;
    for (size_t i = 0; i < 6; ++i) {
        key_report[2 + i] = (i < pressed_count) ? pressed_key_usages[i] : 0x00;
    }

    int ret = hid_int_ep_write(hid_dev, key_report, sizeof(key_report), NULL);
    if (ret) {
        LOG_WRN("HID write failed: %d", ret);
    }
}

/* Limit input listener to the keypad device */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(KBD_NODE), handle_key_event, NULL);

int main(void)
{
    /* USB HID setup */
    hid_dev = device_get_binding("HID_0");
    if (!hid_dev) {
        printk("HID device not found\n");
        return 0;
    }
    usb_hid_register_device(hid_dev, hid_report_desc, sizeof(hid_report_desc), NULL);
    if (usb_hid_init(hid_dev)) {
        printk("USB HID init failed\n");
        return 0;
    }
    if (usb_enable(NULL)) {
        printk("USB enable failed\n");
        return 0;
    }

    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip device %s is not ready", strip->name);
        return 0;
    }

    if (kbd_init_check()) {
        return 0;
    }

    /* LED chase */
    size_t color = 0;
    // int rc;
    // if (gpio_is_ready_dt(&led0)) {
    //     gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    // }

    //    for (;;) {
    //     k_sleep(K_FOREVER);
    // }
}

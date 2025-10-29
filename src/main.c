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



/* HID keyboard report descriptor (standard boot keyboard with 6KRO) */
static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();

/* Buffer for current keys pressed (up to 6 keys) */
static uint8_t key_report[8] = { 0 };  /* [0]=mods, [1]=reserved, [2..7]=keys */

/* Track pressed keys count and list for building reports */
static uint8_t pressed_key_usages[6] = { 0 };
static size_t pressed_count = 0;

static void handle_key_event(struct input_event *evt, void *user_data)
{
    if (evt->type != INPUT_EV_KEY) {
        return;  /* Ignore non-key events */
    }
    uint16_t code = evt->code;
    int32_t value = evt->value;  /* 1 for press, 0 for release */

    /* Only handle our keypad keys (INPUT_KEY_1 through INPUT_KEY_4) */
    // if (code < INPUT_KEY_1 || code > INPUT_KEY_4) {
    //     return;
    // }

    /* Convert to HID usage code and modifier (if any) */
    int16_t hid_usage = input_to_hid_code(code);
    uint8_t hid_modifier = input_to_hid_modifier(code);

    if (hid_usage < 0) {
        return;  // Unknown code, should not happen for our keys
    }

    if (value) {
        /* Key pressed: add to pressed keys list */
        if (pressed_count < 6) {
            pressed_key_usages[pressed_count++] = (uint8_t)hid_usage;
        }
    } else {
        /* Key released: remove from pressed keys list */
        for (size_t i = 0; i < pressed_count; ++i) {
            if (pressed_key_usages[i] == (uint8_t)hid_usage) {
                /* Shift the remaining keys down in the list */
                for (size_t j = i; j < pressed_count - 1; ++j) {
                    pressed_key_usages[j] = pressed_key_usages[j+1];
                }
                pressed_count--;
                break;
            }
        }
    }

    /* Build HID report for current keys pressed */
    key_report[0] = 0x00;  /* no modifiers by default */
    key_report[1] = 0x00;  /* reserved byte */
    /* Set modifier byte if any pressed key is a modifier (not the case for 1–4) */
    if (hid_modifier && value) {
        key_report[0] |= hid_modifier;
    }
    /* Fill key code slots, pad with 0x00 for no key */
    for (size_t i = 0; i < 6; ++i) {
        key_report[2 + i] = (i < pressed_count) ? pressed_key_usages[i] : 0x00;
    }

    /* Send report via HID interrupt endpoint */
    hid_int_ep_write(device_get_binding("HID_0"), key_report, sizeof(key_report), NULL);
}
INPUT_CALLBACK_DEFINE(NULL, handle_key_event, NULL);
int main(void)
{
    const struct device *hid_dev;
    int ret;

    /* Retrieve the default HID device (instance 0) */
    hid_dev = device_get_binding("HID_0");
    if (hid_dev == NULL) {
        printk("ERROR: Cannot find HID device\n");
        return;
    }

    /* Register HID report descriptor and initialize HID device */
    usb_hid_register_device(hid_dev, hid_report_desc, sizeof(hid_report_desc), NULL);
    ret = usb_hid_init(hid_dev);
    if (ret != 0) {
        printk("ERROR: Failed to initialize USB HID (err %d)\n", ret);
        return;
    }

    /* Enable the USB device */
    ret = usb_enable(NULL);
    if (ret != 0) {
        printk("ERROR: Failed to enable USB (err %d)\n", ret);
        return;
    }

    printk("2x2 Keypad HID application ready.\n");
    /* The input callback will now handle key events and send HID reports */
    for (;;) {
        k_sleep(K_FOREVER);
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

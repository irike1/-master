#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_hid.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/drivers/gpio.h>

/* -----------------------------------------------------------------
 * USB HID (unchanged from previous revision)
 * -----------------------------------------------------------------*/
static const uint8_t hid_report_desc[] = HID_KEYBOARD_REPORT_DESC();
static uint8_t       key_report[8] = { 0 };
static uint8_t       pressed_key_usages[6] = { 0 };
static size_t        pressed_count = 0;

/* -----------------------------------------------------------------
 * LEDs: LED0‑on‑board (P0.28) + LED1‑external (P0.01)
 * -----------------------------------------------------------------*/
static const struct gpio_dt_spec led0  = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios); /* P0.28 */
static const struct gpio_dt_spec led1  = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios); /* P0.01 */
static bool blink_enabled = true;  /* toggled by button SW1 */

/* -----------------------------------------------------------------
 * Button SW1 (on‑board Button 1) toggles blink_enabled
 * -----------------------------------------------------------------*/
static const struct gpio_dt_spec btn1 = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback       btn1_cb_data;

static void btn1_pressed_cb(const struct device *dev,
                            struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    blink_enabled = !blink_enabled;
}

/* -----------------------------------------------------------------
 * Input callback → HID (matrix keys only, button lines removed)
 * -----------------------------------------------------------------*/
static void handle_key_event(struct input_event *evt, void *user_data)
{
    if (evt->type != INPUT_EV_KEY) {
        return;
    }
    const uint16_t code  = evt->code;   /* INPUT_KEY_x */
    const bool     press = evt->value;  /* 1 = down, 0 = up */

    /* Accept keys 1–4 only (external keypad). On‑board buttons handled via GPIO) */
    if (code < INPUT_KEY_1 || code > INPUT_KEY_4) {
        return;
    }

    const int16_t hid_usage   = input_to_hid_code(code);
    const uint8_t hid_mod_bit = input_to_hid_modifier(code);
    if (hid_usage < 0) {
        return;
    }

    /* Maintain pressed list */
    if (press) {
        if (pressed_count < 6) {
            pressed_key_usages[pressed_count++] = (uint8_t)hid_usage;
        }
    } else {
        for (size_t i = 0; i < pressed_count; ++i) {
            if (pressed_key_usages[i] == (uint8_t)hid_usage) {
                for (size_t j = i; j < pressed_count - 1; ++j) {
                    pressed_key_usages[j] = pressed_key_usages[j + 1];
                }
                pressed_count--;
                break;
            }
        }
    }

    /* Build 8‑byte report */
    key_report[0] = hid_mod_bit && press ? hid_mod_bit : 0x00;
    key_report[1] = 0x00; /* reserved */
    for (size_t i = 0; i < 6; ++i) {
        key_report[2 + i] = (i < pressed_count) ? pressed_key_usages[i] : 0x00;
    }

    hid_int_ep_write(device_get_binding("HID_0"), key_report, sizeof(key_report), NULL);
}
INPUT_CALLBACK_DEFINE(NULL, handle_key_event, NULL);

/* -----------------------------------------------------------------
 * main()
 * -----------------------------------------------------------------*/
void main(void)
{
    /* --- USB HID setup (unchanged) --- */
    const struct device *hid_dev = device_get_binding("HID_0");
    if (!hid_dev) {
        printk("HID device not found\n");
        return;
    }
    usb_hid_register_device(hid_dev, hid_report_desc, sizeof(hid_report_desc), NULL);
    if (usb_hid_init(hid_dev)) {
        printk("USB HID init failed\n");
        return;
    }
    if (usb_enable(NULL)) {
        printk("USB enable failed\n");
        return;
    }

    /* --- LEDs --- */
    if (gpio_is_ready_dt(&led0)) {
        gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    }
    if (gpio_is_ready_dt(&led1)) {
        gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    }

    /* --- Button SW1 (toggle) --- */
    if (gpio_is_ready_dt(&btn1)) {
        gpio_pin_configure_dt(&btn1, GPIO_INPUT | GPIO_PULL_UP);
        gpio_pin_interrupt_configure_dt(&btn1, GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&btn1_cb_data, btn1_pressed_cb, BIT(btn1.pin));
        gpio_add_callback(btn1.port, &btn1_cb_data);
    }

    printk("Keypad‑HID ready; LED blinker toggled by SW1.\n");

    /* --- Blink loop --- */
    while (true) {
        if (blink_enabled) {
            gpio_pin_set_dt(&led0, 1);
            gpio_pin_set_dt(&led1, 1);
            k_sleep(K_NSEC(2));

            gpio_pin_set_dt(&led0, 0);
            gpio_pin_set_dt(&led1, 0);
            k_sleep(K_NSEC(0));
        } else {
            /* Ensure LEDs remain off while disabled */
            gpio_pin_set_dt(&led0, 0);
            gpio_pin_set_dt(&led1, 0);
            k_sleep(K_MSEC(100));
        }
    }
}

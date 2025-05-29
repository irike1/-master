/*
 * Copyright (c) 2017 Linaro Limited  
 * Copyright (c) 2024 Jamie McCrae
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for controlling linear strips of LEDs.
 *
 * This library abstracts the chipset drivers for individually
 * addressable strips of LEDs.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_

/**
 * @brief LED Strip Interface
 * @defgroup led_strip_interface LED Strip Interface
 * @ingroup io_interfaces
 * @{
 */

#include <errno.h>
#include <zephyr/types.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Color value for a single RGB LED.
 *
 * Individual strip drivers may ignore lower-order bits if their
 * resolution in any channel is less than a full byte.
 */
struct led_rgb {
#ifdef CONFIG_LED_STRIP_RGB_SCRATCH
        uint8_t scratch;
#endif
        /** Red channel */
        uint8_t r;
        /** Green channel */
        uint8_t g;
        /** Blue channel */
        uint8_t b;
};

/**
 * @brief Color value for a single RGBWC LED (Red, Green, Blue, Warm white, Cool white).
 */
struct led_rgbwc {
        /** Red channel */
        uint8_t r;
        /** Green channel */
        uint8_t g;
        /** Blue channel */
        uint8_t b;
        /** Warm White channel */
        uint8_t warm;
        /** Cool White channel */
        uint8_t cool;
};

/**
 * @typedef led_api_update_rgb
 * @brief Callback API for updating an RGB LED strip.
 *
 * @see led_strip_update_rgb() for argument descriptions.
 */
typedef int (*led_api_update_rgb)(const struct device *dev,
                                  struct led_rgb *pixels,
                                  size_t num_pixels);

/**
 * @typedef led_api_update_channels
 * @brief Callback API for updating channels without an RGB interpretation.
 *
 * @see led_strip_update_channels() for argument descriptions.
 */
typedef int (*led_api_update_channels)(const struct device *dev,
                                       uint8_t *channels,
                                       size_t num_channels);

/**
 * @typedef led_api_length
 * @brief Callback API for getting the length of an LED strip.
 *
 * @see led_strip_length() for argument descriptions.
 */
typedef size_t (*led_api_length)(const struct device *dev);

/**
 * @typedef led_api_update_rgbwc
 * @brief Callback API for updating an RGBWC LED strip.
 *
 * @see led_strip_update_rgbwc() for argument descriptions.
 */
typedef int (*led_api_update_rgbwc)(const struct device *dev,
                                    struct led_rgbwc *pixels,
                                    size_t num_pixels);

/**
 * @brief LED strip driver API
 *
 * All LED strip drivers must implement this API.
 */
__subsystem struct led_strip_driver_api {
        led_api_update_rgb      update_rgb;
        led_api_update_channels update_channels;
        led_api_length          length;
        led_api_update_rgbwc    update_rgbwc;
};

/**
 * @brief Update an LED strip made of RGB pixels.
 *
 * @note This routine may overwrite @a pixels.
 *
 * Immediately updates the strip display according to the given RGB pixel array.
 *
 * @param dev         LED strip device
 * @param pixels      Array of RGB pixel data
 * @param num_pixels  Number of pixels in the array
 * @return 0 on success, negative error code on failure.
 */
static inline int led_strip_update_rgb(const struct device *dev,
                                       struct led_rgb *pixels,
                                       size_t num_pixels)
{
        const struct led_strip_driver_api *api =
                (const struct led_strip_driver_api *)dev->api;

        /* Validate pixel count if length callback is available */
        if (api->length != NULL) {
                if (api->length(dev) < num_pixels) {
                        return -ERANGE;
                }
        }

        return api->update_rgb(dev, pixels, num_pixels);
}

/**
 * @brief Update an LED strip on a per-channel basis.
 *
 * @note This routine may overwrite @a channels.
 *
 * Immediately updates the strip display according to the given channel array. Each 
 * byte in the array corresponds to an LED color channel, and channels are sent 
 * linearly in strip order.
 *
 * @param dev          LED strip device
 * @param channels     Array of channel data
 * @param num_channels Length of the channels array
 * @return 0 on success, -ENOSYS if not implemented, or negative error code.
 */
static inline int led_strip_update_channels(const struct device *dev,
                                            uint8_t *channels,
                                            size_t num_channels)
{
        const struct led_strip_driver_api *api =
                (const struct led_strip_driver_api *)dev->api;

        if (api->update_channels == NULL) {
                return -ENOSYS;
        }

        return api->update_channels(dev, channels, num_channels);
}

/**
 * @brief Update an LED strip made of RGBWC pixels (with warm & cool white).
 *
 * @note This routine may overwrite @a pixels.
 *
 * Immediately updates the strip display according to the given RGBWC pixel array.
 * Each pixel contains red, green, blue, warm-white, and cool-white brightness values.
 *
 * @param dev         LED strip device
 * @param pixels      Array of RGBWC pixel data
 * @param num_pixels  Number of pixels in the array
 * @return 0 on success, -ENOSYS if not supported, or negative error code.
 */
static inline int led_strip_update_rgbwc(const struct device *dev,
                                         struct led_rgbwc *pixels,
                                         size_t num_pixels)
{
        const struct led_strip_driver_api *api =
                (const struct led_strip_driver_api *)dev->api;

        if (api->length != NULL) {
                if (api->length(dev) < num_pixels) {
                        return -ERANGE;
                }
        }

        if (api->update_rgbwc == NULL) {
                return -ENOSYS;
        }

        return api->update_rgbwc(dev, pixels, num_pixels);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif  /* ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_ */

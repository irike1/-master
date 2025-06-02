#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_

#include <errno.h>
#include <zephyr/types.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif




struct led_rgb {
#ifdef CONFIG_LED_STRIP_RGB_SCRATCH
	/*
	 * Pad/scratch space needed by some drivers. Users should
	 * ignore.
	 */
	uint8_t scratch;
#endif
	/** Red channel */
	uint8_t r;
	/** Green channel */
	uint8_t g;
	/** Blue channel */
	uint8_t b;
	uint8_t w;
	uint8_t c;
};


typedef int (*led_api_update_rgb)(const struct device *dev,
				  struct led_rgb *pixels,
				  size_t num_pixels);




typedef int (*led_api_update_channels)(const struct device *dev,
				       uint8_t *channels,
				       size_t num_channels);


typedef size_t (*led_api_length)(const struct device *dev);

__subsystem struct led_strip_driver_api {
	led_api_update_rgb update_rgb;
	led_api_update_channels update_channels;
	led_api_length length;
};


static inline int led_strip_update_rgb(const struct device *dev,
				       struct led_rgb *pixels,
				       size_t num_pixels)
{
	const struct led_strip_driver_api *api =
		(const struct led_strip_driver_api *)dev->api;

	/* Allow for out-of-tree drivers that do not have this function for 2 Zephyr releases
	 * until making it mandatory, function added after Zephyr 3.6
	 */
	if (api->length != NULL) {
		/* Ensure supplied pixel size is valid for this device */
		if (api->length(dev) < num_pixels) {
			return -ERANGE;
		}
	}

	return api->update_rgb(dev, pixels, num_pixels);
}



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
 * @brief	Mandatory function to get chain length (in pixels) of an LED strip device.
 *
 * @param dev	LED strip device.
 *
 * @retval	Length of LED strip device.
 */
static inline size_t led_strip_length(const struct device *dev)
{
	const struct led_strip_driver_api *api =
		(const struct led_strip_driver_api *)dev->api;

	return api->length(dev);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif	/* ZEPHYR_INCLUDE_DRIVERS_LED_STRIP_H_ */

/*
 * Local WS2812 I2S LED‑strip driver – forked from Zephyr 3.6 “drivers/led_strip/ws2812_i2s.c”.
 *
 * Changes versus upstream (to decouple from Zephyr’s LED_STRIP subsystem):
 *   • Replaces <zephyr/drivers/led_strip.h> with project‑local "led_strip.h".
 *   • Removes direct reliance on Kconfig symbols CONFIG_LED_STRIP_*, providing
 *     inline fall‑backs so the code builds even when Zephyr’s LED_STRIP module
 *     is disabled.
 *   • Keeps identical runtime behaviour (API, timings, DT compat string) so the
 *     application code and devicetree overlay remain unchanged.
 *
 * SPDX‑License‑Identifier: Apache‑2.0
 */

#define DT_DRV_COMPAT worldsemi_ws2812_i2s

#include <string.h>

/* Local copy of the public LED‑strip API */
#include "led_strip.h"

/* ------------------------------------------------------------------------- */
/* Logging                                                                   */
/* ------------------------------------------------------------------------- */
/* Provide a default debug level if Zephyr’s CONFIG_LED_STRIP_LOG_LEVEL is
 * absent (because LED_STRIP Kconfig was disabled). */
#ifndef CONFIG_LED_STRIP_LOG_LEVEL
#define CONFIG_LED_STRIP_LOG_LEVEL LOG_LEVEL_DBG
#endif

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ws2812_i2s);

/* ------------------------------------------------------------------------- */
/* Zephyr dependencies (device, I2S, DT, kernel)                             */
/* ------------------------------------------------------------------------- */
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include "led.h" /* local copy of dt‑bindings/led/led.h for color IDs */
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define WS2812_I2S_PRE_DELAY_WORDS 1

struct ws2812_i2s_cfg {
	const struct device *dev;
	size_t tx_buf_bytes;
	struct k_mem_slab *mem_slab;
	uint8_t num_colors;
	size_t length;
	const uint8_t *color_mapping;
	uint16_t reset_words;
	uint32_t lrck_period;
	uint32_t extra_wait_time_us;
	bool active_low;
	uint8_t nibble_one;
	uint8_t nibble_zero;
};

/* ------------------------------------------------------------------------- */
/* Helper: serialise one 8‑bit colour into two 16‑bit I2S samples             */
/* ------------------------------------------------------------------------- */
static inline void ws2812_i2s_ser(uint32_t *word, uint8_t colour,
                                  uint8_t sym_one, uint8_t sym_zero)
{
	*word = 0;
	for (uint8_t i = 0; i < 8; i++) {
		*word |= ((colour >> i) & 1 ? sym_one : sym_zero) << (i * 4);
	}
	/* Swap due to stereo channel ordering */
	*word = (*word >> 16) | (*word << 16);
}

/* ------------------------------------------------------------------------- */
/* LED‑strip API: update RGB data                                            */
/* ------------------------------------------------------------------------- */
static int ws2812_strip_update_rgb(const struct device *dev,
                                   struct led_rgb *pixels,
                                   size_t num_pixels)
{
	const struct ws2812_i2s_cfg *cfg = dev->config;
	uint8_t sym_one, sym_zero;
	uint32_t reset_word;
	uint32_t *tx_buf;
	void *mem_block;
	int ret;

	if (cfg->active_low) {
		sym_one   = (~cfg->nibble_one)  & 0x0F;
		sym_zero  = (~cfg->nibble_zero) & 0x0F;
		reset_word = 0xFFFFFFFF;
	} else {
		sym_one   = cfg->nibble_one & 0x0F;
		sym_zero  = cfg->nibble_zero & 0x0F;
		reset_word = 0;
	}

	/* Acquire TX buffer */
	ret = k_mem_slab_alloc(cfg->mem_slab, &mem_block, K_SECONDS(10));
	if (ret < 0) {
		LOG_ERR("TX slab alloc failed (%d)", ret);
		return -ENOMEM;
	}
	tx_buf = (uint32_t *)mem_block;

	/* Pre‑reset */
	for (uint8_t i = 0; i < WS2812_I2S_PRE_DELAY_WORDS; i++) {
		*tx_buf++ = reset_word;
	}

	/* Pixel data, honour colour mapping */
	for (size_t i = 0; i < num_pixels; i++) {
		for (uint8_t j = 0; j < cfg->num_colors; j++) {
			uint8_t p = 0;
			switch (cfg->color_mapping[j]) {
			case LED_COLOR_ID_RED:   p = pixels[i].r; break;
			case LED_COLOR_ID_GREEN: p = pixels[i].g; break;
			case LED_COLOR_ID_BLUE:  p = pixels[i].b; break;
			case LED_COLOR_ID_WHITE: /* API has no white – send 0 */ break;
			default: k_mem_slab_free(cfg->mem_slab, mem_block); return -EINVAL;
			}
			ws2812_i2s_ser(tx_buf++, p, sym_one, sym_zero);
		}
	}

	/* Post‑reset */
	for (uint16_t i = 0; i < cfg->reset_words; i++) {
		*tx_buf++ = reset_word;
	}

	/* Push via I2S */
	ret = i2s_write(cfg->dev, mem_block, cfg->tx_buf_bytes);
	if (ret < 0) {
		k_mem_slab_free(cfg->mem_slab, mem_block);
		LOG_ERR("i2s_write failed (%d)", ret);
		return ret;
	}

	ret = i2s_trigger(cfg->dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret < 0) { LOG_ERR("I2S START trig failed (%d)", ret); return ret; }

	ret = i2s_trigger(cfg->dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	if (ret < 0) { LOG_ERR("I2S DRAIN trig failed (%d)", ret); return ret; }

	/* Wait for transmission */
	uint32_t flush_us = cfg->lrck_period * (cfg->tx_buf_bytes / sizeof(uint32_t));
	k_usleep(flush_us + cfg->extra_wait_time_us);

	return 0;
}

static size_t ws2812_strip_length(const struct device *dev)
{
	return ((const struct ws2812_i2s_cfg *)dev->config)->length;
}

/* ------------------------------------------------------------------------- */
/* Driver initialisation                                                     */
/* ------------------------------------------------------------------------- */
static int ws2812_i2s_init(const struct device *dev)
{
	const struct ws2812_i2s_cfg *cfg = dev->config;
	struct i2s_config i2s_cfg = {0};

	uint32_t lrck_hz = USEC_PER_SEC / cfg->lrck_period;
	LOG_DBG("WS2812 I2S LRCK %u Hz (%u us)", lrck_hz, cfg->lrck_period);

	i2s_cfg.word_size      = 16;
	i2s_cfg.channels       = 2;
	i2s_cfg.format         = I2S_FMT_DATA_FORMAT_I2S;
	i2s_cfg.options        = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
	i2s_cfg.frame_clk_freq = lrck_hz;
	i2s_cfg.mem_slab       = cfg->mem_slab;
	i2s_cfg.block_size     = cfg->tx_buf_bytes;
	i2s_cfg.timeout        = 1000;

	int ret = i2s_configure(cfg->dev, I2S_DIR_TX, &i2s_cfg);
	if (ret < 0) { LOG_ERR("I2S configure failed (%d)", ret); return ret; }

	/* Validate colour mapping */
	for (uint8_t i = 0; i < cfg->num_colors; i++) {
		switch (cfg->color_mapping[i]) {
		case LED_COLOR_ID_RED:
		case LED_COLOR_ID_GREEN:
		case LED_COLOR_ID_BLUE:
		case LED_COLOR_ID_WHITE:
			break;
		default:
			LOG_ERR("%s: invalid colour mapping", dev->name);
			return -EINVAL;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* LED‑strip driver API table                                                */
/* ------------------------------------------------------------------------- */
static const struct led_strip_driver_api ws2812_i2s_api = {
	.update_rgb = ws2812_strip_update_rgb,
	.length     = ws2812_strip_length,
};

/* ------------------------------------------------------------------------- */
/* Devicetree instantiation                                                  */
/* ------------------------------------------------------------------------- */
#ifndef CONFIG_LED_STRIP_INIT_PRIORITY
#define CONFIG_LED_STRIP_INIT_PRIORITY 90
#endif

/* Helpers */
#ifndef DIV_ROUND_UP           /* avoid clash with <zephyr/sys/util.h> */
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
#define RESET_WORDS(i)     DIV_ROUND_UP(DT_INST_PROP(i, reset_delay), DT_INST_PROP(i, lrck_period))
#define NUM_PIXELS(i)      DT_INST_PROP(i, chain_length)
#define NUM_COLORS(i)      DT_INST_PROP_LEN(i, color_mapping)
#define BUFSIZE(i)                                                                    \
	(((NUM_COLORS(i) * NUM_PIXELS(i)) + WS2812_I2S_PRE_DELAY_WORDS + RESET_WORDS(i)) * 4)

#define WS2812_I2S_DEVICE(i)                                                            \
	K_MEM_SLAB_DEFINE_STATIC(ws2812_slab_##i, BUFSIZE(i), 2, 4);                      \
	static const uint8_t color_map_##i[] = DT_INST_PROP(i, color_mapping);            \
	static const struct ws2812_i2s_cfg cfg_##i = {                                    \
		.dev                = DEVICE_DT_GET(DT_INST_PHANDLE(i, i2s_dev)),             \
		.tx_buf_bytes       = BUFSIZE(i),                                           \
		.mem_slab           = &ws2812_slab_##i,                                     \
		.num_colors         = NUM_COLORS(i),                                        \
		.length             = NUM_PIXELS(i),                                        \
		.color_mapping      = color_map_##i,                                        \
		.lrck_period        = DT_INST_PROP(i, lrck_period),                          \
		.extra_wait_time_us = DT_INST_PROP(i, extra_wait_time),                      \
		.reset_words        = RESET_WORDS(i),                                       \
		.active_low         = DT_INST_PROP(i, out_active_low),                       \
		.nibble_one         = DT_INST_PROP(i, nibble_one),                           \
		.nibble_zero        = DT_INST_PROP(i, nibble_zero),                          \
	};                                                                               \
	DEVICE_DT_INST_DEFINE(i, ws2812_i2s_init, NULL, NULL, &cfg_##i, POST_KERNEL,      \
				  CONFIG_LED_STRIP_INIT_PRIORITY, &ws2812_i2s_api);

DT_INST_FOREACH_STATUS_OKAY(WS2812_I2S_DEVICE)

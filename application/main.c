/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>

#include <blink.h>

#ifdef CONFIG_MYFUNCTION
#include "my_function.h"
#endif

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(main);

#define SLEEP_TIME_MS 500

#define UART_RECEIVE_TIMEOUT 	100
#define UART_RECEIVE_BUFF_SIZE 	10

#define BLINK_PERIOD_MS_MAX  1000U

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define SW0_NODE DT_ALIAS(sw0)

//------------------------------------------------------------------------------

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

static struct gpio_callback pin_cb_data;

// static const struct device *bme280_dev = DEVICE_DT_GET(DT_NODELABEL(bme280));
// static const struct device *blink_dev = DEVICE_DT_GET(DT_NODELABEL(blink_led));

//------------------------------------------------------------------------------

void pin_isr(const struct device *dev, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	gpio_pin_toggle_dt(&led1);
	LOG_WRN("Button pressed");
}

//------------------------------------------------------------------------------

#ifdef CONFIG_WIFI_CREDENTIALS_STATIC 
/* STEP 8 - Define the function to populate the Wi-Fi credential parameters */
static int wifi_args_to_params(struct wifi_connect_req_params *params)
{

	/* STEP 8.1 - Populate the SSID and password */


	/* STEP 8.2 - Populate the rest of the relevant members */

	return 0;
}
#endif //CONFIG_WIFI_CREDENTIALS_STATIC

//------------------------------------------------------------------------------

int main(void)
{
	LOG_INF("Main started");

	int ret = 0;

	if (!device_is_ready(led0.port))
		return -1;

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
		return ret;

	if (!device_is_ready(led1.port))
		return -1;

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
		return ret;

	if (!device_is_ready(sw0.port))
		return -1;

	ret = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
	if (ret < 0)
		return ret;

	ret = gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0)
		return ret;

	gpio_init_callback(&pin_cb_data, pin_isr, BIT(sw0.pin));

	ret = gpio_add_callback(sw0.port, &pin_cb_data);
	if (ret < 0)
		return ret;

	// ret = device_is_ready(blink_dev);
	// if (!ret)
	// 	return ret;

	// ret = blink_off(blink_dev);
	// if (ret < 0)
	// 	return ret;

	// ret = device_is_ready(bme280_dev);
	// if (!ret)
	// 	return ret;

	// LOG_INF("Setting LED period to %u ms\n", BLINK_PERIOD_MS_MAX);
	// blink_set_period_ms(blink_dev, BLINK_PERIOD_MS_MAX);

	LOG_INF("Initializing Wi-Fi driver");
	/* Sleep to allow initialization of Wi-Fi driver */
	k_sleep(K_SECONDS(1));

	/* STEP 7 - Initialize and add the callback function for network events */


	#ifdef CONFIG_WIFI_CREDENTIALS_STATIC 
	/* STEP 9.1 - Declare the variable for the network configuration parameters */


	/* STEP 9.2 - Get the network interface */


	/* STEP 10 - Populate cnx_params with the network configuration */


	/* STEP 11 - Call net_mgmt() to request the Wi-Fi connection */

	#endif //CONFIG_WIFI_CREDENTIALS_STATIC

	// k_sem_take(&run_app, K_FOREVER);

	while (1)
	{
		ret = gpio_pin_toggle_dt(&led0);

		if (ret < 0)
			return -1;

		k_msleep(SLEEP_TIME_MS);

#ifdef CONFIG_MYFUNCTION
		int a = 3, b = 4;
		printk("The sum of %d and %d is %d\n", a, b, sum(a, b));
#else
		printk("MYFUNCTION not enabled\n");
#endif

#if 0
		struct sensor_value temp_val;
		struct sensor_value press_val;
		struct sensor_value hum_val;

		ret = sensor_sample_fetch(bme280_dev);
		if (ret < 0)
		{
			LOG_ERR("Could not fetch sample (%d)", ret);
			return 0;
		}

		if (sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_val))
		{
			LOG_ERR("Could not get sample");
			return 0;
		}

		if (sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS, &press_val))
		{
			LOG_ERR("Could not get sample");
			return 0;
		}

		if (sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY, &hum_val))
		{
			LOG_ERR("Could not get sample");
			return 0;
		}

		LOG_INF("Compensated temperature value: %d", temp_val.val1);
		LOG_INF("Compensated pressure value: %d", press_val.val1);
		LOG_INF("Compensated humidity value: %d", hum_val.val1);
#endif
	}
}

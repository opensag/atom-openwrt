/*
 *  Nexsec DTA116X board file
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/dmi.h>

#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>


#define LED_DEVNAME		"leds-gpio"
#define BOARD_NAME_DTA1160	"DTA1160"
#define BOARD_NAME_DTA1161AC4	"DTA1161AC4"
#define BOARD_NAME_DTA1161BC8	"DTA1161BC8"
#define SYS_LED_NAME 		"SYS"
#define CLOUD_LED_NAME		"CLOUD"
#define LTE0_LED_NAME		"Lte_0"
#define LTE1_LED_NAME		"Lte_1"
#define LTE2_LED_NAME		"Lte_2"
#define DTA1161_SYS_LED_GPIO	129
#define DTA1161_CLOUD_LED_GPIO	130
#define DTA1161_LTE0_LED_GPIO	55
#define DTA1161_LTE1_LED_GPIO	54
#define DTA1161_LTE2_LED_GPIO	62
#define DTA1160_SYS_LED_GPIO	196
#define DTA1160_CLOUD_LED_GPIO	197
#define DTA1161_RSTBTN_GPIO	131


static  struct gpio_led dta1161_leds[] = { 
	{SYS_LED_NAME, "default-off", DTA1161_SYS_LED_GPIO, 0, 0, 0},
	{CLOUD_LED_NAME, "default-off", DTA1161_CLOUD_LED_GPIO, 0, 0, 0},
	{LTE0_LED_NAME, "default-off", DTA1161_LTE0_LED_GPIO, 0, 0, 0},
	{LTE1_LED_NAME, "default-off", DTA1161_LTE1_LED_GPIO, 0, 0, 0},
	{LTE2_LED_NAME, "default-off", DTA1161_LTE2_LED_GPIO, 0, 0, 0},
}; 

static  struct gpio_led dta1160_leds[]  = {   
	{SYS_LED_NAME, "default-off", DTA1160_SYS_LED_GPIO, 0, 0, 0},
	{CLOUD_LED_NAME, "default-off", DTA1160_CLOUD_LED_GPIO, 0, 0, 0},
}; 

static  struct gpio_led_platform_data dta1161_leds_pdata = {   
	.num_leds = ARRAY_SIZE(dta1161_leds),
	.leds = dta1161_leds,
};

static struct gpio_led_platform_data dta1160_leds_pdata = {
	.num_leds = ARRAY_SIZE(dta1160_leds),
	.leds = dta1160_leds,
};

static struct platform_device dta1161_leds_pdev = {
	.name = LED_DEVNAME,
		.id = -1,
		.dev = { 
			.platform_data = &dta1161_leds_pdata,
		},
}; 

static struct platform_device dta1160_leds_pdev = {
	.name = LED_DEVNAME,
		.id = -1,
		.dev = { 
			.platform_data = &dta1160_leds_pdata,
		},
}; 

static struct gpio_keys_button dta1161_gpio_keys_table[] = {
	{
		.code		= KEY_RESTART,
		.gpio		= DTA1161_RSTBTN_GPIO,
		.debounce_interval = 20,
		.active_low	= 1,
		.desc		="Reset Button",
	},
};

static struct gpio_keys_platform_data dta1161_gpio_keys_info = {
	.buttons	= dta1161_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(dta1161_gpio_keys_table),
	.poll_interval	= 50, /* default to 50ms */
};

static struct platform_device dta1161_keys_pdev = {
	.name		= "gpio-keys-polled",
	.dev		= {
		.platform_data	= &dta1161_gpio_keys_info,
	},
};

static struct platform_device *dta1160_devices[] __initdata = {
	&dta1160_leds_pdev,		
};

static struct platform_device *dta1161_devices[] __initdata = {
	&dta1161_leds_pdev,	
	&dta1161_keys_pdev,
};

static int __init dta116x_devices_init(void)
{ 

	const char *board_name = dmi_get_system_info(DMI_PRODUCT_NAME);

	if(!strcmp(board_name,BOARD_NAME_DTA1161AC4) || !strcmp(board_name,BOARD_NAME_DTA1161BC8)){
		platform_add_devices(dta1161_devices,
				    ARRAY_SIZE(dta1161_devices));
	}
	else if (!strcmp(board_name,BOARD_NAME_DTA1160)){		
		platform_add_devices(dta1160_devices,
				    ARRAY_SIZE(dta1160_devices));
	}
	else
		pr_info("Add devices err: no DTA116X board info");
	
	return 0;	
}

device_initcall(dta116x_devices_init);
MODULE_DESCRIPTION("For NEXSEC DTA116X ");
MODULE_LICENSE("GPL");

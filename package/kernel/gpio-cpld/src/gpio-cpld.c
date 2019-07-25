/*
 * GPIO driver for Nexsec DTA1160 CPLD *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/version.h>
#include <linux/dmi.h>
#include <linux/string.h>

#define DRVNAME "gpio-cpld"

#define CPLD_MAX_GPIO 3
#define CPLD_LPC_BASE_ADDR 0xF4
#define CPLD_GPIO_HIGH 2
#define CPLD_GPIO_LOW 1
#define CPLD_GPIO_BASE 196
#define PRODUCT_NAME   "DTA1160"

struct cpld_gpio_chip {
	struct gpio_chip chip;
	unsigned int regbase;	
};

static int cpld_gpio_direction_in(struct gpio_chip *chip, unsigned offset);
static int cpld_gpio_get(struct gpio_chip *chip, unsigned offset);
static int cpld_gpio_get_direction(struct gpio_chip *chip, unsigned offset);
static int cpld_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value);
static void cpld_gpio_set(struct gpio_chip *chip, unsigned offset, int value);


static const struct gpio_chip cpld_gpio_chips = {
	.label            = DRVNAME,			
	.owner            = THIS_MODULE,		
	.get_direction	  = cpld_gpio_get_direction,      
	.direction_input  = cpld_gpio_direction_in,
	.get              = cpld_gpio_get,		
	.direction_output = cpld_gpio_direction_out,	
	.set              = cpld_gpio_set,		
	.base             = CPLD_GPIO_BASE,			
	.ngpio            = CPLD_MAX_GPIO,			
	.can_sleep        = 1,			
};


static inline int cpld_reg_get(int reg)
{
	unsigned int reg_value;
	reg_value=inb(reg);		
	return reg_value;
}

static inline void cpld_reg_set(int reg, int val)
{	
	outb(val,reg); 
} 


static int cpld_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{    
	return 0;
}
static int cpld_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
    
	return 0;
}
static int cpld_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{   
	return 0;
}

static int cpld_gpio_get(struct gpio_chip *chip, unsigned offset)
{	
	struct cpld_gpio_chip *cg_chip = gpiochip_get_data(chip);	
	int reg_addr;
	u8 data;
	reg_addr = cg_chip->regbase + offset * 2; 
	data = cpld_reg_get(reg_addr);
	if (data == CPLD_GPIO_HIGH)		
		return 1;	
	else 
		return 0;
}

static void cpld_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct cpld_gpio_chip *cg_chip = gpiochip_get_data(chip);	
	int reg_addr;
	int data;
	
	reg_addr = cg_chip->regbase +  offset * 2; 	
    if (value==1)
		data = CPLD_GPIO_HIGH;
	else 
		data = CPLD_GPIO_LOW;
	cpld_reg_set(reg_addr, data);	
}

/*
 * Platform device and driver.
 */
static int cpld_gpio_set_default(void)
{
    int i;
	for(i=0 ; i<CPLD_MAX_GPIO; i++)
		cpld_reg_set(CPLD_LPC_BASE_ADDR + i * 2 ,CPLD_GPIO_LOW);
	
	return 0;	
}
static int cpld_gpio_probe(struct platform_device *pdev)
{		
	
	struct cpld_gpio_chip *cg_chips;
	
	cg_chips = devm_kzalloc(&pdev->dev, sizeof(*cg_chips), GFP_KERNEL);
	if (!cg_chips)
		return -ENOMEM;			
	cg_chips->chip = cpld_gpio_chips;
	cg_chips->regbase = CPLD_LPC_BASE_ADDR;
	cg_chips->chip.parent = &pdev->dev;
	
	platform_set_drvdata(pdev, cg_chips); 

	devm_gpiochip_add_data(&pdev->dev, &cg_chips->chip,
				      cg_chips);

	cpld_gpio_set_default();

	return 0;

}

static int cpld_gpio_remove(struct platform_device *pdev)
{
	
	struct cpld_gpio_chip *cg_chips = platform_get_drvdata(pdev);
	
	gpiochip_remove (&cg_chips->chip);	

	return 0;
}

static struct platform_device *cpld_gpio_pdev;

static struct platform_driver cpld_gpio_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= cpld_gpio_probe,
	.remove		= cpld_gpio_remove,
};

static int __init cpld_gpio_init(void)
{
	int err;	
	
	const char *board_name = dmi_get_system_info(DMI_PRODUCT_NAME);
        if( !strcmp( board_name , PRODUCT_NAME )){         
		cpld_gpio_pdev = platform_device_alloc(DRVNAME, -1);			
		if (!cpld_gpio_pdev){
			pr_err(DRVNAME ": Error platform_device_alloc\n");
			return -ENOMEM;	
		}
	
		err = platform_device_add(cpld_gpio_pdev);
		if (err) {
			pr_err(DRVNAME "Device addition failed\n");
			platform_device_put(cpld_gpio_pdev);
			return err;
		}
		pr_info(DRVNAME ": Device added\n");
	
		err = platform_driver_register(&cpld_gpio_driver);
		if (err){
		platform_driver_unregister(&cpld_gpio_driver);
	    	return err;
		}

		return err;
	}
	else{
		pr_info(DRVNAME ": gpio-cpld driver is only used on DTA1160\n");
		return 0;
	}
}


static void __exit cpld_gpio_exit(void)
{
	platform_device_unregister(cpld_gpio_pdev);
	platform_driver_unregister(&cpld_gpio_driver);
}

subsys_initcall(cpld_gpio_init);
module_exit(cpld_gpio_exit);

MODULE_DESCRIPTION("GPIO driver for Nexsec DTA1160 CPLD");
MODULE_LICENSE("GPL");

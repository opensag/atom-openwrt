/*
 * GPIO driver for Atom C3000 
 * 
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

#define DRVNAME "gpio-dnv"

#define DNV_SBREG_BAR 		0xFD000000
#define DNV_SBPORT_C2_BASE 	0xC20000
#define DNV_SBPORT_C5_BASE 	0xC50000
#define DNV_GPIO0_OFFSET 	0x4d8
#define DNV_GPIO4_OFFSET 	0x568
#define DNV_GPIO9_OFFSET	0x5d0
#define DNV_RSTBTN_OFFSET	0x670
#define DNV_GPIO_TX_MASK 	0x1
#define DNV_GPIO_RX_MASK 	0x2
#define DNV_GPIO_TX_DIS  	0x100
#define DNV_GPIO_RX_DIS 	0x200
#define DNV_PMODE_BITS 		0x1C00
#define DNV_MAX_GPIO		4
#define DNV_GPIO_BASE       	128
#define BOARD_NAME_DTA1161AC4	"DTA1161AC4"
#define BOARD_NAME_DTA1161BC8	"DTA1161BC8"

struct dnv_gpio_chip {
	struct gpio_chip chip;
	raw_spinlock_t lock;
	void __iomem *regbase_c2;
	void __iomem *regbase_c5;
};

static int dnv_gpio_direction_in(struct gpio_chip *chip, unsigned offset);
static int dnv_gpio_get(struct gpio_chip *chip, unsigned offset);
static int dnv_gpio_get_direction(struct gpio_chip *chip, unsigned offset);
static int dnv_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value);
static void dnv_gpio_set(struct gpio_chip *chip, unsigned offset, int value);


static const struct gpio_chip dnv_gpio_chips = {
	.label            = DRVNAME,			
	.owner            = THIS_MODULE,		
	.get_direction	  = dnv_gpio_get_direction,      
	.direction_input  = dnv_gpio_direction_in,
	.get              = dnv_gpio_get,		
	.direction_output = dnv_gpio_direction_out,	
	.set              = dnv_gpio_set,		
	.base             = DNV_GPIO_BASE,			
	.ngpio            = DNV_MAX_GPIO,			
	.can_sleep        = 1,			
};

static int dnv_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}
static int dnv_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}
static int dnv_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	return 0;
}

static int dnv_gpio_get(struct gpio_chip *chip, unsigned offset)
{	
	struct dnv_gpio_chip *cg_chip = gpiochip_get_data(chip);		
	u32 data;	
	unsigned long flags;	
	void __iomem *reg;
		
	switch (offset){
	case 0:
		reg = cg_chip->regbase_c2 + DNV_GPIO0_OFFSET;
		break;
	case 1:
		reg = cg_chip->regbase_c5 + DNV_GPIO4_OFFSET;
		break;
	case 2:
		reg = cg_chip->regbase_c5 + DNV_GPIO9_OFFSET;
		break;
	case 3:
		reg = cg_chip->regbase_c5 + DNV_RSTBTN_OFFSET;
		break;
	default:
		break;
	}
	
	raw_spin_lock_irqsave(&cg_chip->lock, flags);
	data = readl(reg);	
	raw_spin_unlock_irqrestore(&cg_chip->lock, flags);
    if(!(data&DNV_GPIO_RX_DIS))
		return !!(data & DNV_GPIO_RX_MASK);
	else
		return !!(data & DNV_GPIO_TX_MASK);
}



static void dnv_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{	
	struct dnv_gpio_chip *cg_chip = gpiochip_get_data(chip);	
	u32 data;	
	unsigned long flags;		
	void __iomem *reg;
	
	switch (offset){
	case 0:
		reg = cg_chip->regbase_c2 + DNV_GPIO0_OFFSET;
		break;
	case 1:
		reg = cg_chip->regbase_c5 + DNV_GPIO4_OFFSET;
		break;
	case 2:
		reg = cg_chip->regbase_c5 + DNV_GPIO9_OFFSET;
		break;
	case 3:
		reg = cg_chip->regbase_c5 + DNV_RSTBTN_OFFSET;
		break;
	default:
		break;
	}
	
	raw_spin_lock_irqsave(&cg_chip->lock, flags);
	data = readl(reg);	
	switch (offset){
	case 0:
	case 1:
	case 3:
		if (value)
			writel(data | DNV_GPIO_TX_MASK, reg);
		else
			writel(data & ~DNV_GPIO_TX_MASK, reg);
		break;
	case 2:
		if (value)
			writel(data | DNV_GPIO_TX_MASK, reg);
		else
			writel(data & ~DNV_GPIO_TX_MASK, reg);
		break;
	default:
		break;
	}		
	raw_spin_unlock_irqrestore(&cg_chip->lock, flags);	
	
}

static int dnv_gpio_set_default(struct dnv_gpio_chip *chips)
{
    
	u32 data;	
	unsigned long flags;		
	void __iomem *reg;	
	
	raw_spin_lock_irqsave(&chips->lock, flags);
	
	reg = chips->regbase_c2 + DNV_GPIO0_OFFSET;	
	data = readl(reg);
	data &= ~(DNV_GPIO_TX_DIS | DNV_GPIO_TX_MASK);
	data |= DNV_GPIO_RX_DIS;
	writel(data , reg);
	
	reg = chips->regbase_c5 + DNV_GPIO4_OFFSET;
	data = readl(reg);
	data &= ~(DNV_GPIO_TX_DIS | DNV_GPIO_TX_MASK);
	data |= DNV_GPIO_RX_DIS;
	writel(data , reg);
	
	reg = chips->regbase_c5 + DNV_GPIO9_OFFSET;
	data = readl(reg);
	data &= ~DNV_GPIO_TX_DIS;
	data |= DNV_GPIO_RX_DIS | DNV_GPIO_TX_MASK;
	writel(data , reg);

	reg = chips->regbase_c5 + DNV_RSTBTN_OFFSET;
	data = readl(reg);
	data &= ~(DNV_GPIO_RX_DIS | DNV_PMODE_BITS);
	data |= DNV_GPIO_TX_DIS ;
	writel(data , reg);
	
	raw_spin_unlock_irqrestore(&chips->lock, flags);
		
}

/*
 * Platform device and driver.
 */

static int dnv_gpio_probe(struct platform_device *pdev)
{		
	
	struct dnv_gpio_chip *cg_chips;
	
	cg_chips = devm_kzalloc(&pdev->dev, sizeof(*cg_chips), GFP_KERNEL);
	if (!cg_chips)
		return -ENOMEM;		 
	
	cg_chips->chip = dnv_gpio_chips; 
	cg_chips->regbase_c2= ioremap(DNV_SBREG_BAR + DNV_SBPORT_C2_BASE, 0x1000);
	cg_chips->regbase_c5= ioremap(DNV_SBREG_BAR + DNV_SBPORT_C5_BASE, 0x1000);
	cg_chips->chip.parent = &pdev->dev;
	
	raw_spin_lock_init(&cg_chips->lock);
	
	platform_set_drvdata(pdev, cg_chips); 
  
	devm_gpiochip_add_data(&pdev->dev, &cg_chips->chip, cg_chips);

	dnv_gpio_set_default(cg_chips);

	return 0;

}

static int dnv_gpio_remove(struct platform_device *pdev)
{
	
	struct dnv_gpio_chip *cg_chips = platform_get_drvdata(pdev);
	iounmap(cg_chips->regbase_c2);
	iounmap(cg_chips->regbase_c5);

	return 0;
}

static struct platform_device *dnv_gpio_pdev;

static struct platform_driver dnv_gpio_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= dnv_gpio_probe,
	.remove		= dnv_gpio_remove,
};

static int __init dnv_gpio_init(void)
{
	int err;	
 	const char *board_name = dmi_get_system_info(DMI_PRODUCT_NAME);
        
    if(!strcmp(board_name, BOARD_NAME_DTA1161AC4) || !strcmp(board_name, BOARD_NAME_DTA1161BC8)) {
                	
		dnv_gpio_pdev = platform_device_alloc(DRVNAME, -1);			
		if (!dnv_gpio_pdev){
			pr_err(DRVNAME ": Error platform_device_alloc\n");
			return -ENOMEM;	
		}
	
		err = platform_device_add(dnv_gpio_pdev);
		if (err) {
			pr_err(DRVNAME "Device addition failed\n");
			platform_device_put(dnv_gpio_pdev);
			return err;
		}
	
		err = platform_driver_register(&dnv_gpio_driver);
		if (err){
			platform_driver_unregister(&dnv_gpio_driver);
	    	        return err;
		}
	}
     else{
          pr_info(DRVNAME "Board Name :%s :\n", board_name);
    }
	return 0;

}

subsys_initcall(dnv_gpio_init);

static void __exit dnv_gpio_exit(void)
{    
	platform_device_unregister(dnv_gpio_pdev);
	platform_driver_unregister(&dnv_gpio_driver);
}
module_exit(dnv_gpio_exit);
MODULE_DESCRIPTION("GPIO driver for Atom C3000");
MODULE_LICENSE("GPL");


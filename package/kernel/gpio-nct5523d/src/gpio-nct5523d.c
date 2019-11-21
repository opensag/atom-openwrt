/*
 * GPIO driver for NCT5523D
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

#define DRVNAME "gpio-nct5523d"

/*
 * Super-I/O registers
 */
#define SIO_LDSEL		0x07	/* Logical device select */
#define SIO_CHIPID		0x20	/* Chaip ID (2 bytes) */
#define SIO_GPIO_ENABLE	0x30	/* GPIO enable */

#define SIO_GPIO5_BIT    0x20
#define SIO_GPIO6_BIT    0x40
#define SIO_GPIO5_MODE		0xE5	/* GPIO5 Mode OpenDrain/Push-Pull */
#define SIO_GPIO6_MODE		0xE6	/* GPIO6 Mode OpenDrain/Push-Pull */
#define SIO_GPIO_PUSHPULL   0x0
#define SIO_GPIO5_DIR      0xF4     /* GPIO5 I/O Register */
#define SIO_GPIO6_DIR      0xF8     /* GPIO6 I/O Register */
#define SIO_GPIO5_DATA      0xF5     /* GPIO5 Data Register */
#define SIO_GPIO6_DATA      0xF9     /* GPIO6 Data Register */
#define SIO_GP54_BIT		0x10
#define SIO_GP55_BIT		0x20
#define SIO_GP56_BIT        0x40
#define SIO_GP57_BIT        0x80
#define SIO_GPIO5_ALL	    SIO_GP54_BIT | SIO_GP55_BIT | SIO_GP56_BIT |SIO_GP57_BIT
#define SIO_GP60_BIT        0x01
#define SIO_GP61_BIT        0x02
#define SIO_GP62_BIT        0x04
#define SIO_GPIO6_ALL	    SIO_GP60_BIT | SIO_GP61_BIT |SIO_GP62_BIT

#define SIO_LD_GPIO		0x07	/* GPIO logical device */
#define SIO_LD_GPIO_MODE	0x0F	/* GPIO mode control device */
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

#define SIO_PCENGINES_APU_NCT5523D_ID	0xc453	/* Chip ID */

#define BOARD_NAME_DTA1161AC4      "DTA1161AC4"   /*DTA1161AC4  Product Name*/
#define BOARD_NAME_DTA1161BC8      "DTA1161BC8"   /*DTA1161BC8  Product Name*/


enum chips { nct5523d };

static const char * const nct5523d_names[] = {
	"nct5523d"
};

struct nct5523d_sio {
	int addr;
	enum chips type;
};

struct nct5523d_gpio_bank {
	struct gpio_chip chip;
	unsigned int regbase;
	struct nct5523d_gpio_data *data;
};

struct nct5523d_gpio_data {
	struct nct5523d_sio *sio;
	int nr_bank;
	struct nct5523d_gpio_bank *bank;
};

/*
 * Super-I/O functions.
 */

static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;

	outb(reg++, base);
	val = inb(base + 1) << 8;
	outb(reg, base);
	val |= inb(base + 1);

	return val;
}

static inline void superio_outb(int base, int reg, int val)
{
	outb(reg, base);
	outb(val, base + 1);
}

static inline int superio_enter(int base)
{
	/* Don't step on other drivers' I/O space by accident. */
	if (!request_muxed_region(base, 2, DRVNAME)) {
		pr_err(DRVNAME "I/O address 0x%04x already in use\n", base);
		return -EBUSY;
	}

	/* According to the datasheet the key must be send twice. */
	outb(SIO_UNLOCK_KEY, base);
	outb(SIO_UNLOCK_KEY, base);

	return 0;
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
	release_region(base, 2);
}

/*
 * GPIO chip.
 */

static int nct5523d_gpio_direction_in(struct gpio_chip *chip, unsigned offset);
static int nct5523d_gpio_get(struct gpio_chip *chip, unsigned offset);
static int nct5523d_gpio_get_direction(struct gpio_chip *chip, unsigned offset);
static int nct5523d_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value);
static void nct5523d_gpio_set(struct gpio_chip *chip, unsigned offset, int value);

#define NCT5523D_GPIO_BANK(_base, _ngpio, _regbase)			\
	{								\
		.chip = {						\
			.label            = DRVNAME,			\
			.owner            = THIS_MODULE,		\
			.get_direction	  = nct5523d_gpio_get_direction,      \
			.direction_input  = nct5523d_gpio_direction_in,	\
			.get              = nct5523d_gpio_get,		\
			.direction_output = nct5523d_gpio_direction_out,	\
			.set              = nct5523d_gpio_set,		\
			.base             = _base,			\
			.ngpio            = _ngpio,			\
			.can_sleep        = true,			\
		},							\
		.regbase = _regbase,					\
	}

#define gpio_dir(base) (base + 0)
#define gpio_data(base) (base + 1)

static struct nct5523d_gpio_bank nct5523d_gpio_bank[] = {
	NCT5523D_GPIO_BANK(50, 8, 0xF4),
	NCT5523D_GPIO_BANK(60, 8, 0xF8)
};

static int nct5523d_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct nct5523d_gpio_bank *bank =
		container_of(chip, struct nct5523d_gpio_bank, chip);
	struct nct5523d_sio *sio = bank->data->sio;
	u8 dir;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));
	dir |= (1 << offset);
	superio_outb(sio->addr, gpio_dir(bank->regbase), dir);

	superio_exit(sio->addr);

	return 0;
}

static int nct5523d_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct nct5523d_gpio_bank *bank =
		container_of(chip, struct nct5523d_gpio_bank, chip);
	struct nct5523d_sio *sio = bank->data->sio;
	u8 data;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	data = superio_inb(sio->addr, gpio_data(bank->regbase));

	superio_exit(sio->addr);

	return !!(data & 1 << offset);
}

static int nct5523d_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct nct5523d_gpio_bank *bank =
		container_of(chip, struct nct5523d_gpio_bank, chip);
	struct nct5523d_sio *sio = bank->data->sio;
	u8 direction;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	direction = superio_inb(sio->addr, gpio_dir(bank->regbase));

	superio_exit(sio->addr);

	return !!(direction & 1 << offset);
}


static int nct5523d_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	int err;
	struct nct5523d_gpio_bank *bank =
		container_of(chip, struct nct5523d_gpio_bank, chip);
	struct nct5523d_sio *sio = bank->data->sio;
	u8 dir, data_out;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	data_out = superio_inb(sio->addr, gpio_data(bank->regbase));
	if (value)
		data_out &= ~(1 << offset);
	else
		data_out |= (1 << offset);
	superio_outb(sio->addr, gpio_data(bank->regbase), data_out);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));
	dir &= ~(1 << offset);
	superio_outb(sio->addr, gpio_dir(bank->regbase), dir);

	superio_exit(sio->addr);

	return 0;
}

static void nct5523d_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	int err;
	struct nct5523d_gpio_bank *bank =
		container_of(chip, struct nct5523d_gpio_bank, chip);
	struct nct5523d_sio *sio = bank->data->sio;
	u8 data_out;

	err = superio_enter(sio->addr);
	if (err)
		return;
	superio_select(sio->addr, SIO_LD_GPIO);

	data_out = superio_inb(sio->addr, gpio_data(bank->regbase));
	if (value)
		data_out &= ~(1 << offset);
	else
		data_out |= (1 << offset);
	superio_outb(sio->addr, gpio_data(bank->regbase), data_out);

	superio_exit(sio->addr);
}

/*
 * Platform device and driver.
 */

static int nct5523d_gpio_probe(struct platform_device *pdev)
{
	int err;
	int i;
	struct nct5523d_sio *sio = pdev->dev.platform_data;
	struct nct5523d_gpio_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	switch (sio->type) {
	case nct5523d:
		data->nr_bank = ARRAY_SIZE(nct5523d_gpio_bank);
		data->bank = nct5523d_gpio_bank;
		break;
	default:
		return -ENODEV;
	}
	data->sio = sio;

	platform_set_drvdata(pdev, data);

	/* For each GPIO bank, register a GPIO chip. */
	for (i = 0; i < data->nr_bank; i++) {
		struct nct5523d_gpio_bank *bank = &data->bank[i];
		bank->chip.parent = &pdev->dev;
		bank->data = data;

		err = gpiochip_add(&bank->chip);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to register gpiochip %d: %d\n",
				i, err);
			goto err_gpiochip;
		}
	}

	return 0;

err_gpiochip:
	for (i = i - 1; i >= 0; i--) {
		struct nct5523d_gpio_bank *bank = &data->bank[i];
		gpiochip_remove (&bank->chip);
	}

	return err;
}

static int nct5523d_gpio_remove(struct platform_device *pdev)
{
	int i;
	struct nct5523d_gpio_data *data = platform_get_drvdata(pdev);

	for (i = 0; i < data->nr_bank; i++) {
		struct nct5523d_gpio_bank *bank = &data->bank[i];
		gpiochip_remove (&bank->chip);
	}
	return 0;
}

static int __init nct5523d_find(int addr, struct nct5523d_sio *sio)
{
	int err;
	u16 devid;
	u8 gpio_cfg,gpio_dir,gpio_data;	

	err = superio_enter(addr);
	if (err)
		return err;

	err = -ENODEV;

	devid = superio_inw(addr, SIO_CHIPID);
	switch (devid) {
	case SIO_PCENGINES_APU_NCT5523D_ID:
		sio->type = nct5523d;
		
		superio_select(addr, SIO_LD_GPIO);
		
		/* enable GPIO5 and GPIO6 */
		gpio_cfg = superio_inb(addr, SIO_GPIO_ENABLE);
		gpio_cfg |= SIO_GPIO5_BIT | SIO_GPIO6_BIT;
		superio_outb(addr, SIO_GPIO_ENABLE, gpio_cfg);
		
		gpio_data = superio_inb(addr, SIO_GPIO5_DATA);
		gpio_data &= ~SIO_GPIO5_ALL;
		superio_outb(addr, SIO_GPIO5_DATA, gpio_data);

		gpio_dir = superio_inb(addr, SIO_GPIO5_DIR);
		gpio_dir &= ~SIO_GPIO5_ALL;
		superio_outb(addr, SIO_GPIO5_DIR, gpio_dir);

		gpio_data = superio_inb(addr, SIO_GPIO6_DATA);
		gpio_data &= ~SIO_GPIO6_ALL;
		superio_outb(addr, SIO_GPIO6_DATA, gpio_data);

		gpio_dir = superio_inb(addr, SIO_GPIO6_DIR);
		gpio_dir &= ~SIO_GPIO6_ALL;
		superio_outb(addr, SIO_GPIO6_DIR, gpio_dir);		
		
		break;
	default:
		pr_info(DRVNAME ": Unsupported device 0x%04x\n", devid);
		goto err;
	}
	sio->addr = addr;
	err = 0;

	pr_info(DRVNAME ": Found %s at %#x chip id 0x%04x\n",
		nct5523d_names[sio->type],
		(unsigned int) addr,
		(int) superio_inw(addr, SIO_CHIPID));

        superio_select(sio->addr, SIO_LD_GPIO_MODE);
        superio_outb(sio->addr, SIO_GPIO5_MODE, SIO_GPIO_PUSHPULL);
        superio_outb(sio->addr, SIO_GPIO6_MODE, SIO_GPIO_PUSHPULL);
		

err:
	superio_exit(addr);
	return err;
}

static struct platform_device *nct5523d_gpio_pdev;

static int __init
nct5523d_gpio_device_add(const struct nct5523d_sio *sio)
{
	int err;

	nct5523d_gpio_pdev = platform_device_alloc(DRVNAME, -1);
	if (!nct5523d_gpio_pdev) {
		pr_err(DRVNAME ": Error platform_device_alloc\n");	
		return -ENOMEM;
	}

	err = platform_device_add_data(nct5523d_gpio_pdev, sio, sizeof(*sio));
	if (err) {
		pr_err(DRVNAME "Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(nct5523d_gpio_pdev);
	if (err) {
		pr_err(DRVNAME "Device addition failed\n");
		goto err;
         	pr_info(DRVNAME ": Device added\n");
	}

	return 0;

err:
	platform_device_put(nct5523d_gpio_pdev);
	return err;
	
}

static struct platform_driver nct5523d_gpio_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= nct5523d_gpio_probe,
	.remove		= nct5523d_gpio_remove,
};

static int __init nct5523d_gpio_init(void)
{
	int err;
	struct nct5523d_sio sio;
	const char *board_name = dmi_get_system_info(DMI_PRODUCT_NAME);			
	if(!strcmp(board_name, BOARD_NAME_DTA1161AC4) || !strcmp(board_name, BOARD_NAME_DTA1161BC8)) {
		if (nct5523d_find(0x2e, &sio) && nct5523d_find(0x4e, &sio))
			return -ENODEV;

		err = platform_driver_register(&nct5523d_gpio_driver);
		if (!err) {
			pr_info(DRVNAME ": platform_driver_register\n");
			err = nct5523d_gpio_device_add(&sio);
			if (err)
				platform_driver_unregister(&nct5523d_gpio_driver);
		}
	
		return err;
	}
	
	else {
		pr_info(DRVNAME "Board Name :%s :\n", board_name);
	}
	
	return 0;
	
}

subsys_initcall(nct5523d_gpio_init);

static void __exit nct5523d_gpio_exit(void)
{
	platform_device_unregister(nct5523d_gpio_pdev);	
	platform_driver_unregister(&nct5523d_gpio_driver);
}

module_exit(nct5523d_gpio_exit);
MODULE_DESCRIPTION("GPIO driver for Super-I/O chips NCT5523D");
MODULE_LICENSE("GPL");

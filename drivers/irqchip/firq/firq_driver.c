/*
 * FIQ example
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License Version
 * 2 as published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/fiq.h>

#define COUNTER_MAX		1000
#define DRV_NAME		"fiq"

struct firq_priv {
	void __iomem		*gic_cpu_regs;
	void __iomem		*ocram_regs;
	void __iomem		*epit2_regs;
	int	 		irq;
};

static bool firq_singleton;

/* Setup FIQ-mode registers on particular CPU. */
static long firq_setup_fiq_regs_cpu(void *data)
{
	struct pt_regs fiq_regs;
	struct firq_priv *priv = (struct firq_priv *)data;

	get_fiq_regs(&fiq_regs);
	/* We abuse the FIQ sp register to point to OCRAM VA */
	fiq_regs.ARM_sp = (u32)priv->ocram_regs;
	set_fiq_regs(&fiq_regs);

	return 0;
}

void firq_setup_fiq_regs(struct firq_priv *priv)
{
	int i, cpu;
	/* This will be stored in OCRAM at 0x00940000. */
	u32 ocram_setupdata[] = {
		(u32)priv->gic_cpu_regs,
		(u32)priv->irq,
		(u32)priv->epit2_regs,
	};

	/* Erase the OCRAM area 0x00940000-0x00940100 . */
	for (i = 0; i < 0x100; i += 4)
		writel(0, priv->ocram_regs + i);

	/* Preload the OCRAM area with data we will use in the FIQ handler. */
	for (i = 0; i < ARRAY_SIZE(ocram_setupdata); i++)
		writel(ocram_setupdata[i], priv->ocram_regs + (4 * i));

	dmb();
	/* Run firq_setup_fiq_regs_cpu() on all CPUs in system. */
	for_each_possible_cpu(cpu)
		work_on_cpu(cpu, firq_setup_fiq_regs_cpu, priv);
}

static struct fiq_handler fh = {
	.name   = "fiqdemo"
};

static int firq_probe(struct platform_device *pdev)
{
	struct firq_priv *priv;
	extern void firq_fiq_handler(void);
	extern void firq_fiq_handler_end(void);

	const unsigned int fiq_handler_size =
		firq_fiq_handler_end - firq_fiq_handler;

	if (firq_singleton)
		return -EBUSY;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		return -EINVAL;
	}

	/* FIXME: Do not hardcode the address */
	priv->gic_cpu_regs = devm_ioremap(&pdev->dev, 0x00a00100, 0x100);
	priv->ocram_regs = devm_ioremap(&pdev->dev, 0x00940000, 0x1000);
	priv->epit2_regs = devm_ioremap(&pdev->dev, 0x020d4000, 0x4000);
	if (!priv->gic_cpu_regs || !priv->ocram_regs || !priv->epit2_regs) {
		dev_err(&pdev->dev, "ioremap failed (%p %p %p)!\n",
			priv->gic_cpu_regs, priv->ocram_regs, priv->epit2_regs);
		return -EINVAL;
	}

	dev_err(&pdev->dev, "%s[%i] gic=%p, ocram=%p, epit=%p, irq=%i\n",
		__func__, __LINE__,
		priv->gic_cpu_regs, priv->ocram_regs,
		priv->epit2_regs, priv->irq);

	/* Register the FIQ handler */
	if (claim_fiq(&fh)) {
		dev_err(&pdev->dev, "couldn't claim FIQ.");
		return -ENODEV;
	}

	/* Setup the FIQ handler. */
	dev_info(&pdev->dev, "%s[%i] FIQ handler: start %p size %u\n",
		__func__, __LINE__, firq_fiq_handler, fiq_handler_size);
	print_hex_dump(KERN_INFO, "  ", DUMP_PREFIX_OFFSET, 16, 4,
		       firq_fiq_handler, fiq_handler_size, 0);
	dev_info(&pdev->dev, "%s[%i] registering FIQ handler\n",
		__func__, __LINE__);
	set_fiq_handler(firq_fiq_handler, fiq_handler_size);
	firq_setup_fiq_regs(priv);
	dev_info(&pdev->dev, "%s[%i] FIQ handler registered\n",
		__func__, __LINE__);

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "%s[%i] Enabling FIQ\n", __func__, __LINE__);
	enable_fiq(priv->irq);
	dev_info(&pdev->dev, "%s[%i] FIQ enabled\n", __func__, __LINE__);

	firq_singleton = true;

	/* Start EPIT to generate FIQs */
	writel(0x0, priv->epit2_regs + 0x0 /* CR */);
	writel(0xffff, priv->epit2_regs + 0x8 /* LR */);
	writel(0x1, priv->epit2_regs + 0x4 /* SR */);
	writel((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | /* EN, ENMOD, OCIEN, RLD */
		(1 << 19) | (0x2 << 24), /* WAITEN, CLKSRC=0x2 (32kHz) */
			priv->epit2_regs + 0x0 /* CR */);
	writel(0xffff, priv->epit2_regs + 0x8 /* LR */);
	writel(0x1, priv->epit2_regs + 0x4 /* SR */);

	return 0;
}

static long int firq_remove_cpu0(void *data)
{
	struct firq_priv *priv = (struct firq_priv *)data;

	/* Stop EPIT */
	local_fiq_disable();
	writel(0x0, priv->epit2_regs + 0x0 /* CR */);

	disable_fiq(priv->irq);
	local_fiq_enable();
	release_fiq(&fh);
	firq_singleton = false;
	return 0;
}

static int firq_remove(struct platform_device *pdev)
{
	struct firq_priv *priv = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s[%i] Removing module\n", __func__, __LINE__);

	/*
	 * You must remove FIRQ handler running on CPU0 from CPU0,
	 * otherwise there will be really bad side-effects!
	 */
	return work_on_cpu(0, firq_remove_cpu0, priv);
}

static const struct of_device_id firq_match[] = {
	{ .compatible = "denx,fiq" },
	{ },
};
MODULE_DEVICE_TABLE(of, firq_match);

static struct platform_driver firq_driver = {
	.driver	= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(firq_match),
	},
	.probe	= firq_probe,
	.remove	= firq_remove,
};
module_platform_driver(firq_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FIQ test");
MODULE_ALIAS("devname:firq");
MODULE_ALIAS("platform:firq");

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>

#define DRV_NAME          "pram"
#define IFNAMSIZ          16
#define PRAM_VERSION  "0.0.0"

#define	DMA_BUF_SIZE      (1024*1024)

struct mmio {
	uint8_t *virt;
	uint64_t start;
	uint64_t end;
	uint64_t flags;
	uint64_t len;
};

struct dma {
	uint8_t *virt;
	dma_addr_t phys;
};

struct pram {
	struct mmio bar0;
	struct mmio bar2;
	struct dma  bar2_dma;
};

struct pram_dev {
	struct pram dev;
};

/* Global variables */
static struct pram_dev *pram;


static int pram_open(struct inode *inode, struct file *filp)
{
	pr_info("%s\n", __func__);

	return 0;
}

static int pram_release(struct inode *inode, struct file *filp)
{
	pr_info("%s\n", __func__);

	return 0;
}

static ssize_t pram_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct mmio *bar2 = &pram->dev.bar2;

	//pr_info("%s\n", __func__);

	//*(uint32_t *)(bar0->virt + 0x00) = 0x55;

	if (copy_from_user((uint32_t *)(bar2->virt + 0x04), buf, sizeof(uint32_t))) {
		pr_info("copy_from_user failed\n");
		return -EFAULT;
	}


	return count;
}

static ssize_t pram_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct mmio *bar2 = &pram->dev.bar2;

	//pr_info("%s\n", __func__);

	if (copy_to_user(buf, (uint32_t *)(bar2->virt + 0x04), sizeof(uint32_t))) {
		pr_info("copy_to_user failed\n");
		return -EFAULT;
	}

	return sizeof(uint32_t);
}


static int pram_pci_init(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct dma  *bar2_dma = &pram->dev.bar2_dma;
	struct mmio *bar0 = &pram->dev.bar0;
	struct mmio *bar2 = &pram->dev.bar2;
	int rc;

	pr_info("%s\n", __func__);

	rc = pci_enable_device(pdev);
	if (rc)
		goto error;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto error;

	/* set BUS Master Mode */
	pci_set_master(pdev);

	/* BAR0 (pcie pio) */
	bar0->start = pci_resource_start(pdev, 0);
	bar0->end   = pci_resource_end(pdev, 0);
	bar0->flags = pci_resource_flags(pdev, 0);
	bar0->len   = pci_resource_len(pdev, 0);
	bar0->virt  = ioremap(bar0->start, bar0->len);
	if(!bar0->virt) {
		pr_err("cannot ioremap MMIO0 base\n");
		goto error;
	}
	pr_info("bar0_start: %X\n", (uint32_t)bar0->start);
	pr_info("bar0_end  : %X\n", (uint32_t)bar0->end);
	pr_info("bar0_flags: %X\n", (uint32_t)bar0->flags);
	pr_info("bar0_len  : %X\n", (uint32_t)bar0->len);

	/* BAR2 (pcie DMA) */
	bar2->start = pci_resource_start(pdev, 2);
	bar2->end   = pci_resource_end(pdev, 2);
	bar2->flags = pci_resource_flags(pdev, 2);
	bar2->len   = pci_resource_len(pdev, 2);
	bar2->virt  = ioremap(bar2->start, bar2->len);
	if (!bar2->virt) {
		pr_err("cannot ioremap MMIO1 base\n");
		goto error;
	}
	pr_info("bar2_virt : %p\n", bar2->virt);
	pr_info("bar2_start: %X\n", (uint32_t)bar2->start);
	pr_info("bar2_end  : %X\n", (uint32_t)bar2->end);
	pr_info("bar2_flags: %X\n", (uint32_t)bar2->flags);
	pr_info("bar2_len  : %X\n", (uint32_t)bar2->len);

	/* BAR2 (pcie DMA) */
	bar2_dma->virt = dma_alloc_coherent(&pdev->dev, DMA_BUF_SIZE, &bar2_dma->phys, GFP_KERNEL);
	if (!bar2_dma->virt) {
		pr_err("cannot dma_alloc_coherent\n");
		goto error;
	}
	pr_info("bar2_dma_virt: %p\n", bar2_dma->virt);
	pr_info("bar2_dma_phys: %X\n", (uint32_t)bar2_dma->phys);

	return 0;

error:
	pr_info("pram_pci_init error\n");
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return -1;
}

static void pram_pci_remove(struct pci_dev *pdev)
{
	struct dma  *bar2_dma = &pram->dev.bar2_dma;
	struct mmio *bar0 = &pram->dev.bar0;
	struct mmio *bar2 = &pram->dev.bar2;

	pr_info("%s\n", __func__);

	if (bar0->virt) {
		iounmap(bar0->virt);
		bar0->virt = 0;
	}

	if (bar2->virt) {
		iounmap(bar2->virt);
		bar2->virt = 0;
	}

	if (bar2_dma->virt) {
		dma_free_coherent(&pdev->dev, DMA_BUF_SIZE, bar2_dma->virt, bar2_dma->phys);
		bar2_dma->virt = 0;
		bar2_dma->phys = 0;
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct file_operations pram_fops = {
	.owner        = THIS_MODULE,
	.read         = pram_read,
	.write        = pram_write,
//	.poll         = pram_poll,
//	.compat_ioctl = pram_ioctl,
	.open         = pram_open,
	.release      = pram_release,
};

static struct miscdevice pram_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRV_NAME,
	.fops = &pram_fops,
};

static const struct pci_device_id pram_pci_tbl[] = {
	{0x3776, 0x8022, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{0,}
};
MODULE_DEVICE_TABLE(pci, pram_pci_tbl);

struct pci_driver pram_pci_driver = {
	.name = DRV_NAME,
	.id_table = pram_pci_tbl,
	.probe = pram_pci_init,
	.remove = pram_pci_remove,
	//	.suspend = pram_suspend,
	//	.resume = pram_resume,
};

static int __init pr_init(void)
{
	int rc = 0;

	pr_info("pram (v%s) is loaded\n", PRAM_VERSION);

	pram = kmalloc(sizeof(struct pram_dev), GFP_KERNEL);
	if (pram == 0) {
		pr_err("fail to kmalloc: *pram_dev\n");
		rc = -1;
		goto error;
	}

	rc = misc_register(&pram_dev);
	if (rc) {
		pr_err("fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		rc = -1;
		goto error;
	}

	return pci_register_driver(&pram_pci_driver);

error:
	kfree(pram);
	pram = NULL;
	return rc;
}
module_init(pr_init);


static void __exit pr_release(void)
{
	pr_info("pram (v%s) is unloaded\n", PRAM_VERSION);

	misc_deregister(&pram_dev);
	pci_unregister_driver(&pram_pci_driver);

	kfree(pram);
	pram = NULL;

	return;
}
module_exit(pr_release);


MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("pram");
MODULE_LICENSE("GPL");
MODULE_VERSION(PRAM_VERSION);


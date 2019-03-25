// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Management Engine Error Management
 *
 * Copyright 2019 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Wu Hao <hao.wu@intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Mitchel, Henry <henry.mitchel@intel.com>
 */

#include <linux/uaccess.h>

#include "dfl.h"
#include "dfl-fme.h"

#define FME_ERROR_MASK		0x8
#define FME_ERROR		0x10
#define MBP_ERROR		BIT_ULL(6)
#define PCIE0_ERROR_MASK	0x18
#define PCIE0_ERROR		0x20
#define PCIE1_ERROR_MASK	0x28
#define PCIE1_ERROR		0x30
#define FME_FIRST_ERROR		0x38
#define FME_NEXT_ERROR		0x40
#define RAS_NONFAT_ERROR_MASK	0x48
#define RAS_NONFAT_ERROR	0x50
#define RAS_CATFAT_ERROR_MASK	0x58
#define RAS_CATFAT_ERROR	0x60
#define RAS_ERROR_INJECT	0x68
#define INJECT_ERROR_MASK	GENMASK_ULL(2, 0)

static ssize_t errors_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + FME_ERROR));
}
static DEVICE_ATTR_RO(errors);

static ssize_t first_error_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + FME_FIRST_ERROR));
}
static DEVICE_ATTR_RO(first_error);

static ssize_t next_error_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + FME_NEXT_ERROR));
}
static DEVICE_ATTR_RO(next_error);

static ssize_t clear_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev->parent);
	struct device *err_dev = dev->parent;
	struct dfl_feature *feature;
	void __iomem *base;
	u64 v, val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	feature = dfl_get_feature_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);
	base = feature->ioaddr;

	mutex_lock(&pdata->lock);
	writeq(GENMASK_ULL(63, 0), base + FME_ERROR_MASK);

	v = readq(base + FME_ERROR);
	if (val != v) {
		ret = -EINVAL;
		goto done;
	}

	writeq(v, base + FME_ERROR);
	v = readq(base + FME_FIRST_ERROR);
	writeq(v, base + FME_FIRST_ERROR);
	v = readq(base + FME_NEXT_ERROR);
	writeq(v, base + FME_NEXT_ERROR);

done:
	/* Workaround: disable MBP_ERROR if feature revision is 0 */
	writeq(dfl_feature_revision(feature->ioaddr) ? 0ULL : MBP_ERROR,
	       base + FME_ERROR_MASK);
	mutex_unlock(&pdata->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(clear);

static ssize_t revision_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "%u\n", dfl_feature_revision(base));
}
static DEVICE_ATTR_RO(revision);

static ssize_t pcie0_errors_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + PCIE0_ERROR));
}

static ssize_t pcie0_errors_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev->parent);
	struct device *err_dev = dev->parent;
	void __iomem *base;
	u64 v, val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	mutex_lock(&pdata->lock);
	writeq(GENMASK_ULL(63, 0), base + PCIE0_ERROR_MASK);

	v = readq(base + PCIE0_ERROR);
	if (val != v)
		ret = -EBUSY;
	else
		writeq(v, base + PCIE0_ERROR);

	writeq(0ULL, base + PCIE0_ERROR_MASK);
	mutex_unlock(&pdata->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(pcie0_errors);

static ssize_t pcie1_errors_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + PCIE1_ERROR));
}

static ssize_t pcie1_errors_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev->parent);
	struct device *err_dev = dev->parent;
	void __iomem *base;
	u64 v, val;
	int ret;

	ret = kstrtou64(buf, 0, &val);
	if (ret)
		return ret;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	mutex_lock(&pdata->lock);
	writeq(GENMASK_ULL(63, 0), base + PCIE1_ERROR_MASK);

	v = readq(base + PCIE1_ERROR);
	if (val != v)
		ret = -EBUSY;
	else
		writeq(v, base + PCIE1_ERROR);

	writeq(0ULL, base + PCIE1_ERROR_MASK);
	mutex_unlock(&pdata->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(pcie1_errors);

static ssize_t nonfatal_errors_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + RAS_NONFAT_ERROR));
}
static DEVICE_ATTR_RO(nonfatal_errors);

static ssize_t catfatal_errors_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)readq(base + RAS_CATFAT_ERROR));
}
static DEVICE_ATTR_RO(catfatal_errors);

static ssize_t inject_error_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct device *err_dev = dev->parent;
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	v = readq(base + RAS_ERROR_INJECT);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			 (unsigned long long)FIELD_GET(INJECT_ERROR_MASK, v));
}

static ssize_t inject_error_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev->parent);
	struct device *err_dev = dev->parent;
	void __iomem *base;
	u8 inject_error;
	int ret;
	u64 v;

	ret = kstrtou8(buf, 0, &inject_error);
	if (ret)
		return ret;

	if (inject_error & ~INJECT_ERROR_MASK)
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(err_dev, FME_FEATURE_ID_GLOBAL_ERR);

	mutex_lock(&pdata->lock);
	v = readq(base + RAS_ERROR_INJECT);
	v &= ~INJECT_ERROR_MASK;
	v |= FIELD_PREP(INJECT_ERROR_MASK, inject_error);
	writeq(v, base + RAS_ERROR_INJECT);
	mutex_unlock(&pdata->lock);

	return count;
}
static DEVICE_ATTR_RW(inject_error);

static struct attribute *fme_errors_attrs[] = {
	&dev_attr_errors.attr,
	&dev_attr_first_error.attr,
	&dev_attr_next_error.attr,
	&dev_attr_clear.attr,
	NULL,
};

static struct attribute_group fme_errors_attr_group = {
	.attrs	= fme_errors_attrs,
	.name	= "fme-errors",
};

static struct attribute *errors_attrs[] = {
	&dev_attr_revision.attr,
	&dev_attr_pcie0_errors.attr,
	&dev_attr_pcie1_errors.attr,
	&dev_attr_nonfatal_errors.attr,
	&dev_attr_catfatal_errors.attr,
	&dev_attr_inject_error.attr,
	NULL,
};

static struct attribute_group errors_attr_group = {
	.attrs	= errors_attrs,
};

static const struct attribute_group *error_groups[] = {
	&fme_errors_attr_group,
	&errors_attr_group,
	NULL
};

static void fme_error_enable(struct dfl_feature *feature)
{
	void __iomem *base = feature->ioaddr;

	/* Workaround: disable MBP_ERROR if revision is 0 */
	writeq(dfl_feature_revision(feature->ioaddr) ? 0ULL : MBP_ERROR,
	       base + FME_ERROR_MASK);
	writeq(0ULL, base + PCIE0_ERROR_MASK);
	writeq(0ULL, base + PCIE1_ERROR_MASK);
	writeq(0ULL, base + RAS_NONFAT_ERROR_MASK);
	writeq(0ULL, base + RAS_CATFAT_ERROR_MASK);
}

static void err_dev_release(struct device *dev)
{
	kfree(dev);
}

static int fme_global_err_init(struct platform_device *pdev,
			       struct dfl_feature *feature)
{
	struct device *dev;
	int ret = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->parent = &pdev->dev;
	dev->release = err_dev_release;
	dev_set_name(dev, "errors");

	fme_error_enable(feature);

	ret = device_register(dev);
	if (ret) {
		put_device(dev);
		return ret;
	}

	ret = sysfs_create_groups(&dev->kobj, error_groups);
	if (ret) {
		device_unregister(dev);
		return ret;
	}

	feature->priv = dev;

	return ret;
}

static void fme_global_err_uinit(struct platform_device *pdev,
				 struct dfl_feature *feature)
{
	struct device *dev = feature->priv;

	sysfs_remove_groups(&dev->kobj, error_groups);
	device_unregister(dev);
}

const struct dfl_feature_id fme_global_err_id_table[] = {
	{.id = FME_FEATURE_ID_GLOBAL_ERR,},
	{0,}
};

const struct dfl_feature_ops fme_global_err_ops = {
	.init = fme_global_err_init,
	.uinit = fme_global_err_uinit,
};

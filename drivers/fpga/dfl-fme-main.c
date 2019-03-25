// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Management Engine (FME)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fpga-dfl.h>
#include <linux/sysfs.h>

#include "dfl.h"
#include "dfl-fme.h"

#define DRV_VERSION	"0.8"

static ssize_t ports_num_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_NUM_PORTS, v));
}
static DEVICE_ATTR_RO(ports_num);

/*
 * Bitstream (static FPGA region) identifier number. It contains the
 * detailed version and other information of this static FPGA region.
 */
static ssize_t bitstream_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_ID);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_id);

/*
 * Bitstream (static FPGA region) meta data. It contains the synthesis
 * date, seed and other information of this static FPGA region.
 */
static ssize_t bitstream_metadata_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_MD);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_metadata);

static ssize_t cache_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_CACHE_SIZE, v));
}
static DEVICE_ATTR_RO(cache_size);

static ssize_t fabric_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_FABRIC_VERID, v));
}
static DEVICE_ATTR_RO(fabric_version);

static ssize_t socket_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_SOCKET_ID, v));
}
static DEVICE_ATTR_RO(socket_id);

static const struct attribute *fme_hdr_attrs[] = {
	&dev_attr_ports_num.attr,
	&dev_attr_bitstream_id.attr,
	&dev_attr_bitstream_metadata.attr,
	&dev_attr_cache_size.attr,
	&dev_attr_fabric_version.attr,
	&dev_attr_socket_id.attr,
	NULL,
};

static int fme_hdr_init(struct platform_device *pdev,
			struct dfl_feature *feature)
{
	void __iomem *base = feature->ioaddr;
	int ret;

	dev_dbg(&pdev->dev, "FME HDR Init.\n");
	dev_dbg(&pdev->dev, "FME cap %llx.\n",
		(unsigned long long)readq(base + FME_HDR_CAP));

	ret = sysfs_create_files(&pdev->dev.kobj, fme_hdr_attrs);
	if (ret)
		return ret;

	return 0;
}

static void fme_hdr_uinit(struct platform_device *pdev,
			  struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "FME HDR UInit.\n");
	sysfs_remove_files(&pdev->dev.kobj, fme_hdr_attrs);
}

static long fme_hdr_ioctl_release_port(struct dfl_feature_platform_data *pdata,
				       void __user *arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	struct dfl_fpga_fme_port_release release;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_fme_port_release, port_id);

	if (copy_from_user(&release, arg, minsz))
		return -EFAULT;

	if (release.argsz < minsz || release.flags)
		return -EINVAL;

	return dfl_fpga_cdev_config_port(cdev, release.port_id, true);
}

static long fme_hdr_ioctl_assign_port(struct dfl_feature_platform_data *pdata,
				      void __user *arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	struct dfl_fpga_fme_port_assign assign;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_fme_port_assign, port_id);

	if (copy_from_user(&assign, arg, minsz))
		return -EFAULT;

	if (assign.argsz < minsz || assign.flags)
		return -EINVAL;

	return dfl_fpga_cdev_config_port(cdev, assign.port_id, false);
}

static long fme_hdr_ioctl(struct platform_device *pdev,
			  struct dfl_feature *feature,
			  unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	switch (cmd) {
	case DFL_FPGA_FME_PORT_RELEASE:
		return fme_hdr_ioctl_release_port(pdata, (void __user *)arg);
	case DFL_FPGA_FME_PORT_ASSIGN:
		return fme_hdr_ioctl_assign_port(pdata, (void __user *)arg);
	}

	return -ENODEV;
}

static const struct dfl_feature_id fme_hdr_id_table[] = {
	{.id = FME_FEATURE_ID_HEADER,},
	{0,}
};

static const struct dfl_feature_ops fme_hdr_ops = {
	.init = fme_hdr_init,
	.uinit = fme_hdr_uinit,
	.ioctl = fme_hdr_ioctl,
};

#define FME_THERM_THRESHOLD	0x8
#define TEMP_THRESHOLD1		GENMASK_ULL(6, 0)
#define TEMP_THRESHOLD1_EN	BIT_ULL(7)
#define TEMP_THRESHOLD2		GENMASK_ULL(14, 8)
#define TEMP_THRESHOLD2_EN	BIT_ULL(15)
#define TRIP_THRESHOLD		GENMASK_ULL(30, 24)
#define TEMP_THRESHOLD1_STATUS	BIT_ULL(32)		/* threshold1 reached */
#define TEMP_THRESHOLD2_STATUS	BIT_ULL(33)		/* threshold2 reached */
/* threshold1 policy: 0 - AP2 (90% throttle) / 1 - AP1 (50% throttle) */
#define TEMP_THRESHOLD1_POLICY	BIT_ULL(44)

#define FME_THERM_RDSENSOR_FMT1	0x10
#define FPGA_TEMPERATURE	GENMASK_ULL(6, 0)

#define FME_THERM_CAP		0x20
#define TEMP_THRESHOLD_DISABLE	BIT_ULL(0)

#define THERMAL_ATTR(_name, _mode, _show, _store)	\
struct device_attribute thermal_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

#define THERMAL_ATTR_RO(_name, _show)			\
	THERMAL_ATTR(_name, 0444, _show, NULL)

static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_RDSENSOR_FMT1);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FPGA_TEMPERATURE, v));
}
static THERMAL_ATTR_RO(temperature, temperature_show);

static struct attribute *thermal_mgmt_attrs[] = {
	&thermal_attr_temperature.attr,
	NULL,
};

static struct attribute_group thermal_mgmt_attr_group = {
	.name   = "thermal_mgmt",
	.attrs	= thermal_mgmt_attrs,
};

static ssize_t temp_threshold1_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TEMP_THRESHOLD1, v));
}
static THERMAL_ATTR_RO(threshold1, temp_threshold1_show);

static ssize_t temp_threshold2_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TEMP_THRESHOLD2, v));
}
static THERMAL_ATTR_RO(threshold2, temp_threshold2_show);

static ssize_t temp_trip_threshold_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TRIP_THRESHOLD, v));
}
static THERMAL_ATTR_RO(trip_threshold, temp_trip_threshold_show);

static ssize_t temp_threshold1_status_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TEMP_THRESHOLD1_STATUS, v));
}
static THERMAL_ATTR_RO(threshold1_status, temp_threshold1_status_show);

static ssize_t temp_threshold2_status_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TEMP_THRESHOLD2_STATUS, v));
}
static THERMAL_ATTR_RO(threshold2_status, temp_threshold2_status_show);

static ssize_t temp_threshold1_policy_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_THERMAL_MGMT);

	v = readq(base + FME_THERM_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(TEMP_THRESHOLD1_POLICY, v));
}
static THERMAL_ATTR_RO(threshold1_policy, temp_threshold1_policy_show);

static struct attribute *thermal_threshold_attrs[] = {
	&thermal_attr_threshold1.attr,
	&thermal_attr_threshold2.attr,
	&thermal_attr_trip_threshold.attr,
	&thermal_attr_threshold1_status.attr,
	&thermal_attr_threshold2_status.attr,
	&thermal_attr_threshold1_policy.attr,
	NULL,
};

static struct attribute_group thermal_threshold_attr_group = {
	.name   = "thermal_mgmt",
	.attrs	= thermal_threshold_attrs,
};

static int fme_thermal_mgmt_init(struct platform_device *pdev,
				 struct dfl_feature *feature)
{
	void __iomem *base = feature->ioaddr;
	int ret;
	u64 v;

	ret = sysfs_create_group(&pdev->dev.kobj, &thermal_mgmt_attr_group);
	if (ret)
		return ret;

	v = readq(base + FME_THERM_CAP);
	if (FIELD_GET(TEMP_THRESHOLD_DISABLE, v))
		return 0;

	ret = sysfs_merge_group(&pdev->dev.kobj, &thermal_threshold_attr_group);
	if (ret)
		sysfs_remove_group(&pdev->dev.kobj, &thermal_mgmt_attr_group);

	return ret;
}

static void fme_thermal_mgmt_uinit(struct platform_device *pdev,
				   struct dfl_feature *feature)
{
	sysfs_unmerge_group(&pdev->dev.kobj, &thermal_threshold_attr_group);
	sysfs_remove_group(&pdev->dev.kobj, &thermal_mgmt_attr_group);
}

static const struct dfl_feature_id fme_thermal_mgmt_id_table[] = {
	{.id = FME_FEATURE_ID_THERMAL_MGMT,},
	{0,}
};

static const struct dfl_feature_ops fme_thermal_mgmt_ops = {
	.init = fme_thermal_mgmt_init,
	.uinit = fme_thermal_mgmt_uinit,
};

#define FME_PWR_STATUS		0x8
#define FME_LATENCY_TOLERANCE	BIT_ULL(18)
#define PWR_CONSUMED		GENMASK_ULL(17, 0)

#define FME_PWR_THRESHOLD	0x10
#define PWR_THRESHOLD1		GENMASK_ULL(6, 0)	/* in Watts */
#define PWR_THRESHOLD2		GENMASK_ULL(14, 8)	/* in Watts */
#define PWR_THRESHOLD_MAX	0x7f
#define PWR_THRESHOLD1_STATUS	BIT_ULL(16)
#define PWR_THRESHOLD2_STATUS	BIT_ULL(17)

#define FME_PWR_XEON_LIMIT	0x18
#define XEON_PWR_LIMIT		GENMASK_ULL(14, 0)
#define XEON_PWR_EN		BIT_ULL(15)
#define FME_PWR_FPGA_LIMIT	0x20
#define FPGA_PWR_LIMIT		GENMASK_ULL(14, 0)
#define FPGA_PWR_EN		BIT_ULL(15)

#define POWER_ATTR(_name, _mode, _show, _store)	\
struct device_attribute power_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

#define POWER_ATTR_RO(_name, _show)			\
	POWER_ATTR(_name, 0444, _show, NULL)

#define POWER_ATTR_RW(_name, _show, _store)		\
	POWER_ATTR(_name, 0644, _show, _store)

static ssize_t pwr_consumed_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_STATUS);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(PWR_CONSUMED, v));
}
static POWER_ATTR_RO(consumed, pwr_consumed_show);

static ssize_t pwr_threshold1_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(PWR_THRESHOLD1, v));
}

static ssize_t pwr_threshold1_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u8 threshold;
	int ret;
	u64 v;

	ret = kstrtou8(buf, 0, &threshold);
	if (ret)
		return ret;

	if (threshold > PWR_THRESHOLD_MAX)
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	mutex_lock(&pdata->lock);
	v = readq(base + FME_PWR_THRESHOLD);
	v &= ~PWR_THRESHOLD1;
	v |= FIELD_PREP(PWR_THRESHOLD1, threshold);
	writeq(v, base + FME_PWR_THRESHOLD);
	mutex_unlock(&pdata->lock);

	return count;
}
static POWER_ATTR_RW(threshold1, pwr_threshold1_show, pwr_threshold1_store);

static ssize_t pwr_threshold2_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(PWR_THRESHOLD2, v));
}

static ssize_t pwr_threshold2_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u8 threshold;
	int ret;
	u64 v;

	ret = kstrtou8(buf, 0, &threshold);
	if (ret)
		return ret;

	if (threshold > PWR_THRESHOLD_MAX)
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	mutex_lock(&pdata->lock);
	v = readq(base + FME_PWR_THRESHOLD);
	v &= ~PWR_THRESHOLD2;
	v |= FIELD_PREP(PWR_THRESHOLD2, threshold);
	writeq(v, base + FME_PWR_THRESHOLD);
	mutex_unlock(&pdata->lock);

	return count;
}
static POWER_ATTR_RW(threshold2, pwr_threshold2_show, pwr_threshold2_store);

static ssize_t pwr_threshold1_status_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(PWR_THRESHOLD1_STATUS, v));
}
static POWER_ATTR_RO(threshold1_status, pwr_threshold1_status_show);

static ssize_t pwr_threshold2_status_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_THRESHOLD);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(PWR_THRESHOLD2_STATUS, v));
}
static POWER_ATTR_RO(threshold2_status, pwr_threshold2_status_show);

static ssize_t ltr_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_STATUS);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_LATENCY_TOLERANCE, v));
}
static POWER_ATTR_RO(ltr, ltr_show);

static ssize_t xeon_limit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u16 xeon_limit = 0;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_XEON_LIMIT);

	if (FIELD_GET(XEON_PWR_EN, v))
		xeon_limit = FIELD_GET(XEON_PWR_LIMIT, v);

	return scnprintf(buf, PAGE_SIZE, "%u\n", xeon_limit);
}
static POWER_ATTR_RO(xeon_limit, xeon_limit_show);

static ssize_t fpga_limit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u16 fpga_limit = 0;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_POWER_MGMT);

	v = readq(base + FME_PWR_FPGA_LIMIT);

	if (FIELD_GET(FPGA_PWR_EN, v))
		fpga_limit = FIELD_GET(FPGA_PWR_LIMIT, v);

	return scnprintf(buf, PAGE_SIZE, "%u\n", fpga_limit);
}
static POWER_ATTR_RO(fpga_limit, fpga_limit_show);

static struct attribute *power_mgmt_attrs[] = {
	&power_attr_consumed.attr,
	&power_attr_threshold1.attr,
	&power_attr_threshold2.attr,
	&power_attr_threshold1_status.attr,
	&power_attr_threshold2_status.attr,
	&power_attr_xeon_limit.attr,
	&power_attr_fpga_limit.attr,
	&power_attr_ltr.attr,
	NULL,
};

static struct attribute_group power_mgmt_attr_group = {
	.attrs	= power_mgmt_attrs,
	.name	= "power_mgmt",
};

static int fme_power_mgmt_init(struct platform_device *pdev,
			       struct dfl_feature *feature)
{
	return sysfs_create_group(&pdev->dev.kobj, &power_mgmt_attr_group);
}

static void fme_power_mgmt_uinit(struct platform_device *pdev,
				 struct dfl_feature *feature)
{
	sysfs_remove_group(&pdev->dev.kobj, &power_mgmt_attr_group);
}

static const struct dfl_feature_id fme_power_mgmt_id_table[] = {
	{.id = FME_FEATURE_ID_POWER_MGMT,},
	{0,}
};

static const struct dfl_feature_ops fme_power_mgmt_ops = {
	.init = fme_power_mgmt_init,
	.uinit = fme_power_mgmt_uinit,
};

static struct dfl_feature_driver fme_feature_drvs[] = {
	{
		.id_table = fme_hdr_id_table,
		.ops = &fme_hdr_ops,
	},
	{
		.id_table = fme_pr_mgmt_id_table,
		.ops = &fme_pr_mgmt_ops,
	},
	{
		.id_table = fme_thermal_mgmt_id_table,
		.ops = &fme_thermal_mgmt_ops,
	},
	{
		.id_table = fme_power_mgmt_id_table,
		.ops = &fme_power_mgmt_ops,
	},
	{
		.id_table = fme_global_err_id_table,
		.ops = &fme_global_err_ops,
	},
	{
		.id_table = fme_perf_id_table,
		.ops = &fme_perf_ops,
	},
	{
		.ops = NULL,
	},
};

static long fme_ioctl_check_extension(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	/* No extension support for now */
	return 0;
}

static int fme_open(struct inode *inode, struct file *filp)
{
	struct platform_device *fdev = dfl_fpga_inode_to_feature_dev(inode);
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&fdev->dev);
	int ret;

	if (WARN_ON(!pdata))
		return -ENODEV;

	ret = dfl_feature_dev_use_begin(pdata);
	if (ret)
		return ret;

	dev_dbg(&fdev->dev, "Device File Open\n");
	filp->private_data = pdata;

	return 0;
}

static int fme_release(struct inode *inode, struct file *filp)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;

	dev_dbg(&pdev->dev, "Device File Release\n");
	dfl_feature_dev_use_end(pdata);

	return 0;
}

static long fme_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	switch (cmd) {
	case DFL_FPGA_GET_API_VERSION:
		return DFL_FPGA_API_VERSION;
	case DFL_FPGA_CHECK_EXTENSION:
		return fme_ioctl_check_extension(pdata, arg);
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd.
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 or other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(pdata, f) {
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
		}
	}

	return -EINVAL;
}

static int fme_dev_init(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme *fme;

	fme = devm_kzalloc(&pdev->dev, sizeof(*fme), GFP_KERNEL);
	if (!fme)
		return -ENOMEM;

	fme->pdata = pdata;

	mutex_lock(&pdata->lock);
	dfl_fpga_pdata_set_private(pdata, fme);
	mutex_unlock(&pdata->lock);

	return 0;
}

static void fme_dev_destroy(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme *fme;

	mutex_lock(&pdata->lock);
	fme = dfl_fpga_pdata_get_private(pdata);
	dfl_fpga_pdata_set_private(pdata, NULL);
	mutex_unlock(&pdata->lock);
}

static const struct file_operations fme_fops = {
	.owner		= THIS_MODULE,
	.open		= fme_open,
	.release	= fme_release,
	.unlocked_ioctl = fme_ioctl,
};

static int fme_probe(struct platform_device *pdev)
{
	int ret;

	ret = fme_dev_init(pdev);
	if (ret)
		goto exit;

	ret = dfl_fpga_dev_feature_init(pdev, fme_feature_drvs);
	if (ret)
		goto dev_destroy;

	ret = dfl_fpga_dev_ops_register(pdev, &fme_fops, THIS_MODULE);
	if (ret)
		goto feature_uinit;

	return 0;

feature_uinit:
	dfl_fpga_dev_feature_uinit(pdev);
dev_destroy:
	fme_dev_destroy(pdev);
exit:
	return ret;
}

static int fme_remove(struct platform_device *pdev)
{
	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);
	fme_dev_destroy(pdev);

	return 0;
}

static struct platform_driver fme_driver = {
	.driver	= {
		.name    = DFL_FPGA_FEATURE_DEV_FME,
	},
	.probe   = fme_probe,
	.remove  = fme_remove,
};

module_platform_driver(fme_driver);

MODULE_DESCRIPTION("FPGA Management Engine driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-fme");
MODULE_VERSION(DRV_VERSION);

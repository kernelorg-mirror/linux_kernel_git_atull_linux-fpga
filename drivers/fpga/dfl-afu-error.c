// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Accelerated Function Unit (AFU) Error Reporting
 *
 * Copyright 2019 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@linux.intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Mitchel Henry <henry.mitchel@intel.com>
 */

#include <linux/uaccess.h>

#include "dfl-afu.h"

#define PORT_ERROR_MASK		0x8
#define PORT_ERROR		0x10
#define PORT_FIRST_ERROR	0x18
#define PORT_MALFORMED_REQ0	0x20
#define PORT_MALFORMED_REQ1	0x28

#define ERROR_MASK		GENMASK_ULL(63, 0)

/* mask or unmask port errors by the error mask register. */
static void __port_err_mask(struct device *dev, bool mask)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	writeq(mask ? ERROR_MASK : 0, base + PORT_ERROR_MASK);
}

/* clear port errors. */
static int __port_err_clear(struct device *dev, u64 err)
{
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *base_err, *base_hdr;
	int ret;
	u64 v;

	base_err = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);
	base_hdr = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_HEADER);

	/*
	 * clear Port Errors
	 *
	 * - Check for AP6 State
	 * - Halt Port by keeping Port in reset
	 * - Set PORT Error mask to all 1 to mask errors
	 * - Clear all errors
	 * - Set Port mask to all 0 to enable errors
	 * - All errors start capturing new errors
	 * - Enable Port by pulling the port out of reset
	 */

	/* if device is still in AP6 power state, can not clear any error. */
	v = readq(base_hdr + PORT_HDR_STS);
	if (FIELD_GET(PORT_STS_PWR_STATE, v) == PORT_STS_PWR_STATE_AP6) {
		dev_err(dev, "Could not clear errors, device in AP6 state.\n");
		return -EBUSY;
	}

	/* Halt Port by keeping Port in reset */
	ret = __port_disable(pdev);
	if (ret)
		return ret;

	/* Mask all errors */
	__port_err_mask(dev, true);

	/* Clear errors if err input matches with current port errors.*/
	v = readq(base_err + PORT_ERROR);

	if (v == err) {
		writeq(v, base_err + PORT_ERROR);

		v = readq(base_err + PORT_FIRST_ERROR);
		writeq(v, base_err + PORT_FIRST_ERROR);
	} else {
		ret = -EBUSY;
	}

	/* Clear mask */
	__port_err_mask(dev, false);

	/* Enable the Port by clear the reset */
	__port_enable(pdev);

	return ret;
}

static ssize_t revision_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	return scnprintf(buf, PAGE_SIZE, "%u\n", dfl_feature_revision(base));
}
static DEVICE_ATTR_RO(revision);

static ssize_t errors_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 error;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	error = readq(base + PORT_ERROR);
	mutex_unlock(&pdata->lock);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)error);
}
static DEVICE_ATTR_RO(errors);

static ssize_t first_error_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 error;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	error = readq(base + PORT_FIRST_ERROR);
	mutex_unlock(&pdata->lock);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)error);
}
static DEVICE_ATTR_RO(first_error);

static ssize_t first_malformed_req_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 req0, req1;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	req0 = readq(base + PORT_MALFORMED_REQ0);
	req1 = readq(base + PORT_MALFORMED_REQ1);
	mutex_unlock(&pdata->lock);

	return scnprintf(buf, PAGE_SIZE, "0x%016llx%016llx\n",
			 (unsigned long long)req1, (unsigned long long)req0);
}
static DEVICE_ATTR_RO(first_malformed_req);

static ssize_t clear_store(struct device *dev, struct device_attribute *attr,
			   const char *buff, size_t count)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	u64 value;
	int ret;

	if (kstrtou64(buff, 0, &value))
		return -EINVAL;

	mutex_lock(&pdata->lock);
	ret = __port_err_clear(dev, value);
	mutex_unlock(&pdata->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(clear);

static struct attribute *port_err_attrs[] = {
	&dev_attr_revision.attr,
	&dev_attr_errors.attr,
	&dev_attr_first_error.attr,
	&dev_attr_first_malformed_req.attr,
	&dev_attr_clear.attr,
	NULL,
};

static struct attribute_group port_err_attr_group = {
	.attrs = port_err_attrs,
	.name = "errors",
};

static int port_err_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	dev_dbg(&pdev->dev, "PORT ERR Init.\n");

	mutex_lock(&pdata->lock);
	__port_err_mask(&pdev->dev, false);
	mutex_unlock(&pdata->lock);

	return sysfs_create_group(&pdev->dev.kobj, &port_err_attr_group);
}

static void port_err_uinit(struct platform_device *pdev,
			   struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT ERR UInit.\n");

	sysfs_remove_group(&pdev->dev.kobj, &port_err_attr_group);
}

const struct dfl_feature_id port_err_id_table[] = {
	{.id = PORT_FEATURE_ID_ERROR,},
	{0,}
};

const struct dfl_feature_ops port_err_ops = {
	.init = port_err_init,
	.uinit = port_err_uinit,
};

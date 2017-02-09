/*
 * FPGA Region - Device Tree support for FPGA programming under Linux
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 *
 * Dynamic DT code borrowed from Pantelis Antoniou's
 * "OF: DT-Overlay configfs interface (v7)" patch.
 * Copyright (C) 2013 - Pantelis Antoniou <panto@antoniou-consulting.com>
 *
 * FIT image parsing adapted from u-boot
 * (C) Copyright 2008 Semihalf
 * (C) Copyright 2000-2006
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/firmware.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-region.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static DEFINE_IDA(fpga_region_ida);
static struct class *fpga_region_class;

struct fpga_region *fpga_region_class_find(
	struct device *start, const void *data,
	int (*match)(struct device *, const void *))
{
	struct device *dev;

	dev = class_find_device(fpga_region_class, start, data, match);
	if (!dev)
		return NULL;

	return to_fpga_region(dev);
}
EXPORT_SYMBOL_GPL(fpga_region_class_find);

/**
 * fpga_region_get - get an exclusive reference to a fpga region
 * @region: FPGA Region struct
 *
 * Caller should call fpga_region_put() when done with region.
 *
 * Return fpga_region struct if successful.
 * Return -EBUSY if someone already has a reference to the region.
 * Return -ENODEV if @np is not a FPGA Region.
 */
static struct fpga_region *fpga_region_get(struct fpga_region *region)
{
	struct device *dev = &region->dev;

	if (!mutex_trylock(&region->mutex)) {
		dev_dbg(dev, "%s: FPGA Region already in use\n", __func__);
		return ERR_PTR(-EBUSY);
	}

	get_device(dev);
	if (!try_module_get(dev->parent->driver->owner)) {
		put_device(dev);
		mutex_unlock(&region->mutex);
		return ERR_PTR(-ENODEV);
	}

	dev_dbg(dev, "get\n");

	return region;
}

/**
 * fpga_region_put - release a reference to a region
 *
 * @region: FPGA region
 */
static void fpga_region_put(struct fpga_region *region)
{
	struct device *dev = &region->dev;

	dev_dbg(dev, "put\n");

	module_put(dev->parent->driver->owner);
	put_device(dev);
	mutex_unlock(&region->mutex);
}

/*
 * todo - of_fpga_region_overlay_apply() is adapted from create_overlay()
 * (some changed structs) from Pantelis Antoniou's
 * "OF: DT-Overlay configfs interface (v7)" patch.
 * that hasn't been accepted upstream yet.
 */
static int of_fpga_region_overlay_apply(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	int ret;

	WARN_ON(info->overlay);

	of_fdt_unflatten_tree((void *)info->fdt_blob, NULL,
			      &info->overlay);
	if (!info->overlay) {
		dev_err(dev, "%s: failed to unflatten tree\n", __func__);
		ret = -EINVAL;
		return ret;
	}
	pr_debug("%s: unflattened OK\n", __func__);

	/* mark it as detached */
	of_node_set_flag(info->overlay, OF_DETACHED);

	/* FDT in bitstream should not specify an external FPGA image to load */
	if (of_find_property(info->overlay, "firmware-name", NULL))
		dev_warn(dev, "Ignoring firmware-name property in FPGA image header\n");

	/* perform resolution */
	ret = of_resolve_phandles(info->overlay);
	if (ret != 0) {
		dev_err(dev, "%s: Failed to resolve tree\n", __func__);
		return ret;
	}
	pr_debug("%s: resolved OK\n", __func__);

	ret = of_overlay_create(info->overlay);
	if (ret < 0) {
		dev_err(dev, "%s: Failed to create overlay (err=%d)\n",
			__func__, ret);
		return ret;
	}
	info->ov_id = ret;

	return 0;
}

static void fpga_region_overlay_rm(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;

	if (info->ov_id >= 0) {
		of_overlay_destroy(info->ov_id);
		info->ov_id = -1;
	}
	if (info->fdt_blob) {
		devm_kfree(dev, info->fdt_blob);
		info->fdt_blob = NULL;
	}
}

/**
 * get_data_prop - get pointer, length of binary data
 *
 * FDT is in info->header.  Find the data property at the node offset.
 * Give a pointer to the binary data.
 *
 * Returns length of data or zero
 */
static int get_data_prop(struct fpga_image_info *info,
			 int noffset, const void **data)
{
	int len;

	*data = fdt_getprop(info->header, noffset, "data", &len);
	if (!*data)
		return 0;

	return len;
}

static int get_int_prop(struct fpga_image_info *info,
			int root_offset,
			const char *prop_str)
{
	const u32 *prop;

	prop = fdt_getprop(info->header, root_offset, prop_str, NULL);
	if (prop)
		return fdt32_to_cpu(*prop);

	return 0;
}

static void get_bool_prop_to_flags(struct fpga_image_info *info,
				   int root_offset,
				   const char *prop_str, int bit)
{
	const u32 *prop;

	prop = fdt_getprop(info->header, root_offset, prop_str, NULL);
	if (prop)
		info->flags |= bit;
}

/**
 * region_parse_section_fpga - parse fpga image section
 *
 * FPGA image header is in a contiguous buffer (region->info->header).
 * Parse the image info.  Get pointers to the raw image.
 *
 * @region: FPGA region
 * @node_offset: offset to the flat_dt node
 *
 * Return: 0 for success or negative error code.
 */
static int region_parse_section_fpga(struct fpga_region *region,
				     int node_offset)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	const void *raw_image;
	size_t image_size;

	if (fdt_getprop(info->header, node_offset, "firmware-name", NULL)) {
		dev_warn(dev,
			 "Ignoring firmware-name property in FPGA image header\n");
	}

	get_bool_prop_to_flags(info, node_offset, "partial-fpga-config",
			       FPGA_MGR_PARTIAL_RECONFIG);

	get_bool_prop_to_flags(info, node_offset, "external-fpga-config",
			       FPGA_MGR_EXTERNAL_CONFIG);

	get_bool_prop_to_flags(info, node_offset, "encrypted-fpga-config",
			       FPGA_MGR_ENCRYPTED_BITSTREAM);

	info->enable_timeout_us =
		get_int_prop(info, node_offset, "region-unfreeze-timeout-us");

	info->disable_timeout_us =
		get_int_prop(info, node_offset, "region-freeze-timeout-us");

	info->config_complete_timeout_us =
		get_int_prop(info, node_offset, "config-complete-timeout-us");

	image_size = get_data_prop(info, node_offset, &raw_image);
	if (!image_size)
		return 0;

	if (info->sgt) {
		/* todo */
	} else {
		info->buf = raw_image;
		info->count = image_size;
	}

	return 0;
}

/**
 * region_parse_section_flat_dt - Allocate memory and copy the flat_dt image
 *
 * FPGA image header is in a contiguous buffer (region->info->header).
 *
 * @region: FPGA region
 * @node_offset: offset to the flat_dt node
 *
 * Return: 0 for success or negative error code.
 */
static int region_parse_section_flat_dt(struct fpga_region *region,
					int node_offset)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	size_t fdt_size;
	char *fdt_blob;
	const void *fdt_data;

	fdt_size = get_data_prop(info, node_offset, &fdt_data);
	if (!fdt_size)
		return -EINVAL;

	pr_err("fdt_size   = %d\n", fdt_size);

	/* Save the DT overlay.  We will apply it after FPGA programming */
	if (info->sgt) {
		/* todo */
	} else {
		fdt_blob = devm_kmemdup(dev, fdt_data, fdt_size, GFP_KERNEL);
		if (!fdt_blob)
			return -ENOMEM;
		info->fdt_blob = fdt_blob;
	}

	if (fdt_size != fdt_totalsize(info->fdt_blob)) {
		dev_err(dev, "FDT overlay size mismatch %d vs %d\n",
			fdt_size, fdt_totalsize(info->fdt_blob));
		devm_kfree(dev, info->fdt_blob);
		info->fdt_blob = NULL;
		return -EINVAL;
	}

	return 0;
}

/**
 * fpga_region_parse_images - find images in the header and parse them
 * Assume there is at maximum one fpga image and one flat_dt image.
 *
 * FPGA image header is in a contiguous buffer (region->info->header).
 *
 * @region: FPGA region
 *
 * Return: 0 for success or negative error code.
 */
static int fpga_region_parse_images(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	const char *type_prop;
	int images_offset;
	int ndepth, count, noffset, ret;

	if (fdt_magic(info->header) != FDT_MAGIC) {
		dev_err(dev, "Invalid magic for FPGA image\n");
		return -EINVAL;
	}

	if (fdt_check_header(info->header)) {
		dev_err(dev, "Invalid device tree blob header\n");
		return -EINVAL;
	}

	/* Find the /images node */
	images_offset = fdt_path_offset(info->header, "/images");
	if (images_offset < 0) {
		dev_err(dev, "could not find /images in FPGA image header\n");
		return -EINVAL;
	}

	/* Find the first fpga and flat_dt sections, parse them. */
	ndepth = 0;
	noffset = fdt_next_node(info->header, images_offset, &ndepth);
	for (count = 0;	(noffset >= 0) && (ndepth > 0);
	     noffset = fdt_next_node(info->header, noffset, &ndepth)) {
		if (ndepth != 1)
			continue;

		type_prop = fdt_getprop(info->header, noffset, "type", NULL);

		if (!strcmp(type_prop, "fpga")) {
			ret = region_parse_section_fpga(region, noffset);
		} else if (!strcmp(type_prop, "flat_dt")) {
			ret = region_parse_section_flat_dt(region, noffset);
		} else {
			dev_err(dev,
				"Unsupported FPGA image header node type (%s)\n",
				type_prop);
			return -EINVAL;
		}

		count++;
	}

	return 0;
}

/**
 * fpga_region_parse_header - parse FPGA image header
 *
 * FPGA image is in region->info's sg list, contiguous buffer
 * (info->header), or firmware.
 *
 * @region: FPGA region
 *
 * Parse the FPGA stream header, leave the results in the image info.
 *
 * Return: 0 for success or negative error code.
 */
static int fpga_region_parse_header(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	int ret;

	if (!info->header) {
		if (info->sgt) {
			/* todo */
		} else if (info->fw) {
			info->header = (char *)info->fw->data;
		} else {
			dev_err(dev, "No header\n");
			return -EINVAL;
		}
	}

	ret = fpga_region_parse_images(region);

	return ret;
}

/**
 * fpga_region_program_fpga - program FPGA
 * @region: FPGA region
 * Program an FPGA using fpga image info (region->info).
 * Return 0 for success or negative error code.
 */
int fpga_region_program_fpga(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	int ret;

	if (!region->info)
		return -EINVAL;

	region = fpga_region_get(region);
	if (IS_ERR(region)) {
		dev_err(dev, "failed to get FPGA region\n");
		return PTR_ERR(region);
	}

	ret = fpga_mgr_lock(region->mgr);
	if (ret) {
		dev_err(dev, "FPGA manager is busy\n");
		goto err_put_region;
	}

	/*
	 * In some cases, we already have a list of bridges in the
	 * fpga region struct.  Or we don't have any bridges.
	 */
	if (region->get_bridges) {
		ret = region->get_bridges(region);
		if (ret) {
			dev_err(dev, "failed to get fpga region bridges\n");
			goto err_unlock_mgr;
		}
	}

	ret = fpga_bridges_disable(&region->bridge_list);
	if (ret) {
		dev_err(dev, "failed to disable bridges\n");
		goto err_put_br;
	}

	ret = fpga_mgr_load(region->mgr, info);
	if (ret) {
		dev_err(dev, "failed to load FPGA image\n");
		goto err_put_br;
	}

	ret = fpga_bridges_enable(&region->bridge_list);
	if (ret) {
		dev_err(dev, "failed to enable region bridges\n");
		goto err_put_br;
	}

	fpga_mgr_unlock(region->mgr);
	fpga_region_put(region);

	return 0;

err_put_br:
	if (region->get_bridges)
		fpga_bridges_put(&region->bridge_list);
err_unlock_mgr:
	fpga_mgr_unlock(region->mgr);
err_put_region:
	fpga_region_put(region);

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_region_program_fpga);

/**
 * fpga_region_free
 * Undo some of what fpga_region_program_fpga() did to free up
 * region to be reprogrammed.
 * @region: FPGA region
 */
void fpga_region_free(struct fpga_region *region)
{
	if (!region->info)
		return;

	fpga_bridges_disable(&region->bridge_list);
	if (region->get_bridges)
		fpga_bridges_put(&region->bridge_list);

	fpga_image_info_free(region->info);
	region->info = NULL;
}
EXPORT_SYMBOL_GPL(fpga_region_free);

/* Region's image info has sg/buf/firmware.  Parse it, apply it */
int fpga_region_fdt_image_apply(struct fpga_region *region)
{
	struct device *dev = &region->dev;
	struct fpga_image_info *info = region->info;
	const struct firmware *fw;
	int ret;

	if (info->firmware_name) {
		ret = request_firmware(&fw, info->firmware_name, dev);
		if (ret)
			return ret;
		info->fw = fw;
	}

	ret = fpga_region_parse_header(region);
	if (ret)
		goto out_rel;

	if (info->buf) {
		ret = fpga_region_program_fpga(region);
		if (ret)
			goto out_rel;
	}

	if (info->fdt_blob) {
		ret = of_fpga_region_overlay_apply(region);
		if (ret) {
			dev_err(dev, "failed to apply fdt in bitstream\n");
			devm_kfree(dev, info->fdt_blob);
			info->fdt_blob = NULL;
		}
	}

out_rel:
	if (info->fw) {
		release_firmware(fw);
		info->fw = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(fpga_region_fdt_image_apply);

#if IS_ENABLED(CONFIG_FPGA_REGION_SYSFS)

static ssize_t firmware_name_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct fpga_region *region = to_fpga_region(dev);
	struct fpga_image_info *info = region->info;

	if (info && info->firmware_name)
		return sprintf(buf, "%s\n", info->firmware_name);

	return 0;
}

static ssize_t firmware_name_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct fpga_region *region = to_fpga_region(dev);
	struct fpga_image_info *info;
	char *firmware_name;
	int ret;

	firmware_name = devm_kzalloc(dev, count, GFP_KERNEL);
	if (!firmware_name)
		return -ENOMEM;

	ret = sscanf(buf, "%s", firmware_name);
	if (ret != 1) {
		devm_kfree(dev, firmware_name);
		return -EINVAL;
	}

	/* If empty string input, free up the region */
	if (!strnlen(firmware_name, count)) {
		fpga_region_overlay_rm(region);
		fpga_region_free(region);
		devm_kfree(dev, firmware_name);
		return count;
	}

	/* Must free up region if programmed */
	if (region->info) {
		dev_err(dev, "region already in use\n");
		return -EINVAL;
	}

	info = fpga_image_info_alloc(dev);
	if (!info)
		return -ENOMEM;

	info->firmware_name = firmware_name;
	region->info = info;
	ret = fpga_region_fdt_image_apply(region);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(firmware_name);

static struct attribute *fpga_region_attrs[] = {
	&dev_attr_firmware_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(fpga_region);

#endif /* CONFIG_FPGA_REGION_SYSFS */

int fpga_region_register(struct device *dev, struct fpga_region *region)
{
	int id, ret = 0;

	id = ida_simple_get(&fpga_region_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	mutex_init(&region->mutex);
	INIT_LIST_HEAD(&region->bridge_list);
	device_initialize(&region->dev);
	region->dev.class = fpga_region_class;
	region->dev.parent = dev;
	region->dev.of_node = dev->of_node;
	region->dev.id = id;
	dev_set_drvdata(dev, region);

	ret = dev_set_name(&region->dev, "region%d", id);
	if (ret)
		goto err_remove;

	ret = device_add(&region->dev);
	if (ret)
		goto err_remove;

	return 0;

err_remove:
	ida_simple_remove(&fpga_region_ida, id);
	return ret;
}
EXPORT_SYMBOL_GPL(fpga_region_register);

int fpga_region_unregister(struct fpga_region *region)
{
	device_unregister(&region->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(fpga_region_unregister);

static void fpga_region_dev_release(struct device *dev)
{
	struct fpga_region *region = to_fpga_region(dev);

	ida_simple_remove(&fpga_region_ida, region->dev.id);
}

/**
 * fpga_region_init - init function for fpga_region class
 * Creates the fpga_region class and registers a reconfig notifier.
 */
static int __init fpga_region_init(void)
{
	fpga_region_class = class_create(THIS_MODULE, "fpga_region");
	if (IS_ERR(fpga_region_class))
		return PTR_ERR(fpga_region_class);

	fpga_region_class->dev_release = fpga_region_dev_release;

#if IS_ENABLED(CONFIG_FPGA_REGION_SYSFS)
	fpga_region_class->dev_groups = fpga_region_groups;
#endif /* CONFIG_FPGA_REGION_SYSFS */

	return 0;
}

static void __exit fpga_region_exit(void)
{
	class_destroy(fpga_region_class);
	ida_destroy(&fpga_region_ida);
}

subsys_initcall(fpga_region_init);
module_exit(fpga_region_exit);

MODULE_DESCRIPTION("FPGA Region");
MODULE_AUTHOR("Alan Tull <atull@kernel.org>");
MODULE_LICENSE("GPL v2");

/*************************************************************************/ /*
 avb-streaming

 Copyright (C) 2014-2016,2018 Renesas Electronics Corporation

 License        Dual MIT/GPLv2

 The contents of this file are subject to the MIT license as set out below.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 Alternatively, the contents of this file may be used under the terms of
 the GNU General Public License Version 2 ("GPL") in which case the provisions
 of GPL are applicable instead of those above.

 If you wish to allow use of your version of this file only under the terms of
 GPL, and not to allow others to use your version of this file under the terms
 of the MIT license, indicate your decision by deleting the provisions above
 and replace them with the notice and other provisions required by GPL as set
 out in the file called "GPL-COPYING" included in this distribution. If you do
 not delete the provisions above, a recipient may use your version of this file
 under the terms of either the MIT license or GPL.

 This License is also included in this distribution in the file called
 "MIT-COPYING".

 EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 GPLv2:
 If you wish to use this file under the terms of GPL, following terms are
 effective.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; version 2 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/ /*************************************************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "../drivers/net/ethernet/renesas/ravb.h"
#include "ravb_streaming.h"
#include "ravb_streaming_sysfs.h"

/**
 * streaming private sysfs operations
 */
static struct attribute *stp_dev_basic_attrs[] = {
	NULL,
};

static struct attribute_group stp_dev_basic_group = {
	.attrs = stp_dev_basic_attrs,
};

#define STP_STATS_SHOW_U64(_name) \
static ssize_t stp_stats_##_name##_show(struct device *dev, \
		struct device_attribute *attr, char *page) \
{ \
	int i; \
	u64 tmp = 0; \
	struct streaming_private *stp = dev_get_drvdata(dev); \
	struct hwqueue_info *hwq; \
	struct kobject *stq_kobj; \
\
	for (i = 0, hwq = stp->hwqueueInfoTable; \
			i < RAVB_HWQUEUE_NUM; i++, hwq++) { \
		tmp += hwq->pstats._name; \
		list_for_each_entry(stq_kobj, &hwq->attached->list, entry) \
			tmp += to_stq(stq_kobj)->pstats._name; \
	} \
\
	return snprintf(page, PAGE_SIZE - 1, "%llu\n", tmp); \
}

#define STP_STATS_ATTR_RO(_name) \
struct device_attribute stp_stats_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0444 }, \
	.show	= stp_stats_##_name##_show, \
}

STP_STATS_SHOW_U64(rx_packets);
STP_STATS_SHOW_U64(tx_packets);
STP_STATS_SHOW_U64(rx_bytes);
STP_STATS_SHOW_U64(tx_bytes);
STP_STATS_SHOW_U64(rx_errors);
STP_STATS_SHOW_U64(tx_errors);

static STP_STATS_ATTR_RO(rx_packets);
static STP_STATS_ATTR_RO(tx_packets);
static STP_STATS_ATTR_RO(rx_bytes);
static STP_STATS_ATTR_RO(tx_bytes);
static STP_STATS_ATTR_RO(rx_errors);
static STP_STATS_ATTR_RO(tx_errors);

static struct attribute *stp_dev_stat_attrs[] = {
	&stp_stats_rx_packets_attribute.attr,
	&stp_stats_tx_packets_attribute.attr,
	&stp_stats_rx_bytes_attribute.attr,
	&stp_stats_tx_bytes_attribute.attr,
	&stp_stats_rx_errors_attribute.attr,
	&stp_stats_tx_errors_attribute.attr,
	NULL,
};

static struct attribute_group stp_dev_stat_group = {
	.name  = "statistics",
	.attrs  = stp_dev_stat_attrs,
};

const struct attribute_group *stp_sysfs_groups[] = {
	&stp_dev_basic_group,
	&stp_dev_stat_group,
	NULL,
};

/**
 * hw queue info sysfs operations
 */
#define HWQ_SHOW_BOOL(_name) \
static ssize_t hwq_##_name##_show(struct device *dev, \
			   struct device_attribute *attr, char *page) \
{ \
	struct hwqueue_info *hwq = dev_get_drvdata(dev); \
	return snprintf(page, PAGE_SIZE - 1, "%s\n", \
			(hwq->_name) ? "true" : "false"); \
}

#define HWQ_SHOW_INT(_name) \
static ssize_t hwq_##_name##_show(struct device *dev, \
			   struct device_attribute *attr, char *page) \
{ \
	struct hwqueue_info *hwq = dev_get_drvdata(dev); \
	return snprintf(page, PAGE_SIZE - 1, "%d\n", hwq->_name); \
}

#define HWQ_ATTR_RO(_name) \
struct device_attribute hwq_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0444 }, \
	.show	= hwq_##_name##_show, \
}

#define HWQ_ATTR(_name) \
struct device_attribute hwq_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0664 }, \
	.show	= hwq_##_name##_show, \
	.store	= hwq_##_name##_store, \
}

/* hwq basic attrs */
static ssize_t hwq_state_show(struct device *dev,
			      struct device_attribute *attr,
			      char *page)
{
	struct hwqueue_info *hwq = dev_get_drvdata(dev);

	return snprintf(page, PAGE_SIZE - 1, "%s\n",
			(char *)avb_state_to_str(hwq->state));
}

HWQ_SHOW_INT(index);
HWQ_SHOW_BOOL(tx);
HWQ_SHOW_INT(qno);
HWQ_SHOW_INT(chno);

static HWQ_ATTR_RO(index);
static HWQ_ATTR_RO(state);
static HWQ_ATTR_RO(tx);
static HWQ_ATTR_RO(qno);
static HWQ_ATTR_RO(chno);

static struct attribute *hwq_dev_basic_attrs[] = {
	&hwq_index_attribute.attr,
	&hwq_state_attribute.attr,
	&hwq_tx_attribute.attr,
	&hwq_qno_attribute.attr,
	&hwq_chno_attribute.attr,
	NULL,
};

static struct attribute_group hwq_dev_basic_group = {
	.attrs = hwq_dev_basic_attrs,
};

/* hwq rx attrs */
static ssize_t hwq_streamID_show(struct device *dev,
				 struct device_attribute *attr,
				 char *page)
{
	struct hwqueue_info *hwq = dev_get_drvdata(dev);
	u8 *streamID = hwq->streamID;

	return snprintf(page, PAGE_SIZE - 1,
			"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			streamID[0], streamID[1], streamID[2], streamID[3],
			streamID[4], streamID[5], streamID[6], streamID[7]);
}

static ssize_t hwq_streamID_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hwqueue_info *hwq = dev_get_drvdata(dev);
	int params[8];
	u8 streamID[8];
	int cnt, i;
	int err = -EINVAL;

	cnt = sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		     &params[0], &params[1], &params[2], &params[3],
		     &params[4], &params[5], &params[6], &params[7]);
	if (cnt != 8)
		goto out;

	for (i = 0; i < 8; i++)
		streamID[i] = params[i];

	err = -EPERM;

	if (register_streamID(hwq, streamID))
		goto out;

	return count;

out:
	return err;
}

static HWQ_ATTR(streamID);

static struct attribute *hwq_dev_rx_attrs[] = {
	&hwq_streamID_attribute.attr,
	NULL,
};

static struct attribute_group hwq_dev_rx_group = {
	.attrs = hwq_dev_rx_attrs,
};

/* hwq statistics */
#define HWQ_STATS_SHOW_U64(_name) \
static ssize_t hwq_stats_##_name##_show(struct device *dev, \
		struct device_attribute *attr, char *page) \
{ \
	u64 tmp = 0; \
	struct hwqueue_info *hwq = dev_get_drvdata(dev); \
	struct kobject *stq_kobj; \
\
	tmp += hwq->pstats._name; \
	list_for_each_entry(stq_kobj, &hwq->attached->list, entry) \
		tmp += to_stq(stq_kobj)->pstats._name; \
\
	return snprintf(page, PAGE_SIZE - 1, "%llu\n", tmp); \
}

#define HWQ_STATS_ATTR_RO(_name) \
struct device_attribute hwq_stats_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0444 }, \
	.show	= hwq_stats_##_name##_show, \
}

HWQ_STATS_SHOW_U64(rx_packets);
HWQ_STATS_SHOW_U64(tx_packets);
HWQ_STATS_SHOW_U64(rx_bytes);
HWQ_STATS_SHOW_U64(tx_bytes);
HWQ_STATS_SHOW_U64(rx_errors);
HWQ_STATS_SHOW_U64(tx_errors);

static HWQ_STATS_ATTR_RO(rx_packets);
static HWQ_STATS_ATTR_RO(tx_packets);
static HWQ_STATS_ATTR_RO(rx_bytes);
static HWQ_STATS_ATTR_RO(tx_bytes);
static HWQ_STATS_ATTR_RO(rx_errors);
static HWQ_STATS_ATTR_RO(tx_errors);

static struct attribute *hwq_dev_stat_attrs[] = {
	&hwq_stats_rx_packets_attribute.attr,
	&hwq_stats_tx_packets_attribute.attr,
	&hwq_stats_rx_bytes_attribute.attr,
	&hwq_stats_tx_bytes_attribute.attr,
	&hwq_stats_rx_errors_attribute.attr,
	&hwq_stats_tx_errors_attribute.attr,
	NULL,
};

static struct attribute_group hwq_dev_stat_group = {
	.name  = "statistics",
	.attrs = hwq_dev_stat_attrs,
};

const struct attribute_group *hwq_sysfs_groups_rx[] = {
	&hwq_dev_basic_group,
	&hwq_dev_rx_group,
	&hwq_dev_stat_group,
	NULL,
};

const struct attribute_group *hwq_sysfs_groups_tx[] = {
	&hwq_dev_basic_group,
	&hwq_dev_stat_group,
	NULL,
};

/**
 * stream queue info sysfs operations
 */
struct stq_attribute {
	struct attribute attr;
	ssize_t (*show)(struct stqueue_info *stq,
			struct stq_attribute *attr,
			char *buf);
	ssize_t (*store)(struct stqueue_info *stq,
			 struct stq_attribute *attr,
			 const char *buf,
			 size_t count);
};

#define to_stq_attr(x) container_of(x, struct stq_attribute, attr)

static ssize_t stq_attr_show(struct kobject *kobj,
			     struct attribute *attr,
			     char *buf)
{
	struct stq_attribute *attribute = to_stq_attr(attr);

	if (!attribute->show)
		return -EIO;

	return attribute->show(to_stq(kobj), attribute, buf);
}

static ssize_t stq_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t len)
{
	struct stq_attribute *attribute = to_stq_attr(attr);

	if (!attribute->store)
		return -EIO;

	return attribute->store(to_stq(kobj), attribute, buf, len);
}

const struct sysfs_ops stq_sysfs_ops = {
	.show = stq_attr_show,
	.store = stq_attr_store,
};

#define STQ_SHOW_BOOL(_name) \
static ssize_t stq_##_name##_show(struct stqueue_info *stq, \
			   struct stq_attribute *attr, char *page) \
{ \
	return snprintf(page, PAGE_SIZE - 1, "%s\n", \
			(stq->_name) ? "true" : "false"); \
}

#define STQ_SHOW_INT(_name) \
static ssize_t stq_##_name##_show(struct stqueue_info *stq, \
			   struct stq_attribute *attr, char *page) \
{ \
	return snprintf(page, PAGE_SIZE - 1, "%d\n", stq->_name); \
}

static ssize_t stq_state_show(struct stqueue_info *stq,
			      struct stq_attribute *attr,
			      char *page)
{
	return snprintf(page, PAGE_SIZE - 1, "%s\n",
			(char *)avb_state_to_str(stq->state));
}

STQ_SHOW_INT(index);
STQ_SHOW_INT(qno);

static ssize_t stq_cbs_params_show(struct stqueue_info *stq,
				   struct stq_attribute *attr,
				   char *page)
{
	return snprintf(page, PAGE_SIZE - 1, "0x%08x +0x%04x -0x%04x\n",
			stq->cbs.bandwidthFraction,
			stq->cbs.idleSlope,
			stq->cbs.sendSlope);
}

#define STQ_ATTR_RO(_name) \
struct stq_attribute stq_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0444 }, \
	.show	= stq_##_name##_show, \
}

#define STQ_ATTR(_name) \
struct stq_attribute stq_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0664 }, \
	.show	= stq_##_name##_show, \
	.store	= stq_##_name##_store, \
}

static STQ_ATTR_RO(index);
static STQ_ATTR_RO(state);
static STQ_ATTR_RO(qno);
/* for Tx stream */
static STQ_ATTR_RO(cbs_params);

struct attribute *stq_default_attrs_rx[] = {
	&stq_index_attribute.attr,
	&stq_state_attribute.attr,
	&stq_qno_attribute.attr,
	NULL,
};

struct attribute *stq_default_attrs_tx[] = {
	&stq_index_attribute.attr,
	&stq_state_attribute.attr,
	&stq_qno_attribute.attr,
	&stq_cbs_params_attribute.attr,
	NULL,
};

#define STQ_STATS_SHOW_U64(_name) \
static ssize_t stq_stats_##_name##_show(struct stqueue_info *stq, \
			   struct stq_attribute *attr, char *page) \
{ \
	return snprintf(page, PAGE_SIZE - 1, "%llu\n", stq->pstats._name); \
}

#define STQ_STATS_ATTR_RO(_name) \
struct stq_attribute stq_stats_##_name##_attribute = { \
	.attr	= { .name = __stringify(_name), .mode = 0444 }, \
	.show	= stq_stats_##_name##_show, \
}

STQ_STATS_SHOW_U64(rx_packets);
STQ_STATS_SHOW_U64(tx_packets);
STQ_STATS_SHOW_U64(rx_bytes);
STQ_STATS_SHOW_U64(tx_bytes);
STQ_STATS_SHOW_U64(rx_errors);
STQ_STATS_SHOW_U64(tx_errors);

static STQ_STATS_ATTR_RO(rx_packets);
static STQ_STATS_ATTR_RO(tx_packets);
static STQ_STATS_ATTR_RO(rx_bytes);
static STQ_STATS_ATTR_RO(tx_bytes);
static STQ_STATS_ATTR_RO(rx_errors);
static STQ_STATS_ATTR_RO(tx_errors);

static struct attribute *stq_dev_stat_attrs[] = {
	&stq_stats_rx_packets_attribute.attr,
	&stq_stats_tx_packets_attribute.attr,
	&stq_stats_rx_bytes_attribute.attr,
	&stq_stats_tx_bytes_attribute.attr,
	&stq_stats_rx_errors_attribute.attr,
	&stq_stats_tx_errors_attribute.attr,
	NULL,
};

struct attribute_group stq_dev_stat_group = {
	.name  = "statistics",
	.attrs = stq_dev_stat_attrs,
};

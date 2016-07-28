/*************************************************************************/ /*
 avb-streaming

 Copyright (C) 2014-2016 Renesas Electronics Corporation

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

#include <linux/interrupt.h>

#include "../drivers/net/ethernet/renesas/ravb.h"
#include "ravb_streaming.h"
#include "ravb_streaming_version.h"
#include "ravb_streaming_avbtool.h"

/**
 * correct statistics counter operations
 */
static void correct_pstats_stp(struct stqueue_info *stq,
		struct packet_stats *pstats)
{
	struct streaming_private *stp = stp_ptr;
	struct hwqueue_info *hwq;
	struct kobject *stq_kobj;
	int i;

	memset(pstats, 0, sizeof(*pstats));

	for (i = 0, hwq = stp->hwqueueInfoTable;
			i < RAVB_HWQUEUE_NUM; i++, hwq++) {

		pstats->rx_packets += hwq->pstats.rx_packets;
		pstats->tx_packets += hwq->pstats.tx_packets;
		pstats->rx_bytes += hwq->pstats.rx_bytes;
		pstats->tx_bytes += hwq->pstats.tx_bytes;
		pstats->rx_errors += hwq->pstats.rx_errors;
		pstats->tx_errors += hwq->pstats.tx_errors;

		list_for_each_entry(stq_kobj, &(hwq->attached->list), entry) {
			stq = to_stq(stq_kobj);
			pstats->rx_packets += stq->pstats.rx_packets;
			pstats->tx_packets += stq->pstats.tx_packets;
			pstats->rx_bytes += stq->pstats.rx_bytes;
			pstats->tx_bytes += stq->pstats.tx_bytes;
			pstats->rx_errors += stq->pstats.rx_errors;
			pstats->tx_errors += stq->pstats.tx_errors;
		}
	}
}

static void correct_pstats(struct stqueue_info *stq,
		struct packet_stats *pstats)
{
	if (!stq)
		correct_pstats_stp(NULL, pstats);
	else
		memcpy(pstats, &(stq->pstats), sizeof(*pstats));
}

static void correct_dstats_stp(struct stqueue_info *stq,
	struct driver_stats *dstats)
{
	struct streaming_private *stp = stp_ptr;
	struct hwqueue_info *hwq;
	struct kobject *stq_kobj;
	int i;

	memset(dstats, 0, sizeof(*dstats));

	for (i = 0, hwq = stp->hwqueueInfoTable;
		i < RAVB_HWQUEUE_NUM; i++, hwq++) {

		dstats->rx_interrupts += hwq->dstats.rx_interrupts;
		dstats->tx_interrupts += hwq->dstats.tx_interrupts;
		dstats->rx_current += hwq->dstats.rx_current;
		dstats->tx_current += hwq->dstats.tx_current;
		dstats->rx_dirty += hwq->dstats.rx_dirty;
		dstats->tx_dirty += hwq->dstats.tx_dirty;

		list_for_each_entry(stq_kobj, &(hwq->attached->list), entry) {
			stq = to_stq(stq_kobj);
			dstats->rx_entry_wait += stq->dstats.rx_entry_wait;
			dstats->tx_entry_wait += stq->dstats.tx_entry_wait;
			dstats->rx_entry_complete += stq->dstats.rx_entry_complete;
			dstats->tx_entry_complete += stq->dstats.tx_entry_complete;
		}
	}
}

static void correct_dstats(struct stqueue_info *stq,
	struct driver_stats *dstats)
{
	struct hwqueue_info *hwq;

	if (!stq)
		correct_dstats_stp(NULL, dstats);
	else {
		hwq = stq->hwq;
		dstats->rx_interrupts = hwq->dstats.rx_interrupts;
		dstats->tx_interrupts = hwq->dstats.tx_interrupts;
		dstats->rx_current = hwq->dstats.rx_current;
		dstats->tx_current = hwq->dstats.tx_current;
		dstats->rx_dirty = hwq->dstats.rx_dirty;
		dstats->tx_dirty = hwq->dstats.tx_dirty;
		dstats->rx_entry_wait = stq->dstats.rx_entry_wait;
		dstats->tx_entry_wait = stq->dstats.tx_entry_wait;
		dstats->rx_entry_complete = stq->dstats.rx_entry_complete;
		dstats->tx_entry_complete = stq->dstats.tx_entry_complete;
	}
}

/**
 * AVB tool commands
 */
static const char ravb_avbtool_gstrings_stats[][EAVB_GSTRING_LEN] = {
	/* packet statistics */
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_errors",
	"tx_errors",
	/* driver statistics */
	"rx_interrupts",
	"tx_interrupts",
	"rx_current",
	"tx_current",
	"rx_dirty",
	"tx_dirty",
	"rx_entry_wait",
	"tx_entry_wait",
	"rx_entry_complete",
	"tx_entry_complete",
};
#define EAVB_AVBTOOL_STATS_LEN	ARRAY_SIZE(ravb_avbtool_gstrings_stats)

static long ravb_avbtool_get_drvinfo(struct file *file, unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_drvinfo info = {
		.driver = DRIVER,
		.version = DRIVER_VERSION,
		.n_stats = EAVB_AVBTOOL_STATS_LEN,
	};

	pr_debug("get_drvinfo:\n");

	strlcpy(info.bus_info, dev_name(ndev->dev.parent),
			sizeof(info.bus_info));

	if (copy_to_user(useraddr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long ravb_avbtool_get_ringparam(struct file *file, unsigned long parm)
{
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_ringparam ringparam = {
		.rx_max_pending = RAVB_RINGSIZE,
		.rx_pending = RAVB_RINGSIZE,
		.tx_max_pending = RAVB_RINGSIZE,
		.tx_pending = RAVB_RINGSIZE,
	};

	pr_debug("get_ringparam:\n");

	if (copy_to_user(useraddr, &ringparam, sizeof(ringparam)))
		return -EFAULT;

	return 0;
}

static long ravb_avbtool_get_channels(struct file *file, unsigned long parm)
{
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_channels channels = {
		.max_rx = RAVB_HWQUEUE_RXNUM,
		.max_tx = RAVB_HWQUEUE_TXNUM,
		.rx_count = RAVB_HWQUEUE_RXNUM,
		.tx_count = RAVB_HWQUEUE_TXNUM,
	};

	pr_debug("get_channels:\n");

	if (copy_to_user(useraddr, &channels, sizeof(channels)))
		return -EFAULT;

	return 0;
}

static long ravb_avbtool_get_sset_count(struct file *file, unsigned long parm)
{
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_sset_info info;
	u64 sset_mask;
	u32 *info_buf = NULL;
	int i, idx = 0;
	unsigned long n_bits = 0;
	long err;

	pr_debug("get_sset_count:\n");

	if (copy_from_user(&info, useraddr, sizeof(info)))
		return -EFAULT;

	/* store copy of mask, because we zero struct later on */
	sset_mask = info.sset_mask;
	if (!sset_mask)
		return 0;

	/* calculate size of return buffer */
	n_bits = hweight64(sset_mask);

	info_buf = kzalloc((n_bits * sizeof(u32)), GFP_USER);
	if (!info_buf)
		return -ENOMEM;

	/**
	 * fill return buffer based on input bitmask and successful
	 * get_sset_count return
	 */
	for (i = 0; i < 64; i++) {
		if (!(sset_mask & (1ULL << i)))
			continue;

		switch (i) {
		case EAVB_SS_STATS:
			info.sset_mask |= (1ULL << i);
			info_buf[idx++] = EAVB_AVBTOOL_STATS_LEN;
			break;
		default:
			break;
		}
	}

	err = -EFAULT;
	if (copy_to_user(useraddr, &info, sizeof(info)))
		goto out;
	useraddr += offsetof(struct eavb_avbtool_sset_info, data);
	if (copy_to_user(useraddr, info_buf, idx * sizeof(u32)))
		goto out;

	err = 0;

out:
	kfree(info_buf);
	return err;
}

static long ravb_avbtool_get_strings(struct file *file, unsigned long parm)
{
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_gstrings gstrings;
	u8 *data;
	long err = 0;

	pr_debug("get_strings:\n");

	if (copy_from_user(&gstrings, useraddr, sizeof(gstrings)))
		return -EFAULT;

	switch (gstrings.string_set) {
	case EAVB_SS_STATS:
		gstrings.len = EAVB_AVBTOOL_STATS_LEN;
		break;
	default:
		return -EOPNOTSUPP;
	}

	data = kmalloc(gstrings.len * EAVB_GSTRING_LEN, GFP_USER);
	if (!data)
		return -ENOMEM;

	switch (gstrings.string_set) {
	case EAVB_SS_STATS:
		memcpy(data, *ravb_avbtool_gstrings_stats,
				sizeof(ravb_avbtool_gstrings_stats));
		break;
	}

	err = -EFAULT;
	if (copy_to_user(useraddr, &gstrings, sizeof(gstrings)))
		goto out;
	useraddr += offsetof(struct eavb_avbtool_gstrings, data);
	if (copy_to_user(useraddr, data, gstrings.len * EAVB_GSTRING_LEN))
		goto out;

	err = 0;

out:
	kfree(data);
	return err;
}

static long ravb_avbtool_get_stats(struct file *file, unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq;
	void __user *useraddr = (void __user *)parm;
	struct eavb_avbtool_stats stats;
	struct packet_stats pstats;
	struct driver_stats dstats;
	u64 *data;
	int i = 0, n_stats = EAVB_AVBTOOL_STATS_LEN;
	long err = 0;

	if (!kif)
		stq = NULL;
	else
		stq = kif->handle;

	pr_debug("get_stats: %s\n", (stq) ? stq_name(stq) : stp_name(stp));

	stats.n_stats = n_stats;
	data = kmalloc((n_stats * sizeof(u64)), GFP_USER);
	if (!data)
		return -ENOMEM;

	/* packet statistics */
	correct_pstats(stq, &pstats);
	data[i++] = pstats.rx_packets;
	data[i++] = pstats.tx_packets;
	data[i++] = pstats.rx_bytes;
	data[i++] = pstats.tx_bytes;
	data[i++] = pstats.rx_errors;
	data[i++] = pstats.tx_errors;

	/* driver statistics */
	correct_dstats(stq, &dstats);
	data[i++] = dstats.rx_interrupts;
	data[i++] = dstats.tx_interrupts;
	data[i++] = dstats.rx_current;
	data[i++] = dstats.tx_current;
	data[i++] = dstats.rx_dirty;
	data[i++] = dstats.tx_dirty;
	data[i++] = dstats.rx_entry_wait;
	data[i++] = dstats.tx_entry_wait;
	data[i++] = dstats.rx_entry_complete;
	data[i++] = dstats.tx_entry_complete;

	err = -EFAULT;
	if (copy_to_user(useraddr, &stats, sizeof(stats)))
		goto out;
	useraddr += offsetof(struct eavb_avbtool_stats, data);
	if (copy_to_user(useraddr, data, stats.n_stats * sizeof(u64)))
		goto out;
	err = 0;

 out:
	kfree(data);
	return err;
}

long ravb_streaming_ioctl_avbtool(struct file *file,
		unsigned int cmd, unsigned long parm)
{
	switch (cmd) {
	case EAVB_GDRVINFO:
		return ravb_avbtool_get_drvinfo(file, parm);
	case EAVB_GRINGPARAM:
		return ravb_avbtool_get_ringparam(file, parm);
	case EAVB_GCHANNELS:
		return ravb_avbtool_get_channels(file, parm);
	case EAVB_GSSET_INFO:
		return ravb_avbtool_get_sset_count(file, parm);
	case EAVB_GSTRINGS:
		return ravb_avbtool_get_strings(file, parm);
	case EAVB_GSTATS:
		return ravb_avbtool_get_stats(file, parm);
	default:
		return -EINVAL;
	}
}

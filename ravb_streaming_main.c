/*************************************************************************/ /*
 avb-streaming

 Copyright (C) 2014-2018 Renesas Electronics Corporation

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

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME "/" fmt

#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mdio-bitbang.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/cache.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sh_eth.h>
#include <linux/hrtimer.h>

#include "../drivers/net/ethernet/renesas/ravb.h"
#include "ravb_streaming.h"
#include "ravb_streaming_avbtool.h"
#include "ravb_streaming_sysfs.h"

#define CREATE_TRACE_POINTS
#include "ravb_streaming_trace.h"

#define DEBUG_AVB_CACHESYNC (0)

#define AVB_CTRL_MINOR (127)
#define AVB_MINOR_RANGE (AVB_CTRL_MINOR + 1)

/**
 * global parameters
 */
static int major;
module_param(major, int, 0440);

static char *interface;
module_param(interface, charp, 0440);

static int irq_timeout_usec_tx0;
module_param(irq_timeout_usec_tx0, int, 0660);

static int irq_timeout_usec_tx1;
module_param(irq_timeout_usec_tx1, int, 0660);

static int irq_timeout_usec_rx;
module_param(irq_timeout_usec_rx, int, 0660);

static int irq_coalesce_frame_tx;
module_param(irq_coalesce_frame_tx, int, 0660);

static int irq_coalesce_frame_rx;
module_param(irq_coalesce_frame_rx, int, 0660);

struct streaming_private *stp_ptr;
static struct kmem_cache *streaming_entry_cache;

/**
 * utilities
 */
#define avb_spin_lock(lock, index, stqno) \
	do { \
		spin_lock(lock); \
		trace_avb_spin_lock(index, stqno); \
	} while (0)

#define avb_spin_unlock(lock, index, stqno) \
	do { \
		trace_avb_spin_unlock(index, stqno); \
		spin_unlock(lock); \
	} while (0)

#define avb_spin_lock_irqsave(lock, flags, index, stqno) \
	do { \
		spin_lock_irqsave(lock, flags); \
		trace_avb_spin_lock_irqsave(index, stqno); \
	} while (0)

#define avb_spin_unlock_irqrestore(lock, flags, index, stqno) \
	do { \
		trace_avb_spin_unlock_irqrestore(index, stqno); \
		spin_unlock_irqrestore(lock, flags); \
	} while (0)

#define avb_wake_up_interruptible(waitevent, index, stqno) \
	do { \
		trace_avb_wait_wakeup(index, stqno); \
		wake_up_interruptible(waitevent); \
	} while (0)

#define avb_wait_event_interruptible(waitevent, cond, index, stqno) \
	({ \
		trace_avb_wait_sleep(index, stqno); \
		wait_event_interruptible(waitevent, cond); \
	 })

#define avb_down(sem, index, stqno) \
	do { \
		down(sem); \
		trace_avb_sem_take(index, stqno); \
	} while (0)

#define avb_up(sem, index, stqno) \
	do { \
		trace_avb_sem_give(index, stqno); \
		up(sem); \
	} while (0)

static int ravb_wait_reg(struct net_device *ndev,
			 enum ravb_reg reg,
			 u32 mask,
			 u32 value)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if ((ravb_read(ndev, reg) & mask) == value)
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

static inline bool uncached_access(struct stqueue_info *stq)
{
	return !!(stq->flags & O_DSYNC);
}

static inline bool is_readable_count(struct stqueue_info *stq,
				     unsigned int count)
{
	if (stq->blockmode == EAVB_BLOCK_WAITALL)
		return (stq->entrynum.completed >= count)  ? true : false;
	else
		return (stq->entrynum.completed > 0) ? true : false;
}

static inline bool is_readable(struct stqueue_info *stq)
{
	return (stq->entrynum.completed > 0) ? true : false;
}

static inline bool is_writeble(struct stqueue_info *stq)
{
	return (stq->entrynum.accepted < RAVB_ENTRY_THRETH) ? true : false;
}

const char *avb_state_to_str(enum AVB_STATE state)
{
	switch (state) {
	case AVB_STATE_SLEEP:        return "sleep";
	case AVB_STATE_IDLE:         return "idle";
	case AVB_STATE_ACTIVE:       return "active";
	case AVB_STATE_WAITCOMPLETE: return "waitcomplete";
	default: return "invalid";
	}
}

static inline void hwq_sequencer(struct hwqueue_info *hwq,
				 enum AVB_STATE state)
{
	if (hwq->state != state) {
		trace_avb_hwq_state(hwq->index, state);
		hwq->state = state;
	}
}

static inline void stq_sequencer(struct stqueue_info *stq,
				 enum AVB_STATE state)
{
	if (stq->state != state) {
		trace_avb_stq_state(stq->hwq->index, stq->qno, state);
		stq->state = state;
	}
}

static inline u32 hwq_event_irq(struct hwqueue_info *hwq,
				enum AVB_EVENT event,
				u32 param)
{
	u32 events;

	events = hwq->pendingEvents;
	trace_avb_event(hwq->index, hwq->state, event, events);

	switch (event) {
	case AVB_EVENT_CLEAR:
		hwq->pendingEvents = 0;
		break;
	case AVB_EVENT_ATTACH:
	case AVB_EVENT_DETACH:
	case AVB_EVENT_RXINT:
	case AVB_EVENT_TXINT:
	case AVB_EVENT_UNLOAD:
	case AVB_EVENT_TIMEOUT:
		if (!(events & event)) {
			hwq->pendingEvents |= event;
			avb_wake_up_interruptible(&hwq->waitEvent,
						  hwq->index,
						  -1);
		}
		break;
	default:
		WARN(1, "context error: invalid event type\n");
		break;
	}

	return events;
}

static inline u32 hwq_event(struct hwqueue_info *hwq,
			    enum AVB_EVENT event,
			    u32 param)
{
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct ravb_private *priv = netdev_priv(ndev);
	unsigned long flags;
	u32 events;

	avb_spin_lock_irqsave(&priv->lock, flags, hwq->index, -1);

	events = hwq_event_irq(hwq, event, param);

	avb_spin_unlock_irqrestore(&priv->lock, flags, hwq->index, -1);

	return events;
}

static inline u32 hwq_event_clear(struct hwqueue_info *hwq)
{
	return hwq_event(hwq, AVB_EVENT_CLEAR, -1);
}

static void hwq_try_to_start_irq_timeout_timer(struct hwqueue_info *hwq)
{
	int irq_timeout_usec;

	if (hwq->tx) {
		if (!hwq->index)
			irq_timeout_usec = irq_timeout_usec_tx0;
		else
			irq_timeout_usec = irq_timeout_usec_tx1;
	} else {
		irq_timeout_usec = irq_timeout_usec_rx;
	}

	if (irq_timeout_usec)
		hrtimer_start(&hwq->timer,
			      ns_to_ktime(irq_timeout_usec * NSEC_PER_USEC),
			      HRTIMER_MODE_REL);
}

/**
 * streaming entry operations
 */
static struct stream_entry *get_streaming_entry(void)
{
	struct stream_entry *e;

	e = kmem_cache_alloc(streaming_entry_cache, GFP_KERNEL);
	if (!e)
		return NULL;

	INIT_LIST_HEAD(&e->list);
	memset(e->descs, 0, sizeof(e->descs));
	e->total_bytes = 0;
	e->errors = 0;

	trace_avb_entry_get(e);

	return e;
}

static void put_streaming_entry(struct stream_entry *e)
{
	trace_avb_entry_put(e);

	list_del(&e->list);
	kmem_cache_free(streaming_entry_cache, e);
}

static void cachesync_streaming_entry(struct stream_entry *e)
{
	struct eavb_entryvec *evec;
	int i;
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;

	evec = e->msg.vec;

	if (e->stq->hwq->tx) {
		for (i = 0; i < e->vecsize; i++, evec++)
			dma_sync_single_for_device(pdev_dev,
						   evec->base,
						   evec->len,
						   DMA_TO_DEVICE);
	} else {
		for (i = 0; i < e->vecsize; i++, evec++)
			dma_sync_single_for_cpu(pdev_dev,
						evec->base,
						evec->len,
						DMA_FROM_DEVICE);
	}
}

/**
 * userpage operations
 */
static struct ravb_user_page *get_userpage(void)
{
	struct page *page;
	dma_addr_t page_dma;
	struct ravb_user_page *userpage;
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;

	userpage = vzalloc(sizeof(*userpage));
	if (unlikely(!userpage))
		goto err_alloc;
	page = alloc_page(GFP_KERNEL | GFP_DMA | __GFP_COLD);
	if (unlikely(!page))
		goto err_allocpage;
	page_dma = dma_map_page(pdev_dev, page, 0, PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(pdev_dev, page_dma))
		goto err_map;

	INIT_LIST_HEAD(&userpage->list);
	userpage->page = page;
	userpage->page_dma = page_dma;

	return userpage;

err_map:
	put_page(page);
err_allocpage:
	vfree(userpage);
err_alloc:
	return NULL;
}

static void put_userpage(struct ravb_user_page *userpage)
{
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;

	dma_unmap_page(pdev_dev,
		       userpage->page_dma,
		       PAGE_SIZE,
		       DMA_FROM_DEVICE);
	put_page(userpage->page);
	list_del(&userpage->list);
	vfree(userpage);
}

static struct ravb_user_page *lookup_userpage(struct stqueue_info *stq,
					      dma_addr_t physaddr)
{
	struct streaming_private *stp = stp_ptr;
	struct ravb_user_page *userpage;

	if (list_empty(&stp->userpages) &&
	    (stq && list_empty(&stq->userpages)))
		return NULL;

	if (stq)
		/* search a userpage from stq userpages */
		list_for_each_entry(userpage, &stq->userpages, list)
			if (physaddr == userpage->page_dma)
				break;

	/* not found a userpage in stq userpages */
	if (!stq || &userpage->list == &stq->userpages) {
		/* search a userpage from stp userpages */
		list_for_each_entry(userpage, &stp->userpages, list)
			if (physaddr == userpage->page_dma)
				break;

		if (&userpage->list == &stp->userpages)
			return NULL;
	}

	return userpage;
}

/**
 * descriptor encode/decode
 */
static void clear_desc(struct hwqueue_info *hwq)
{
	struct ravb_desc *desc;
	int j;

	/* init all descriptor type */
	for (j = 0, desc = hwq->ring; j < hwq->ringsize; j++, desc++)
		desc->die_dt = (hwq->tx) ? DT_FEMPTY : DT_FSINGLE;
	/* link tail desc to head desc */
	desc->die_dt = DT_LINKFIX;
	desc->dptr = cpu_to_le32((u32)hwq->ring_dma);

	hwq->remain = hwq->ringsize;
	hwq->curr = 0;

	hwq->dstats.rx_dirty = hwq->dstats.rx_current;
	hwq->dstats.tx_dirty = hwq->dstats.tx_current;
}

static void *get_desc(struct hwqueue_info *hwq, dma_addr_t *desc_dma)
{
	struct ravb_desc *desc;

	desc = hwq->ring + hwq->curr;
	*desc_dma = hwq->ring_dma + (dma_addr_t)(sizeof(*desc) * hwq->curr);

	hwq->remain--;
	hwq->curr = (hwq->curr + 1) % hwq->ringsize;
	/* Keep lowest free descriptor use - since driver load */
	hwq->minremain = min_t(s32, hwq->minremain, hwq->remain);

	return desc;
}

static void put_desc(struct hwqueue_info *hwq, void *buf)
{
	struct ravb_desc *desc = buf;

	if (desc)
		hwq->remain++;
}

static int desc_pre_encode_rx(struct stream_entry *e)
{
	struct ravb_rx_desc *desc;
	struct eavb_entryvec *evec;
	int i;

	evec = e->msg.vec;

	for (i = 0; i < EAVB_ENTRYVECNUM; i++, evec++) {
		if (!evec->len)
			break;

		desc = (struct ravb_rx_desc *)&e->pre_enc[i];
		desc->ds_cc = cpu_to_le16(evec->len);
		desc->msc = 0;
		desc->dptr = cpu_to_le32(evec->base);

		/* DT change timing should be latest */
		if (!evec->base)
			desc->die_dt = DT_FEMPTY_ND;
		else
			desc->die_dt = DT_FEMPTY;
	}

	return i;
}

static int desc_pre_encode_tx(struct stream_entry *e)
{
	struct ravb_tx_desc *desc;
	struct eavb_entryvec *evec;
	int i;

	evec = e->msg.vec;

	for (i = 0; i < EAVB_ENTRYVECNUM; i++, evec++) {
		if (!evec->base && !evec->len)
			break;

		desc = (struct ravb_tx_desc *)&e->pre_enc[i];
		desc->ds_tagl = cpu_to_le16(evec->len);
		desc->tagh_tsr = 0;
		desc->dptr = cpu_to_le32(evec->base);
		desc->die_dt = ((i == 0) ? DT_FSTART : DT_FMID);
	}

	if (i)
		e->pre_enc[i - 1].die_dt = ((i == 1) ? DT_FSINGLE : DT_FEND);

	return i;
}

static int desc_pre_encode(struct stream_entry *e, bool tx)
{
	if (tx)
		return desc_pre_encode_tx(e);
	else
		return desc_pre_encode_rx(e);
}

/* Caller must check remain */
static void desc_copy(struct hwqueue_info *hwq, struct stream_entry *e)
{
	struct ravb_desc *desc;
	dma_addr_t desc_dma;
	int i;
#if DEBUG_AVB_CACHESYNC
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;
#endif
	int irq_coalesce_frame = (hwq->tx) ?
				 irq_coalesce_frame_tx : irq_coalesce_frame_rx;
	u64 dstats_current = 0;

	for (i = 0; i < e->vecsize; i++) {
		desc = get_desc(hwq, &desc_dma);

		if (hwq->irq_coalesce_frame_count) {
			hwq->irq_coalesce_frame_count--;
		} else {
			e->pre_enc[i].die_dt |= DESC_DIE_DPF_01;
			hwq->irq_coalesce_frame_count = irq_coalesce_frame;
		}

		*desc = e->pre_enc[i];

#if DEBUG_AVB_CACHESYNC
		dma_sync_single_for_device(pdev_dev,
					   desc_dma,
					   sizeof(*desc),
					   DMA_TO_DEVICE);
#endif

		dstats_current++;

		e->descs[i] = desc;
		e->dma_descs[i] = desc_dma;

		trace_avb_desc(hwq->index,
			       e->stq->qno,
			       e,
			       desc,
			       desc->die_dt,
			       desc->dptr,
			       desc->ds,
			       1,
			       hwq->tx);
	}

	if (hwq->tx)
		hwq->dstats.tx_current += dstats_current;
	else
		hwq->dstats.rx_current += dstats_current;
}

static bool desc_decode_rx(struct hwqueue_info *hwq, struct stream_entry *e)
{
	struct ravb_rx_desc *desc;
	struct eavb_entryvec *evec;
	bool progress = true;
	int i;
#if DEBUG_AVB_CACHESYNC
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;
#endif

	/* TODO need error handling */

	evec = e->msg.vec;
	for (i = 0; i < e->vecsize; i++, evec++) {
		desc = (struct ravb_rx_desc *)e->descs[i];
		if (!desc)
			continue;

#if DEBUG_AVB_CACHESYNC
		dma_sync_single_for_cpu(pdev_dev, e->dma_descs[i],
					sizeof(*desc), DMA_FROM_DEVICE);
#endif

		switch (desc->die_dt & 0xf0) {
		case DT_FSINGLE:
		case DT_FEND:
			progress = false;
			/* fallthrough */
		case DT_FSTART:
		case DT_FMID:
			put_desc(hwq, desc);
			hwq->dstats.rx_dirty++;
			break;
		default:
			continue;
		}

		evec->len = desc->ds_cc & RX_DS;
		evec->base = desc->dptr;

		if (desc->msc & (MSC_CRC | MSC_RFE | MSC_RTSF | MSC_RTLF | MSC_CEEF))
			e->errors++;
		e->total_bytes += evec->len;

		e->descs[i] = NULL;
		e->dma_descs[i] = 0;

		trace_avb_desc_decode_rx(hwq->index,
					 e->stq->qno,
					 e,
					 desc,
					 desc->die_dt,
					 desc->dptr,
					 desc->ds_cc);
	}

	/* descriptor sync error */
	if (progress) {
		evec = e->msg.vec;
		for (i = 0; i < e->vecsize; i++, evec++)
			if (e->descs[i])
				break;
		if (i == e->vecsize) {
			/* TODO descriptor sync recovery */
			pr_warn("desc/de/rx: force terminate entry\n");
			progress = false;
		}
	}

	return progress;
}

static bool desc_decode_tx(struct hwqueue_info *hwq, struct stream_entry *e)
{
	struct ravb_desc *desc;
	struct eavb_entryvec *evec;
	int i;
#if DEBUG_AVB_CACHESYNC
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct device *pdev_dev = ndev->dev.parent;
#endif

	evec = e->msg.vec;
	for (i = 0; i < e->vecsize; i++, evec++) {
		desc = e->descs[i];
		if (!desc)
			continue;

#if DEBUG_AVB_CACHESYNC
		dma_sync_single_for_cpu(pdev_dev, e->dma_descs[i],
					sizeof(*desc), DMA_FROM_DEVICE);
#endif

		if ((desc->die_dt & 0xf0) != DT_FEMPTY)
			break;

		put_desc(hwq, desc);
		hwq->dstats.tx_dirty++;

		e->total_bytes += desc->ds;
		e->descs[i] = NULL;
		e->dma_descs[i] = 0;

		trace_avb_desc_decode_tx(hwq->index,
					 e->stq->qno,
					 e,
					 desc,
					 desc->die_dt,
					 desc->dptr,
					 desc->ds);
	}

	return (i == e->vecsize) ? false : true;
}

static bool desc_decode(struct hwqueue_info *hwq, struct stream_entry *e)
{
	if (hwq->tx)
		return desc_decode_tx(hwq, e);
	else
		return desc_decode_rx(hwq, e);
}

/**
 * calcuration CBS parameter functions
 */
static void update_cbs_param(bool add,
			     enum eavb_streamclass class,
			     struct eavb_cbsparam *cbs,
			     bool apply)
{
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct eavb_cbsparam *cbsTotal;
	u32 maxFrameSize = 2012 * 8; /* bit */
	u32 maxInterferenceSize =
		((class == EAVB_CLASSA) ? 2100 : 4200) * 8; /* bit */
	u32 offset;

	cbsTotal = &stp->cbsInfo.param[class];

	if (add) {
		cbsTotal->bandwidthFraction += cbs->bandwidthFraction;
		stp->cbsInfo.bandwidthFraction += cbs->bandwidthFraction;
	} else {
		cbsTotal->bandwidthFraction -= cbs->bandwidthFraction;
		stp->cbsInfo.bandwidthFraction -= cbs->bandwidthFraction;
	}

	cbsTotal->idleSlope = cbsTotal->bandwidthFraction >> 16;

	if (!cbsTotal->idleSlope)
		cbsTotal->idleSlope = 1;

	cbsTotal->sendSlope = (U16_MAX - cbsTotal->idleSlope) * -1;
	cbsTotal->hiCredit = maxInterferenceSize * cbsTotal->idleSlope;
	cbsTotal->loCredit = maxFrameSize * cbsTotal->sendSlope;

	if (apply) {
		offset = class * sizeof(u32);
		ravb_write(ndev, cbsTotal->idleSlope, CIVR0 + offset);
		ravb_write(ndev, cbsTotal->sendSlope, CDVR0 + offset);
		ravb_write(ndev, cbsTotal->hiCredit, CUL0 + offset);
		ravb_write(ndev, cbsTotal->loCredit, CLL0 + offset);
	}
}

static inline void register_cbs_param(enum eavb_streamclass class,
				      struct eavb_cbsparam *cbs)
{
	update_cbs_param(true, class, cbs, true);
}

static inline void unregister_cbs_param(enum eavb_streamclass class,
					struct eavb_cbsparam *cbs,
					bool apply)
{
	update_cbs_param(false, class, cbs, apply);
}

static int separation_filter_initialise(struct net_device *ndev)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 csr_ops;

	csr_ops = ravb_read(ndev, CSR) & CSR_OPS;
	if (csr_ops == CSR_OPS_CONFIG) {
		/* Separation Filter Offset */
		ravb_write(ndev, 0x16, SFO);
		/* Separation Filter Mask */
		ravb_write(ndev, 0xffffffff, SFM0);
		ravb_write(ndev, 0xffffffff, SFM1);
	} else if (csr_ops == CSR_OPS_OPERATION) {
		if (priv->chip_id == RCAR_GEN2)
			return -EBUSY;

		/* Separation Filter Offset(OPERATION MODE) */
		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;

		ravb_write(ndev, 0x16, SFV0);
		ravb_write(ndev, 0x16, SFV1);
		ravb_write(ndev, SFL_LC_SFO, SFL);

		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;

		/* Separation Filter Mask(OPERATION MODE) */
		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;

		ravb_write(ndev, 0xffffffff, SFV0);
		ravb_write(ndev, 0xffffffff, SFV1);
		ravb_write(ndev, SFL_LC_SFM, SFL);

		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;
	}

	return 0;
}

int register_streamID(struct hwqueue_info *hwq, u8 streamID[8])
{
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	u32 *valp;
	int index;
	int offset;
	u32 csr_ops;
	u32 val[2];

	csr_ops = ravb_read(ndev, CSR) & CSR_OPS;

	/* should be CONFIG or OPERATION */
	if (!(csr_ops & (CSR_OPS_CONFIG | CSR_OPS_OPERATION)))
		return -EPERM;

	/* setting of separation filter */
	valp = (u32 *)streamID;
	index = hwq->chno - RAVB_HWQUEUE_RESERVEDNUM;
	offset = index * 2 * sizeof(u32);

	if (csr_ops == CSR_OPS_CONFIG) {
		ravb_write(ndev, *(valp + 0), SFP0 + offset);
		ravb_write(ndev, *(valp + 1), SFP1 + offset);
	} else if (csr_ops == CSR_OPS_OPERATION) {
		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;

		ravb_write(ndev, *(valp + 0), SFV0);
		ravb_write(ndev, *(valp + 1), SFV1);
		ravb_write(ndev, index, SFL);

		if (ravb_wait_reg(ndev, SFL, SFL_LC, SFL_LC_LOADABLE))
			return -EBUSY;
	}

	val[0] = ravb_read(ndev, SFP0 + offset);
	val[1] = ravb_read(ndev, SFP1 + offset);

	memcpy(hwq->streamID, (u8 *)val, sizeof(hwq->streamID));

	if ((val[0] != *(valp + 0)) || (val[1] != *(valp + 1)))
		return -EAGAIN;

	return 0;
}

/**
 * stqueue info operations
 */
static void stq_release(struct kobject *kobj)
{
	struct stqueue_info *stq = to_stq(kobj);
	struct hwqueue_info *hwq = stq->hwq;
	struct stream_entry *e, *e1;
	struct ravb_user_page *userpage, *userpage1;

	if (hwq->tx)
		unregister_cbs_param(hwq->index, &stq->cbs, true);
	list_for_each_entry_safe(e, e1, &stq->entryWaitQueue, list)
		put_streaming_entry(e);
	list_for_each_entry_safe(e, e1, &stq->entryLogQueue, list)
		put_streaming_entry(e);
	list_for_each_entry_safe(userpage, userpage1, &stq->userpages, list)
		put_userpage(userpage);

	/* merge statistics values */
	hwq->pstats.rx_packets += stq->pstats.rx_packets;
	hwq->pstats.tx_packets += stq->pstats.tx_packets;
	hwq->pstats.rx_bytes += stq->pstats.rx_bytes;
	hwq->pstats.tx_bytes += stq->pstats.tx_bytes;
	hwq->pstats.rx_errors += stq->pstats.rx_errors;
	hwq->pstats.tx_errors += stq->pstats.tx_errors;

	kfree(stq);
}

static struct kobj_type stq_ktype_rx = {
	.sysfs_ops = &stq_sysfs_ops,
	.release = stq_release,
	.default_attrs = stq_default_attrs_rx,
};

static struct kobj_type stq_ktype_tx = {
	.sysfs_ops = &stq_sysfs_ops,
	.release = stq_release,
	.default_attrs = stq_default_attrs_tx,
};

static struct stqueue_info *get_stq(struct hwqueue_info *hwq, int index)
{
	struct stqueue_info *stq;

	stq = kzalloc(sizeof(*stq), GFP_KERNEL);
	if (unlikely(!stq))
		goto no_memory;

	stq->qno = index;

	if (hwq->tx)
		stq->index = hwq->index * RAVB_STQUEUE_NUM + index;
	else
		stq->index = RAVB_HWQUEUE_TXNUM * RAVB_STQUEUE_NUM +
			hwq->index - RAVB_HWQUEUE_TXNUM;

	init_waitqueue_head(&stq->waitEvent);
	INIT_LIST_HEAD(&stq->entryWaitQueue);
	INIT_LIST_HEAD(&stq->entryLogQueue);
	INIT_LIST_HEAD(&stq->userpages);

	stq->list.next = LIST_POISON1; /* for debug */
	stq->list.prev = LIST_POISON2; /* for debug */
	stq->hwq = hwq;

	stq_sequencer(stq, AVB_STATE_IDLE);

	stq->kobj.kset = hwq->attached;

	if (kobject_init_and_add(&stq->kobj,
				 hwq->tx ? &stq_ktype_tx : &stq_ktype_rx,
				 NULL,
				 "%s:%d",
				 hwq_name(hwq),
				 stq->qno))
		goto no_kobj;

	if (sysfs_create_group(&stq->kobj, &stq_dev_stat_group))
		goto no_kobj;

	kobject_uevent(&stq->kobj, KOBJ_ADD);

	return stq;

no_kobj:
	kobject_put(&stq->kobj);
no_memory:
	return NULL;
}

static void put_stq(struct stqueue_info *stq)
{
	kobject_uevent(&stq->kobj, KOBJ_REMOVE);
	kobject_del(&stq->kobj);
	kobject_put(&stq->kobj);
}

/**
 * streaming driver API functions
 */
/* This API can be called from only the kernel driver */
static long ravb_get_entrynum_kernel(void *handle,
				     struct eavb_entrynum *entrynum)
{
	struct stqueue_info *stq = handle;

	if (!stq)
		return -EINVAL;

	if (!entrynum)
		return -EINVAL;

	*entrynum = stq->entrynum;

	return 0;
}

/* This API can be called from only the kernel driver */
static long ravb_get_linkspeed(void *handle)
{
	struct stqueue_info *stq = handle;
	struct hwqueue_info *hwq;
	struct streaming_private *stp;
	struct net_device *ndev;
	struct ravb_private *priv;

	if (!stq)
		return -EINVAL;

	hwq = stq->hwq;
	stp = to_stp(hwq->device.parent);
	ndev = to_net_dev(stp->device.parent);
	priv = netdev_priv(ndev);

	if (!priv->link) {
		pr_debug("netdev priv->link is 0\n");
		return 0;
	}

	if (!netif_running(ndev)) {
		pr_debug("netif_running() return  0\n");
		return 0;
	}

	return (long)priv->speed;
}

/* This API can be called from only the kernel driver */
static long ravb_blocking_cancel_kernel(void *handle)
{
	struct stqueue_info *stq = handle;
	struct hwqueue_info *hwq;

	if (!stq)
		return -EINVAL;

	hwq = stq->hwq;
	if (!(stq->flags & O_NONBLOCK)) {
		avb_down(&hwq->sem, hwq->index, stq->qno);
		stq->cancel = true;
		avb_up(&hwq->sem, hwq->index, stq->qno);
		avb_wake_up_interruptible(&stq->waitEvent,
					  hwq->index, stq->qno);
	}

	return 0;
}

static long ravb_set_txparam_kernel(void *handle, struct eavb_txparam *txparam)
{
	struct streaming_private *stp = stp_ptr;
	struct stqueue_info *stq = handle;
	u64 bw;

	if (!stq)
		return -EINVAL;

	if (!txparam)
		return -EINVAL;

	if (!stq->hwq->tx)
		return -EPERM;

	if (stq->state != AVB_STATE_IDLE)
		return -EBUSY;

	pr_debug("set_txparam: %s %08x %08x %08x\n",
		 stq_name(stq),
		 txparam->cbs.bandwidthFraction,
		 txparam->cbs.idleSlope,
		 txparam->cbs.sendSlope);

	avb_down(&stp->sem, -1, -1);

	bw = stp->cbsInfo.bandwidthFraction;
	bw -= stq->cbs.bandwidthFraction;
	bw += txparam->cbs.bandwidthFraction;

	if (bw > RAVB_CBS_BANDWIDTH_LIMIT) {
		avb_up(&stp->sem, -1, -1);
		return -ENOSPC;
	}

	unregister_cbs_param(stq->hwq->index, &stq->cbs, false);
	memcpy(&stq->cbs, &txparam->cbs, sizeof(stq->cbs));
	register_cbs_param(stq->hwq->index, &stq->cbs);

	avb_up(&stp->sem, -1, -1);

	return 0;
}

static long ravb_set_txparam(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_txparam txparam;
	char __user *buf = (char __user *)parm;

	if (copy_from_user(&txparam, buf, sizeof(txparam)))
		return -EFAULT;

	return ravb_set_txparam_kernel(kif->handle, &txparam);
}

static long ravb_get_txparam_kernel(void *handle, struct eavb_txparam *txparam)
{
	struct stqueue_info *stq = handle;

	if (!stq)
		return -EINVAL;

	if (!txparam)
		return -EINVAL;

	if (!stq->hwq->tx)
		return -EPERM;

	pr_debug("get_txparam: %s\n", stq_name(stq));

	memcpy(&txparam->cbs, &stq->cbs, sizeof(stq->cbs));

	return 0;
}

static long ravb_get_txparam(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_txparam txparam;
	char __user *buf = (char __user *)parm;
	long ret;

	ret = ravb_get_txparam_kernel(kif->handle, &txparam);
	if (ret)
		return ret;

	if (copy_to_user(buf, &txparam, sizeof(txparam)))
		return -EFAULT;

	return 0;
}

static long ravb_set_rxparam_kernel(void *handle, struct eavb_rxparam *rxparam)
{
	struct streaming_private *stp = stp_ptr;
	struct stqueue_info *stq = handle;
	int ret;

	if (!stq)
		return -EINVAL;

	if (!rxparam)
		return -EINVAL;

	if (stq->hwq->tx)
		return -EPERM;

	avb_down(&stp->sem, -1, -1);
	ret = register_streamID(stq->hwq, rxparam->streamid);
	avb_up(&stp->sem, -1, -1);

	pr_debug("set_rxparam: %s %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		 stq_name(stq),
		 rxparam->streamid[0], rxparam->streamid[1],
		 rxparam->streamid[2], rxparam->streamid[3],
		 rxparam->streamid[4], rxparam->streamid[5],
		 rxparam->streamid[6], rxparam->streamid[7]);

	return ret;
}

static long ravb_set_rxparam(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_rxparam rxparam;
	char __user *buf = (char __user *)parm;

	if (copy_from_user(&rxparam, buf, sizeof(rxparam)))
		return -EFAULT;

	return ravb_set_rxparam_kernel(kif->handle, &rxparam);
}

static long ravb_get_rxparam_kernel(void *handle, struct eavb_rxparam *rxparam)
{
	struct stqueue_info *stq = handle;

	if (!stq)
		return -EINVAL;

	if (!rxparam)
		return -EINVAL;

	if (stq->hwq->tx)
		return -EPERM;

	pr_debug("get_rxparam: %s\n", stq_name(stq));

	memcpy(rxparam->streamid, stq->hwq->streamID,
	       sizeof(rxparam->streamid));

	return 0;
}

static long ravb_get_rxparam(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_rxparam rxparam;
	char __user *buf = (char __user *)parm;
	long ret;

	ret = ravb_get_rxparam_kernel(kif->handle, &rxparam);
	if (ret)
		return ret;

	if (copy_to_user(buf, &rxparam, sizeof(rxparam)))
		return -EFAULT;

	return 0;
}

static long ravb_get_cbs_info(struct file *file, unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;
	char __user *buf = (char __user *)parm;
	long err = 0;

	pr_debug("get_cbs_info: %08x A:%08x B:%08x\n",
		 stp->cbsInfo.bandwidthFraction,
		 stp->cbsInfo.param[0].bandwidthFraction,
		 stp->cbsInfo.param[1].bandwidthFraction);

	avb_down(&stp->sem, -1, -1);
	if (copy_to_user(buf, &stp->cbsInfo, sizeof(struct eavb_cbsinfo)))
		err = -EFAULT;
	avb_up(&stp->sem, -1, -1);

	return err;
}

static long ravb_set_option_kernel(void *handle, struct eavb_option *option)
{
	struct stqueue_info *stq = handle;

	if (!stq)
		return -EINVAL;

	if (!option)
		return -EINVAL;

	pr_debug("set_option: %s %d %08x\n",
		 stq_name(stq), option->id, option->param);

	switch (option->id) {
	case EAVB_OPTIONID_BLOCKMODE:
		switch (option->param) {
		case EAVB_BLOCK_NOWAIT:
		case EAVB_BLOCK_WAITALL:
			stq->blockmode = option->param;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static long ravb_set_option(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_option option;
	char __user *buf = (char __user *)parm;

	if (copy_from_user(&option, buf, sizeof(option)))
		return -EFAULT;

	return ravb_set_option_kernel(kif->handle, &option);
}

static long ravb_get_option_kernel(void *handle, struct eavb_option *option)
{
	struct stqueue_info *stq = handle;

	if (!stq)
		return -EINVAL;

	if (!option)
		return -EINVAL;

	switch (option->id) {
	case EAVB_OPTIONID_BLOCKMODE:
		option->param = stq->blockmode;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("get_option: %s %d %08x\n",
		 stq_name(stq), option->id, option->param);

	return 0;
}

static long ravb_get_option(struct file *file, unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct eavb_option option;
	char __user *buf = (char __user *)parm;
	long ret;

	if (copy_from_user(&option, buf, sizeof(option)))
		return -EFAULT;

	ret = ravb_get_option_kernel(kif->handle, &option);
	if (ret)
		return ret;

	if (copy_to_user(buf, &option, sizeof(option)))
		return -EFAULT;

	return 0;
}

static long ravb_map_page(struct file *file, unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq;
	struct ravb_user_page *userpage;
	struct eavb_dma_alloc dma;
	char __user *buf = (char __user *)parm;
	long err = 0;

	if (kif)
		stq = kif->handle;
	else
		stq = NULL;

	if (copy_from_user(&dma, buf, sizeof(dma))) {
		pr_err("map_page: copy from user failed\n");
		err = -EFAULT;
		goto failed;
	}

	userpage = get_userpage();
	if (unlikely(!userpage)) {
		err = -ENOMEM;
		goto failed;
	}

	if ((u64)userpage->page_dma >= 0x100000000UL)
		pr_warn("map_page: 32bit over address(page_dma=%pad)\n",
			&userpage->page_dma);
	dma.dma_paddr = cpu_to_le32((u32)userpage->page_dma);
	dma.mmap_size = PAGE_SIZE;

	if (copy_to_user(buf, &dma, sizeof(dma))) {
		pr_err("map_page: copyout to user failed\n");
		put_userpage(userpage);
		err = -EFAULT;
		goto failed;
	}

	avb_down(&stp->sem, -1, -1);
	if (stq)
		list_add_tail(&userpage->list, &stq->userpages);
	else
		list_add_tail(&userpage->list, &stp->userpages);
	avb_up(&stp->sem, -1, -1);

	pr_debug("map_page: %p %08x %d\n", userpage->page,
		 dma.dma_paddr, dma.mmap_size);

	return 0;

failed:
	return err;
}

static long ravb_unmap_page(struct file *file, unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq;
	struct ravb_user_page *userpage;
	struct eavb_dma_alloc dma;
	char __user *buf = (char __user *)parm;
	long err = 0;

	if (kif)
		stq = kif->handle;
	else
		stq = NULL;

	if (copy_from_user(&dma, buf, sizeof(dma)))
		return -EFAULT;

	pr_debug("unmap_page: %08x %d\n", dma.dma_paddr, dma.mmap_size);

	if (dma.dma_paddr == 0)
		return 0;

	avb_down(&stp->sem, -1, -1);

	userpage = lookup_userpage(stq, dma.dma_paddr);
	if (!userpage) {
		err = -EINVAL;
		goto failed;
	}
	put_userpage(userpage);

failed:
	avb_up(&stp->sem, -1, -1);
	return err;
}

static int ravb_streaming_read_stq_kernel(void *handle,
					  struct eavb_entry *buf,
					  unsigned int num);
static int ravb_streaming_write_stq_kernel(void *handle,
					   struct eavb_entry *buf,
					   unsigned int num);

int ravb_streaming_open_stq_kernel(enum AVB_DEVNAME dev_name,
				   struct ravb_streaming_kernel_if *kif,
				   unsigned int flags)
{
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct ravb_private *priv = netdev_priv(ndev);
	struct hwqueue_info *hwq;
	struct stqueue_info *stq;
	int index, qno, n_queues;

	if (!kif)
		return -EINVAL;

	index = dev_name;
	if (index >= RAVB_HWQUEUE_NUM || index < 0)
		return -ENODEV;

	hwq = &stp->hwqueueInfoTable[index];
	n_queues = (hwq->tx) ? RAVB_STQUEUE_NUM : 1;

	avb_down(&hwq->sem, hwq->index, -1);
	qno = find_first_zero_bit(hwq->stream_map, n_queues);
	if (!(qno < n_queues)) {
		avb_up(&hwq->sem, hwq->index, -1);
		return -EBUSY;
	}

	stq = get_stq(hwq, qno);
	if (!stq) {
		avb_up(&hwq->sem, hwq->index, -1);
		return -ENOMEM;
	}

	kif->handle = stq;
	kif->read = &ravb_streaming_read_stq_kernel;
	kif->write = &ravb_streaming_write_stq_kernel;
	kif->set_txparam = &ravb_set_txparam_kernel;
	kif->get_txparam = &ravb_get_txparam_kernel;
	kif->set_rxparam = &ravb_set_rxparam_kernel;
	kif->get_rxparam = &ravb_get_rxparam_kernel;
	kif->set_option = &ravb_set_option_kernel;
	kif->get_option = &ravb_get_option_kernel;
	kif->get_entrynum = &ravb_get_entrynum_kernel;
	kif->get_linkspeed = &ravb_get_linkspeed;
	kif->blocking_cancel = &ravb_blocking_cancel_kernel;

	stq->flags = flags;

	hwq->stqueueInfoTable[qno] = stq;
	set_bit(qno, hwq->stream_map);

	/* reset decriptor count if hw incorrect state */
	if (ravb_read(ndev, CDAR0 + (hwq->qno * sizeof(u32))) ==
	    (u32)(priv->desc_bat_dma + (sizeof(struct ravb_desc) * hwq->qno))) {
		hwq->curr = 0;
		hwq->remain = hwq->ringsize;
	}

	avb_up(&hwq->sem, hwq->index, -1);

	pr_debug("open: %s\n", stq_name(stq));

	return 0;
}
EXPORT_SYMBOL(ravb_streaming_open_stq_kernel);
static int ravb_streaming_open_stq(struct inode *inode, struct file *file)
{
	struct ravb_streaming_kernel_if *kif;
	int ret;

	kif = kzalloc(sizeof(*kif), GFP_KERNEL);

	if (!kif)
		return -ENOMEM;

	ret = ravb_streaming_open_stq_kernel((enum AVB_DEVNAME)(iminor(inode)),
					     kif, file->f_flags);
	if (ret) {
		kfree(kif);
		return ret;
	}

	file->private_data = kif;

	return 0;
}

static int ravb_streaming_open(struct inode *inode, struct file *file)
{
	if (iminor(inode) == AVB_CTRL_MINOR)
		return 0;

	return ravb_streaming_open_stq(inode, file);
}

int ravb_streaming_release_stq_kernel(void *handle)
{
	struct streaming_private *stp = stp_ptr;
	struct stqueue_info *stq = handle;
	struct hwqueue_info *hwq;

	if (!stq)
		return -EINVAL;

	pr_debug("close: %s\n", stq_name(stq));

	hwq = stq->hwq;

	/**
	 * if ACTIVE or WAITCOMPLETE,
	 * wait complete all entry processed.
	 */
	avb_down(&hwq->sem, hwq->index, stq->qno);
	switch (stq->state) {
	case AVB_STATE_ACTIVE:
	case AVB_STATE_WAITCOMPLETE:
		if (!hwq->tx)
			hwq->defunct = 1;
		hwq_event(hwq, AVB_EVENT_DETACH, stq->qno);
		avb_up(&hwq->sem, hwq->index, stq->qno);
		trace_avb_wait_sleep(hwq->index, stq->qno);
		while (wait_event_interruptible(stq->waitEvent,
						stq->state == AVB_STATE_IDLE))
			;
		avb_down(&hwq->sem, hwq->index, stq->qno);
		break;
	default:
		break;
	}

	clear_bit(stq->qno, hwq->stream_map);
	avb_up(&hwq->sem, hwq->index, stq->qno);

	avb_down(&stp->sem, -1, -1);
	put_stq(stq);
	avb_up(&stp->sem, -1, -1);

	return 0;
}
EXPORT_SYMBOL(ravb_streaming_release_stq_kernel);
static int ravb_streaming_release_stq(struct inode *inode, struct file *file)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	int ret;

	ret = ravb_streaming_release_stq_kernel(kif->handle);
	if (ret)
		return ret;

	kfree(kif);

	return 0;
}

static int ravb_streaming_release(struct inode *inode, struct file *file)
{
	if (!file->private_data)
		return 0;

	return ravb_streaming_release_stq(inode, file);
}

static int ravb_streaming_read_stq_kernel(void *handle,
					  struct eavb_entry *buf,
					  unsigned int num)
{
	struct stqueue_info *stq = handle;
	struct hwqueue_info *hwq;
	struct stream_entry *e;
	int i;
	int err;

	if (!stq)
		return -EINVAL;

	if (!buf)
		return -EINVAL;

	pr_debug("read: %s > num=%d\n", stq_name(stq), num);

	if (!num)
		return 0;

	if (stq->blockmode == EAVB_BLOCK_WAITALL &&
	    num > RAVB_ENTRY_THRETH)
		return -ENOMEM;

	hwq = stq->hwq;

	avb_down(&hwq->sem, hwq->index, stq->qno);
	stq->cancel = false;
	if (!is_readable_count(stq, num)) {
		avb_up(&hwq->sem, hwq->index, stq->qno);

		if (stq->flags & O_NONBLOCK)
			return -EAGAIN;

		err = avb_wait_event_interruptible(
			stq->waitEvent,
			is_readable_count(stq, num) || stq->cancel,
			hwq->index, stq->qno);
		if (err < 0)
			return -EINTR;

		avb_down(&hwq->sem, hwq->index, stq->qno);
	}

	num = min_t(u32, (u32)num, stq->entrynum.completed);
	for (i = 0; i < num; i++) {
		e = list_first_entry(&stq->entryLogQueue,
				     struct stream_entry, list);
		memcpy(buf + i, &e->msg, sizeof(struct eavb_entry));
		if (!uncached_access(stq))
			cachesync_streaming_entry(e);
		put_streaming_entry(e);
	}

	stq->entrynum.accepted -= i;
	stq->entrynum.completed -= i;
	if (hwq->tx) {
		if (stq->dstats.tx_entry_complete >= (u64)i) {
			stq->dstats.tx_entry_complete -= (u64)i;
		} else {
			pr_warn("read: underflow (tx_entry_complete=0x%016llx) < (i=%d)\n",
				stq->dstats.tx_entry_complete, i);
			stq->dstats.tx_entry_complete = 0x8000000000000000ULL;
		}
	} else {
		if (stq->dstats.rx_entry_complete >= (u64)i) {
			stq->dstats.rx_entry_complete -= (u64)i;
		} else {
			pr_warn("read: underflow (rx_entry_complete=0x%016llx) < (i=%d)\n",
				stq->dstats.rx_entry_complete, i);
			stq->dstats.rx_entry_complete = 0x8000000000000000ULL;
		}
	}

	avb_up(&hwq->sem, hwq->index, stq->qno);
	avb_wake_up_interruptible(&stq->waitEvent, hwq->index, stq->qno);

	pr_debug("read: %s < num=%d\n", stq_name(stq), i);

	return i;
}

static ssize_t ravb_streaming_read_stq(struct file *file,
				       char __user *buf,
				       size_t count, loff_t *ppos)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq = kif->handle;
	int num;
	unsigned int fraction;
	unsigned long ret;
	ssize_t rsize;

	stq->flags = file->f_flags;
	num = count / sizeof(struct eavb_entry);
	fraction = count % sizeof(struct eavb_entry);

	pr_debug("read: %s < count=%zd, fraction=%d\n",
		 stq_name(stq), count, fraction);

	if (fraction)
		return -EINVAL;

	num = ravb_streaming_read_stq_kernel(stq, stq->ebuf, num);
	if (num <= 0)
		return (ssize_t)num;

	ret = copy_to_user(buf, stq->ebuf,
			   num * sizeof(struct eavb_entry));
	if (ret) {
		pr_err("read: %s copy to user failed\n", stq_name(stq));
		return -EFAULT;
	}

	rsize = num * sizeof(struct eavb_entry);
	pr_debug("read: %s < count=%zd\n", stq_name(stq), rsize);

	return rsize;
}

static ssize_t ravb_streaming_read(struct file *file,
				   char __user *buf,
				   size_t count, loff_t *ppos)
{
	if (!file->private_data)
		return -EPERM;

	return ravb_streaming_read_stq(file, buf, count, ppos);
}

static int ravb_streaming_write_stq_kernel(void *handle,
					   struct eavb_entry *buf,
					   unsigned int num)
{
	struct stqueue_info *stq = handle;
	struct hwqueue_info *hwq;
	struct stream_entry *e;
	struct list_head entry_queue;
	int i;
	int err;

	if (!stq)
		return -EINVAL;

	if (!buf)
		return -EINVAL;

	pr_debug("write: %s > num=%d\n", stq_name(stq), num);

	if (!num)
		return 0;

	hwq = stq->hwq;

	avb_down(&hwq->sem, hwq->index, stq->qno);
	stq->cancel = false;
	if (!is_writeble(stq)) {
		avb_up(&hwq->sem, hwq->index, stq->qno);

		if (stq->flags & O_NONBLOCK)
			return -EAGAIN;

		err = avb_wait_event_interruptible(
			stq->waitEvent,
			is_writeble(stq) || stq->cancel,
			hwq->index, stq->qno);
		if (err < 0)
			return -EINTR;

		avb_down(&hwq->sem, hwq->index, stq->qno);
	}

	num = min_t(u32, (u32)num,
		    RAVB_ENTRY_THRETH - stq->entrynum.accepted);
	avb_up(&hwq->sem, hwq->index, stq->qno);
	/* entry remain is full */
	if (!num)
		return 0;

	INIT_LIST_HEAD(&entry_queue);
	for (i = 0; i < num; i++) {
		e = get_streaming_entry();
		if (!e)
			break;
		memcpy(&e->msg, buf + i, sizeof(struct eavb_entry));
		e->vecsize = desc_pre_encode(e, stq->hwq->tx);
		if (e->vecsize == 0) {
			/* TODO countup invalid entry num */
			pr_warn("write: %s invalid entry(%08x) ignored\n",
				stq_name(stq), e->msg.seq_no);
			put_streaming_entry(e);
		} else {
			e->stq = stq;
			if (!uncached_access(stq))
				cachesync_streaming_entry(e);
			trace_avb_entry_accept_wrap(e);
			list_move_tail(&e->list, &entry_queue);
		}
	}

	avb_down(&hwq->sem, hwq->index, stq->qno);

	stq->entrynum.accepted += i;
	if (hwq->tx)
		stq->dstats.tx_entry_wait += (u64)i;
	else
		stq->dstats.rx_entry_wait += (u64)i;

	list_splice_tail(&entry_queue, &stq->entryWaitQueue);

	/* if IDLE or WAITCOMPLETE, attach to hwq */
	if (stq->state == AVB_STATE_IDLE ||
	    stq->state == AVB_STATE_WAITCOMPLETE) {
		stq_sequencer(stq, AVB_STATE_ACTIVE);
		list_add_tail(&stq->list, &hwq->activeStreamQueue);
		hwq_event(hwq, AVB_EVENT_ATTACH, stq->qno);
	}

	avb_up(&hwq->sem, hwq->index, stq->qno);

	pr_debug("write: %s < num=%d\n", stq_name(stq), i);

	return i;
}

static ssize_t ravb_streaming_write_stq(struct file *file,
					const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq = kif->handle;
	int num;
	unsigned int fraction;
	unsigned long ret;
	ssize_t wsize;

	stq->flags = file->f_flags;
	num = min_t(u32, (u32)(count / sizeof(struct eavb_entry)),
		    RAVB_ENTRY_THRETH);
	fraction = count % sizeof(struct eavb_entry);

	pr_debug("write: %s < count=%zd, fraction=%d\n",
		 stq_name(stq), count, fraction);

	if (fraction)
		return -EINVAL;

	ret = copy_from_user(stq->ebuf, buf,
			     num * sizeof(struct eavb_entry));
	if (ret) {
		pr_err("write: %s copy from user failed\n", stq_name(stq));
		return -EFAULT;
	}

	num = ravb_streaming_write_stq_kernel(stq, stq->ebuf, num);
	if (num <= 0)
		return (ssize_t)num;

	wsize = num * sizeof(struct eavb_entry);
	pr_debug("write: %s < count=%zd\n", stq_name(stq), wsize);

	return wsize;
}

static ssize_t ravb_streaming_write(struct file *file,
				    const char __user *buf,
				    size_t count, loff_t *ppos)
{
	if (!file->private_data)
		return -EPERM;

	return ravb_streaming_write_stq(file, buf, count, ppos);
}

static unsigned int ravb_streaming_poll_stq(struct file *file,
					    struct poll_table_struct *wait)
{
	int ret = 0;
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq = kif->handle;

	pr_debug("poll: %s r=%d, w=%d\n",
		 stq_name(stq), is_readable(stq), is_writeble(stq));

	poll_wait(file, &stq->waitEvent, wait);

	if (is_readable(stq))
		ret |= POLLIN | POLLRDNORM;

	if (is_writeble(stq))
		ret |= POLLOUT | POLLWRNORM;

	return ret;
}

static unsigned int ravb_streaming_poll(struct file *file,
					struct poll_table_struct *wait)
{
	if (!file->private_data)
		return 0;

	return ravb_streaming_poll_stq(file, wait);
}

static void ravb_streaming_vm_open(struct vm_area_struct *vma)
{
}

static void ravb_streaming_vm_close(struct vm_area_struct *vma)
{
}

#if KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
static int ravb_streaming_vm_fault(struct vm_fault *vmf)
#else
static int ravb_streaming_vm_fault(struct vm_area_struct *area,
				   struct vm_fault *vmf)
#endif
{
	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct ravb_streaming_mmap_ops = {
	.open   = ravb_streaming_vm_open,
	.close  = ravb_streaming_vm_close,
	.fault  = ravb_streaming_vm_fault
};

static int ravb_streaming_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct streaming_private *stp = stp_ptr;
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq;
	unsigned long size  = vma->vm_end - vma->vm_start;
	dma_addr_t pgoff = (dma_addr_t)vma->vm_pgoff;
	dma_addr_t physaddr = pgoff << PAGE_SHIFT;

	if (kif)
		stq = kif->handle;
	else
		stq = NULL;

	pr_debug("mmap: %s range=%08lx-%08lx,size=%lu,physaddr=%pad\n",
		 (stq) ? stq_name(stq) : stp_name(stp),
		 vma->vm_start, vma->vm_end, size, &physaddr);

	if (!lookup_userpage(stq, physaddr))
		return -EINVAL;

	vma->vm_page_prot = phys_mem_access_prot(file,
						 pgoff,
						 size,
						 vma->vm_page_prot);

	vma->vm_ops = &ravb_streaming_mmap_ops;

	if (remap_pfn_range(vma,
			    vma->vm_start,
			    pgoff,
			    size,
			    vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static long ravb_streaming_ioctl_stp(struct file *file,
				     unsigned int cmd,
				     unsigned long parm)
{
	struct streaming_private *stp = stp_ptr;

	pr_debug("ioctl: %s cmd=%08x\n", stp_name(stp), cmd);

	switch (cmd) {
	case EAVB_MAPPAGE:
		return ravb_map_page(file, parm);
	case EAVB_UNMAPPAGE:
		return ravb_unmap_page(file, parm);
	case EAVB_GETCBSINFO:
		return ravb_get_cbs_info(file, parm);
	case EAVB_GDRVINFO:
	case EAVB_GRINGPARAM:
	case EAVB_GCHANNELS:
	case EAVB_GSSET_INFO:
	case EAVB_GSTRINGS:
	case EAVB_GSTATS:
		return ravb_streaming_ioctl_avbtool(file, cmd, parm);
	default:
		return -EINVAL;
	}
}

static long ravb_streaming_ioctl_stq(struct file *file,
				     unsigned int cmd,
				     unsigned long parm)
{
	struct ravb_streaming_kernel_if *kif = file->private_data;
	struct stqueue_info *stq = kif->handle;

	pr_debug("ioctl: %s cmd=%08x\n", stq_name(stq), cmd);

	switch (cmd) {
	case EAVB_MAPPAGE:
		return ravb_map_page(file, parm);
	case EAVB_UNMAPPAGE:
		return ravb_unmap_page(file, parm);
	case EAVB_SETTXPARAM:
		return ravb_set_txparam(file, parm);
	case EAVB_GETTXPARAM:
		return ravb_get_txparam(file, parm);
	case EAVB_SETRXPARAM:
		return ravb_set_rxparam(file, parm);
	case EAVB_GETRXPARAM:
		return ravb_get_rxparam(file, parm);
	case EAVB_GETCBSINFO:
		return ravb_get_cbs_info(file, parm);
	case EAVB_SETOPTION:
		return ravb_set_option(file, parm);
	case EAVB_GETOPTION:
		return ravb_get_option(file, parm);
	case EAVB_GDRVINFO:
	case EAVB_GRINGPARAM:
	case EAVB_GCHANNELS:
	case EAVB_GSSET_INFO:
	case EAVB_GSTRINGS:
	case EAVB_GSTATS:
		return ravb_streaming_ioctl_avbtool(file, cmd, parm);
	default:
		return -EINVAL;
	}
}

static long ravb_streaming_ioctl(struct file *file,
				 unsigned int cmd, unsigned long parm)
{
	if (!file->private_data)
		return ravb_streaming_ioctl_stp(file, cmd, parm);
	else
		return ravb_streaming_ioctl_stq(file, cmd, parm);
}

#ifdef CONFIG_COMPAT
struct eavb_dma_alloc_compat {
	u32 dma_paddr;
	compat_uptr_t dma_vaddr;
	compat_uint_t mmap_size;
};

#define EAVB_MAPPAGE_COMPAT   _IOR(EAVB_MAGIC, 1, struct eavb_dma_alloc_compat)
#define EAVB_UNMAPPAGE_COMPAT _IOW(EAVB_MAGIC, 2, struct eavb_dma_alloc_compat)

static long ravb_map_page_compat(struct file *file, unsigned long parm)
{
	long ret;
	struct eavb_dma_alloc_compat dma32;
	struct eavb_dma_alloc __user *pdma;
	void __user *dma_vaddr;

	pdma = compat_alloc_user_space(sizeof(*pdma));

	ret = ravb_map_page(file, (unsigned long)(pdma));
	if (ret)
		return ret;

	if (!access_ok(VERIFY_READ, pdma, sizeof(*pdma)))
		goto failed;
	if (__get_user(dma32.dma_paddr, &pdma->dma_paddr) ||
	    __get_user(dma_vaddr, &pdma->dma_vaddr) ||
	    __get_user(dma32.mmap_size, &pdma->mmap_size))
		goto failed;
	dma32.dma_vaddr = ptr_to_compat(dma_vaddr);

	if (copy_to_user((void __user *)parm, &dma32, sizeof(dma32)))
		goto failed;

	return 0;

failed:
	ravb_unmap_page(file, (unsigned long)(pdma));
	return -EFAULT;
}

static long ravb_unmap_page_compat(struct file *file, unsigned long parm)
{
	struct eavb_dma_alloc_compat dma32;
	struct eavb_dma_alloc __user *pdma;

	if (copy_from_user(&dma32, (void __user *)parm, sizeof(dma32)))
		return -EFAULT;

	pdma = compat_alloc_user_space(sizeof(*pdma));
	if (!access_ok(VERIFY_WRITE, pdma, sizeof(*pdma)))
		return -EFAULT;
	if (__put_user(dma32.dma_paddr, &pdma->dma_paddr) ||
	    __put_user((void __user *)(unsigned long)dma32.dma_vaddr,
		       &pdma->dma_vaddr) ||
	    __put_user(dma32.mmap_size, &pdma->mmap_size))
		return -EFAULT;

	return ravb_unmap_page(file, (unsigned long)(pdma));
}

static long ravb_streaming_ioctl_compat(struct file *file,
					unsigned int cmd, unsigned long parm)
{
	switch (cmd) {
	case EAVB_MAPPAGE_COMPAT:
		return ravb_map_page_compat(file, parm);
	case EAVB_UNMAPPAGE_COMPAT:
		return ravb_unmap_page_compat(file, parm);
	default:
		return ravb_streaming_ioctl(file, cmd, parm);
	}
}
#endif

static const struct file_operations ravb_streaming_fops = {
	.owner		= THIS_MODULE,
	.open		= ravb_streaming_open,
	.release	= ravb_streaming_release,
	.read		= ravb_streaming_read,
	.write		= ravb_streaming_write,
	.poll		= ravb_streaming_poll,
	.mmap       = ravb_streaming_mmap,
	.unlocked_ioctl	= ravb_streaming_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ravb_streaming_ioctl_compat,
#endif
};

static inline int ravb_control_interrupt(struct net_device *ndev,
					 struct hwqueue_info *hwq,
					 bool enable)
{
	struct ravb_private *priv = netdev_priv(ndev);
	u32 ofs, val;
	unsigned long flags;

	avb_spin_lock_irqsave(&priv->lock, flags, hwq->index, -1);
	ofs = (hwq->tx) ? TIC : RIC0;
	val = ravb_read(ndev, ofs);
	if (enable)
		ravb_write(ndev, (val | (1 << hwq->chno)), ofs);
	else
		ravb_write(ndev, (val & ~(1 << hwq->chno)), ofs);
	avb_spin_unlock_irqrestore(&priv->lock, flags, hwq->index, -1);

	return 0;
}

static inline int ravb_enable_interrupt(struct net_device *ndev,
					struct hwqueue_info *hwq)
{
	struct ravb_private *priv = netdev_priv(ndev);

	if (priv->chip_id == RCAR_GEN3) {
		if (hwq->tx)
			ravb_write(ndev, BIT(hwq->chno + TDP_BIT_OFFSET), TIE);
		else
			ravb_write(ndev, BIT(hwq->chno + RDP_BIT_OFFSET), RIE3);

		/* if irq timeout isn't zero, start irq timeout timer */
		hwq_try_to_start_irq_timeout_timer(hwq);

		return 0;
	}

	return ravb_control_interrupt(ndev, hwq, true);
}

static inline int ravb_disable_interrupt(struct net_device *ndev,
					 struct hwqueue_info *hwq)
{
	struct ravb_private *priv = netdev_priv(ndev);

	if (priv->chip_id == RCAR_GEN3) {
		if (hwq->tx)
			ravb_write(ndev, BIT(hwq->chno + TDP_BIT_OFFSET), TID);
		else
			ravb_write(ndev, BIT(hwq->chno + RDP_BIT_OFFSET), RID3);

		return 0;
	}

	return ravb_control_interrupt(ndev, hwq, false);
}

static inline int ravb_reload_chain(struct net_device *ndev, int index)
{
	u32 loadmask = 1 << index;

	/* if not OPERATION mode, doesn't need reload chain */
	if ((ravb_read(ndev, CSR) & CSR_OPS) != CSR_OPS_OPERATION)
		return 0;

	ravb_write(ndev, loadmask, DLR);
	while (ravb_read(ndev, DLR) & loadmask)
		;

	return 0;
}

static int hwq_task_process_terminate(struct hwqueue_info *hwq)
{
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct ravb_private *priv = netdev_priv(ndev);
	struct ravb_desc *desc;
	struct stqueue_info *stq, *stq1;
	struct stream_entry *e, *e1;
	struct stqueue_info *stq_pool[RAVB_STQUEUE_NUM] = { NULL };
	int index, i;

	if (unlikely(hwq->defunct)) {
		/* write EOS for hw terminate */
		index = hwq->qno;
		desc = (struct ravb_desc *)&priv->desc_bat[index];
		desc->die_dt = DT_EOS;
		/* force reload chain */
		ravb_reload_chain(ndev, index);
		/* clear descriptor chain */
		clear_desc(hwq);
		/* write LINKFIX as restore chain */
		desc->die_dt = DT_LINKFIX;
		/* force reload chain */
		ravb_reload_chain(ndev, index);

		/* flush activeStreamQueue */
		list_for_each_entry_safe(stq, stq1, &hwq->activeStreamQueue, list) {
			stq_pool[stq->qno] = stq;
			list_del(&stq->list);
		}
		/* flush completeWaitQueue */
		list_for_each_entry_safe(e, e1, &hwq->completeWaitQueue, list) {
			stq_pool[e->stq->qno] = e->stq;
			put_streaming_entry(e);
		}

		/* raise stream queue */
		for (i = 0; i < RAVB_STQUEUE_NUM; i++) {
			stq = stq_pool[i];
			if (stq) {
				stq_sequencer(stq, AVB_STATE_IDLE);
				avb_wake_up_interruptible(&stq->waitEvent,
							  hwq->index,
							  stq->qno);
			}
		}
	}

	return 0;
}

static int hwq_task_process_encode(struct hwqueue_info *hwq)
{
	struct stqueue_info *stq;
	struct stream_entry *e;
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);

	while (hwq->remain >= EAVB_ENTRYVECNUM &&
	       !list_empty(&hwq->activeStreamQueue)) {
		stq = list_first_entry(&hwq->activeStreamQueue,
				       struct stqueue_info,
				       list);
		e = list_first_entry(&stq->entryWaitQueue,
				     struct stream_entry,
				     list);

		desc_copy(hwq, e);
		trace_avb_entry_encode(e);
		list_move_tail(&e->list, &hwq->completeWaitQueue);
		stq->entrynum.processed++;

		if (hwq->tx)
			stq->dstats.tx_entry_wait--;
		else
			stq->dstats.rx_entry_wait--;

		if (list_empty(&stq->entryWaitQueue)) {
			list_del(&stq->list);
			stq_sequencer(stq, AVB_STATE_WAITCOMPLETE);
		} else {
			list_move_tail(&stq->list, &hwq->activeStreamQueue);
		}
	}

	if (hwq->tx) {
		/* transmission start request */
		ravb_write(ndev,
			   ravb_read(ndev, TCCR) | (TCCR_TSRQ0 << hwq->chno),
			   TCCR);
	}

	return 0;
}

static int hwq_task_process_decode(struct hwqueue_info *hwq)
{
	struct stqueue_info *stq;
	struct stream_entry *e, *e1;
	struct stqueue_info *stq_pool[RAVB_STQUEUE_NUM] = { NULL };
	int i;
	bool progress;

	progress = false;

	list_for_each_entry_safe(e, e1, &hwq->completeWaitQueue, list) {
		progress = desc_decode(hwq, e);
		if (progress)
			break;

		trace_avb_entry_decode(e);
		stq = e->stq;
		list_move_tail(&e->list, &stq->entryLogQueue);
		stq->entrynum.processed--;
		stq->entrynum.completed++;

		if (hwq->tx) {
			stq->dstats.tx_entry_complete++;
			stq->pstats.tx_packets++;
			stq->pstats.tx_errors += (u64)e->errors;
			stq->pstats.tx_bytes += (u64)e->total_bytes;
		} else {
			stq->dstats.rx_entry_complete++;
			stq->pstats.rx_packets++;
			stq->pstats.rx_errors += (u64)e->errors;
			stq->pstats.rx_bytes += (u64)e->total_bytes;
		}

		if (stq->entrynum.processed == 0 &&
		    stq->state == AVB_STATE_WAITCOMPLETE)
			stq_sequencer(stq, AVB_STATE_IDLE);

		stq_pool[stq->qno] = stq;
	}

	for (i = 0; i < RAVB_STQUEUE_NUM; i++) {
		stq = stq_pool[i];
		if (stq)
			avb_wake_up_interruptible(&stq->waitEvent,
						  hwq->index, stq->qno);
	}

	return progress;
}

static int hwq_task_process_judge(struct hwqueue_info *hwq, bool progress)
{
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);

	if (list_empty(&hwq->activeStreamQueue)) {
		if (list_empty(&hwq->completeWaitQueue)) {
			hwq->defunct = 0;
			hwq_sequencer(hwq, AVB_STATE_IDLE);
		} else {
			hwq_sequencer(hwq, AVB_STATE_WAITCOMPLETE);
			/* enable interrupt */
			ravb_enable_interrupt(ndev, hwq);
		}
	} else {
		if (!progress && list_empty(&hwq->completeWaitQueue)) {
			hwq_sequencer(hwq, AVB_STATE_ACTIVE);
		} else {
			hwq_sequencer(hwq, AVB_STATE_WAITCOMPLETE);
			/* enable interrupt */
			ravb_enable_interrupt(ndev, hwq);
		}
	}

	return 0;
}

/**
 * sequencer task each hwqueue_info
 */
static int ravb_hwq_task(void *param)
{
	struct hwqueue_info *hwq = param;
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);
	int ret;
	bool progress;

	while (!kthread_should_stop()) {
		ret = avb_wait_event_interruptible(hwq->waitEvent,
						   hwq->pendingEvents,
						   hwq->index,
						   -1);
		if (ret < 0) {
			pr_err("hwq_task.%d: wait_event error\n", hwq->index);
			continue;
		}

		/* unload event */
		if (hwq->pendingEvents & AVB_EVENT_UNLOAD) {
			avb_down(&hwq->sem, hwq->index, -1);
			/* terminate hardware queue */
			hwq->defunct = 1;
			hwq_task_process_terminate(hwq);
			hwq->defunct = 0;
			avb_up(&hwq->sem, hwq->index, -1);
			break;
		}

		hwq_event_clear(hwq);

		switch (hwq->state) {
		case AVB_STATE_IDLE:
			if (list_empty(&hwq->activeStreamQueue))
				break;
			/* fall through */
		case AVB_STATE_WAITCOMPLETE:
			/* disable interrupt */
			ravb_disable_interrupt(ndev, hwq);
			hwq_sequencer(hwq, AVB_STATE_ACTIVE);
			/* fall through */
		case AVB_STATE_ACTIVE:
			/* TODO implement SW scheduler */
			do {
				avb_down(&hwq->sem, hwq->index, -1);

				/* terminate hardware queue */
				hwq_task_process_terminate(hwq);
				/* convert new entry to build descriptor */
				hwq_task_process_encode(hwq);
				/* process completed descriptor by HW */
				progress = hwq_task_process_decode(hwq);
				/* judge hwq Task state */
				hwq_task_process_judge(hwq, progress);

				avb_up(&hwq->sem, hwq->index, -1);
			} while (hwq->state == AVB_STATE_ACTIVE);
			break;
		default:
			break;
		}
	}

	/* TODO finalize task */

	return 0;
}

static enum hrtimer_restart ravb_streaming_timer_handler(struct hrtimer *timer)
{
	struct hwqueue_info *hwq;

	hwq = container_of(timer, struct hwqueue_info, timer);
	if (hwq->state == AVB_STATE_IDLE)
		return HRTIMER_NORESTART;

	hwq_event(hwq, AVB_EVENT_TIMEOUT, hwq->chno);

	return HRTIMER_NORESTART;
}

/* ISR for streaming */
static irqreturn_t ravb_streaming_interrupt(int irq, void *dev_id)
{
	struct streaming_private *stp = dev_id;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct ravb_private *priv = netdev_priv(ndev);
	struct hwqueue_info *hwq;
	irqreturn_t ret = IRQ_NONE;
	u32 intr_status = 0;
	int i, index, limit;

	avb_spin_lock(&priv->lock, -1, -1);

	/* Get interrpt stat */
	intr_status = ravb_read(ndev, ISS);
	/* Check interrupt */
	if (!(intr_status & (ISS_FRS | ISS_FTS)))
		goto other_irq;

	/* Transmit Summary */
	if (intr_status & ISS_FTS) {
		u32 tis = ravb_read(ndev, TIS);
		u32 tmp = 0;

		limit = RAVB_HWQUEUE_TXNUM + RAVB_HWQUEUE_RESERVEDNUM;
		for (i = RAVB_HWQUEUE_RESERVEDNUM; i < limit; i++) {
			if (!(tis & (1 << i)))
				continue;

			tmp |= (1 << i);
			index = i - RAVB_HWQUEUE_RESERVEDNUM;
			hwq = &stp->hwqueueInfoTable[index];
			hwq_event_irq(hwq, AVB_EVENT_TXINT, index);

			hwq->dstats.tx_interrupts++;

			ret = IRQ_HANDLED;
		}
		ravb_write(ndev, ~tmp, TIS);
	}

	/* Frame Receive Summary */
	if (intr_status & ISS_FRS) {
		u32 ris0 = ravb_read(ndev, RIS0);
		u32 tmp = 0;

		limit = RAVB_HWQUEUE_RXNUM + RAVB_HWQUEUE_RESERVEDNUM;
		for (i = RAVB_HWQUEUE_RESERVEDNUM; i < limit; i++) {
			if (!(ris0 & (1 << i)))
				continue;

			tmp |= (1 << i);
			index = i - RAVB_HWQUEUE_RESERVEDNUM +
				RAVB_HWQUEUE_TXNUM;
			hwq = &stp->hwqueueInfoTable[index];
			hwq_event_irq(hwq, AVB_EVENT_RXINT, index);

			hwq->dstats.rx_interrupts++;

			ret = IRQ_HANDLED;
		}
		ravb_write(ndev, ~tmp, RIS0);
	}

other_irq:
	avb_spin_unlock(&priv->lock, -1, -1);

	return ret;
}

/* interrupt for streaming */
static irqreturn_t ravb_streaming_interrupt_rxtx(int irq, void *dev_id)
{
	struct hwqueue_info *hwq = dev_id;
	struct streaming_private *stp = to_stp(hwq->device.parent);
	struct net_device *ndev = to_net_dev(stp->device.parent);

	if (!hwq)
		return IRQ_NONE;

	if (irq != hwq->irq)
		return IRQ_NONE;

	if (hwq->tx) {
		hwq_event_irq(hwq, AVB_EVENT_TXINT, hwq->index);
		hwq->dstats.tx_interrupts++;
		ravb_write(ndev, ~BIT(hwq->chno + TDP_BIT_OFFSET), TIS);
	} else {
		hwq_event_irq(hwq, AVB_EVENT_RXINT, hwq->index);
		hwq->dstats.rx_interrupts++;
		ravb_write(ndev, ~BIT(hwq->chno + RDP_BIT_OFFSET), RIS3);
	}

	/* if irq timeout isn't zero, start irq timeout timer */
	hwq_try_to_start_irq_timeout_timer(hwq);

	return IRQ_HANDLED;
}

/**
 * device resource management callbacks
 */
static void stp_dev_release(struct device *dev)
{
	/* reserved */
}

static void hwq_dev_release(struct device *dev)
{
	/* reserved */
}

/**
 * initialize streaming API
 */
static const char *ravb_streaming_irqs[RAVB_HWQUEUE_NUM] = {
	"ch20", /* RAVB STREAMING TX0 */
	"ch21", /* RAVB STREAMING TX1 */
	"ch2",  /* RAVB STREAMING RX0 */
	"ch3",  /* RAVB STREAMING RX1 */
	"ch4",  /* RAVB STREAMING RX2 */
	"ch5",  /* RAVB STREAMING RX3 */
	"ch6",  /* RAVB STREAMING RX4 */
	"ch7",  /* RAVB STREAMING RX5 */
	"ch8",  /* RAVB STREAMING RX6 */
	"ch9",  /* RAVB STREAMING RX7 */
	"ch10", /* RAVB STREAMING RX8 */
	"ch11", /* RAVB STREAMING RX9 */
	"ch12", /* RAVB STREAMING RX10 */
	"ch13", /* RAVB STREAMING RX11 */
	"ch14", /* RAVB STREAMING RX12 */
	"ch15", /* RAVB STREAMING RX13 */
	"ch16", /* RAVB STREAMING RX14 */
	"ch17", /* RAVB STREAMING RX15 */
};

static const struct of_device_id ravb_streaming_match_table[] = {
	{ .compatible = "renesas,etheravb-rcar-gen2",
	  .data = (void *)RCAR_GEN2 },
	{ .compatible = "renesas,etheravb-rcar-gen3",
	  .data = (void *)RCAR_GEN3 },
	{ }
};
MODULE_DEVICE_TABLE(of, ravb_streaming_match_table);

static inline bool match_dev(struct net_device *ndev)
{
	const struct of_device_id *match = NULL;

	if (!ndev || !ndev->dev.parent)
		return false;

	/* check supported devices */
	match = of_match_device(of_match_ptr(ravb_streaming_match_table),
				ndev->dev.parent);

	return match;
}

static int ravb_streaming_init(void)
{
	int err = -ENODEV;
	int i;
	struct net_device *ndev = NULL;
	struct ravb_private *priv;
	struct streaming_private *stp;
	struct hwqueue_info *hwq;
	struct ravb_desc *desc;
	struct device *dev;
	struct device *pdev_dev;
	char taskname[32] = { '\0' };
	const char *irq_name, *irq_name2;
	int irq;

	/*
	 * Default behaviour is automatically detect the ravb interface
	 * unless the user used (interface) parameter to explicitly
	 * specify a network interface.
	 */
	if (!interface) {
		pr_info("searching for ravb interface\n");
		rcu_read_lock();
		/* search for ravb interface */
		for_each_netdev_rcu(&init_net, ndev) {
			if (match_dev(ndev)) {
				dev_hold(ndev);
				interface = ndev->name;
				break;
			}
		}
		rcu_read_unlock();

		if (!interface) {
			pr_err("no supported network interfaces\n");
			goto no_device;
		}
	} else {
		pr_info("%s explicitly specified by user\n", interface);
		ndev = dev_get_by_name(&init_net, interface);

		if (!ndev)
			goto no_device;

		if (!match_dev(ndev)) {
			pr_err("unsupport network interface\n");
			goto no_device_match;
		}
	}

	pr_info("init: start(%s)\n", interface);

	pdev_dev = ndev->dev.parent;

	priv = netdev_priv(ndev);

	err = -ENOMEM;

	stp = vzalloc(sizeof(*stp));
	if (unlikely(!stp))
		goto no_memory;

	stp_ptr = stp;

	/* create class */
	stp->avb_class = class_create(THIS_MODULE, "avb");
	if (IS_ERR(stp->avb_class)) {
		err = PTR_RET(stp->avb_class);
		pr_err("init: failed to create avb class\n");
		goto no_class;
	}

	err = -ENODEV;

	if (major) {
		stp->dev = MKDEV(major, 0);
		err = register_chrdev_region(stp->dev,
					     AVB_MINOR_RANGE,
					     "avb");
	} else {
		err = alloc_chrdev_region(&stp->dev,
					  0,
					  AVB_MINOR_RANGE,
					  "avb");
	}

	if (err < 0) {
		pr_err("init: failed to register avb character devices\n");
		goto no_region;
	}
	major = MAJOR(stp->dev);

	cdev_init(&stp->cdev, &ravb_streaming_fops);
	stp->cdev.owner = THIS_MODULE;

	err = cdev_add(&stp->cdev, stp->dev, AVB_MINOR_RANGE);
	if (err) {
		pr_err("init: failed to add avb device\n");
		goto no_register;
	}

	/* Initialize separation filter */
	err = separation_filter_initialise(ndev);
	if (err) {
		pr_err("init: failed to separation filter initialize\n");
		goto err_initdevice;
	}

	/* initialize streaming private */
	sema_init(&stp->sem, 1);

	/* create entry cache */
	streaming_entry_cache = kmem_cache_create("avb_entry_cache",
						  sizeof(struct stream_entry),
						  0,
						  0,
						  NULL);

	INIT_LIST_HEAD(&stp->userpages);

	/* device initialize */
	dev = &stp->device;
	device_initialize(dev);
	dev->parent = &ndev->dev;
	dev->class = stp->avb_class;
	dev->devt = MKDEV(major, AVB_CTRL_MINOR);
	dev_set_drvdata(dev, stp);
	dev->groups = stp_sysfs_groups;
	dev->release = stp_dev_release;
	dev_set_name(dev, "avb_ctrl");

	err = device_add(dev);
	if (err)
		goto err_initstp;

	if (priv->chip_id == RCAR_GEN2) {
		err = devm_request_irq(dev,
				       ndev->irq,
				       ravb_streaming_interrupt,
				       IRQF_SHARED,
				       "avb_streaming",
				       stp);
		if (err) {
			pr_err("request_irq error\n");
			goto err_initirq;
		}
	}

	/* initialize hwqueue info */
	for (i = 0; i < RAVB_HWQUEUE_NUM; i++) {
		err = -ENOMEM;

		hwq = &stp->hwqueueInfoTable[i];
		hwq->index = i;
		hwq->tx = (hwq->index < RAVB_HWQUEUE_TXNUM);
		hwq->qno = hwq->index + RAVB_HWQUEUE_RESERVEDNUM +
			(RAVB_HWQUEUE_RESERVEDNUM * !hwq->tx);
		hwq->chno = hwq->index + RAVB_HWQUEUE_RESERVEDNUM -
			(RAVB_HWQUEUE_TXNUM * !hwq->tx);
		hwq->state = AVB_STATE_IDLE;
		hwq->ringsize = RAVB_RINGSIZE - 1;
		hwq->ring = dma_alloc_coherent(pdev_dev,
					       (hwq->ringsize + 1) * sizeof(*desc),
					       &hwq->ring_dma,
					       GFP_KERNEL);
		if (!hwq->ring) {
			pr_err("init: cannot allocate hw queue ring area\n");
			goto err_inithwqueue;
		}
		if ((u64)hwq->ring_dma >= 0x100000000UL) {
			pr_err("ring_format: 32bit over address(ring_dma=%pad)\n",
			       &hwq->ring_dma);
			goto err_inithwqueue;
		}

		/* register chain in DBAT */
		desc = (struct ravb_desc *)&priv->desc_bat[hwq->qno];
		desc->dptr = cpu_to_le32((u32)hwq->ring_dma);
		desc->die_dt = DT_LINKFIX;
		/* clear descriptor chain */
		clear_desc(hwq);
		hwq->minremain = hwq->remain;
		hwq->irq_coalesce_frame_count = (hwq->tx) ?
			irq_coalesce_frame_tx : irq_coalesce_frame_rx;

		sema_init(&hwq->sem, 1);
		init_waitqueue_head(&hwq->waitEvent);
		INIT_LIST_HEAD(&hwq->activeStreamQueue);
		INIT_LIST_HEAD(&hwq->completeWaitQueue);

		/* device initialize */
		dev = &hwq->device;
		device_initialize(dev);
		dev->parent = &stp->device;
		dev->class = stp->avb_class;
		dev->devt = MKDEV(major, hwq->index);
		dev_set_drvdata(dev, hwq);
		dev->groups = (hwq->tx) ?
			hwq_sysfs_groups_tx : hwq_sysfs_groups_rx;
		dev->release = hwq_dev_release;
		dev_set_name(dev,
			     (hwq->tx) ? "avb_tx%d" : "avb_rx%d",
			     hwq->index - (RAVB_HWQUEUE_TXNUM * !hwq->tx));
		err = device_add(dev);
		if (err)
			goto err_inithwqueue;
		hwq->device_add_flag = true;

		hwq->attached = kset_create_and_add("attached",
						    NULL,
						    &hwq->device.kobj);
		if (!hwq->attached) {
			err = -ENOMEM;
			goto err_inithwqueue;
		}

		sprintf(taskname, hwq_name(hwq));
		hwq->task = kthread_run(ravb_hwq_task, hwq, taskname);
		if (IS_ERR(hwq->task)) {
			pr_err("init: cannot run AVB streaming task\n");
			err = PTR_RET(hwq->task);
			hwq->task = NULL;
			goto err_inithwqueue;
		}

		hrtimer_init(&hwq->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hwq->timer.function = ravb_streaming_timer_handler;

		if (priv->chip_id == RCAR_GEN3) {
			irq_name = ravb_streaming_irqs[i];
			irq = platform_get_irq_byname(priv->pdev, irq_name);
			if (irq < 0) {
				pr_err("init: unsupport irq name (%s)\n",
				       irq_name);
				err = irq;
				goto err_inithwqueue;
			}
			irq_name2 = devm_kasprintf(dev, GFP_KERNEL,
						   "%s:%s:%s", ndev->name,
						   irq_name, hwq_name(hwq));
			err = devm_request_irq(dev,
					       irq,
					       ravb_streaming_interrupt_rxtx,
					       0,
					       irq_name2,
					       hwq);
			if (err) {
				pr_err("request_irq(%d,%s) error\n",
				       irq, irq_name2);
				goto err_inithwqueue;
			}
			hwq->irq = irq;
		}
	}

	pr_info("init: success\n");

	return 0;

err_inithwqueue:
	for (i = 0; i < RAVB_HWQUEUE_NUM; i++) {
		hwq = &stp->hwqueueInfoTable[i];
		if (hwq->task)
			kthread_stop(hwq->task);
		if (hwq->attached)
			kset_unregister(hwq->attached);
		if (hwq->device_add_flag)
			device_unregister(&hwq->device);

		if (hwq->ring) {
			dma_free_coherent(pdev_dev,
					  (hwq->ringsize + 1) * sizeof(*desc),
					  hwq->ring,
					  hwq->ring_dma);
		}

		hwq->ring = NULL;
		hwq->ring_dma = 0;
	}
err_initirq:
	device_unregister(&stp->device);
err_initstp:
	kmem_cache_destroy(streaming_entry_cache);
err_initdevice:
	cdev_del(&stp->cdev);
no_register:
	unregister_chrdev_region(stp->dev, RAVB_HWQUEUE_NUM);
no_region:
	class_destroy(stp->avb_class);
no_class:
	vfree(stp);
	stp_ptr = NULL;
no_memory:
no_device_match:
	dev_put(ndev);
no_device:
	stp_ptr = NULL;

	pr_info("init: failed\n");

	return err;
}

/**
 * cleanup streaming API
 */
static void ravb_streaming_cleanup(void)
{
	int i, j;
	struct hwqueue_info *hwq;
	struct stream_entry *e, *e1;
	struct ravb_user_page *userpage, *userpage1;
	struct streaming_private *stp = stp_ptr;
	struct net_device *ndev = to_net_dev(stp->device.parent);
	struct ravb_private *priv = netdev_priv(ndev);
	struct device *pdev_dev = ndev->dev.parent;
	struct ravb_desc *desc;

	pr_info("cleanup: start\n");

	unregister_chrdev_region(stp->dev, AVB_MINOR_RANGE);
	cdev_del(&stp->cdev);

	/* cleanup hwqueue info */
	for (i = 0; i < RAVB_HWQUEUE_NUM; i++) {
		hwq = &stp->hwqueueInfoTable[i];
		if (hwq->task) {
			hwq_event(hwq, AVB_EVENT_UNLOAD, -1);
			kthread_stop(hwq->task);
		}

		/* write EOS for hw terminate */
		desc = (struct ravb_desc *)&priv->desc_bat[hwq->qno];
		desc->die_dt = DT_EOS;
		/* force reload chain */
		ravb_reload_chain(ndev, hwq->qno);

		/* cleanup stream queue info */
		for (j = 0; j < ((hwq->tx) ? RAVB_STQUEUE_NUM : 1); j++)
			if (test_and_clear_bit(j, hwq->stream_map))
				put_stq(hwq->stqueueInfoTable[j]);

		list_for_each_entry_safe(e, e1, &hwq->completeWaitQueue, list)
			put_streaming_entry(e);
		if (hwq->attached)
			kset_unregister(hwq->attached);
		if (hwq->device_add_flag)
			device_unregister(&hwq->device);

		if (hwq->ring) {
			dma_free_coherent(pdev_dev,
					  (hwq->ringsize + 1) * sizeof(*desc),
					  hwq->ring,
					  hwq->ring_dma);
		}
	}

	device_unregister(&stp->device);
	class_destroy(stp->avb_class);

	/* destroy entry cache */
	kmem_cache_destroy(streaming_entry_cache);

	/* cleanup user pages */
	list_for_each_entry_safe(userpage, userpage1, &stp->userpages, list)
		put_userpage(userpage);

	stp_ptr = NULL;

	vfree(stp);

	dev_put(ndev);

	pr_info("cleanup: end\n");
}

module_init(ravb_streaming_init);
module_exit(ravb_streaming_cleanup);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("Renesas AVB Streaming Driver");
MODULE_LICENSE("Dual MIT/GPL");

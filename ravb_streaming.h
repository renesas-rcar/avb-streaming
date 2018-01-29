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

#ifndef __RAVB_STREAMING_H__
#define __RAVB_STREAMING_H__

#include "ravb_eavb.h"

/* number of driver managed stream queue */
#define RAVB_STQUEUE_TXNUM (EAVB_TXSTREAMNUM)
#define RAVB_STQUEUE_RXNUM (EAVB_RXSTREAMNUM)
#define RAVB_STQUEUE_NUM \
		((RAVB_STQUEUE_TXNUM > RAVB_STQUEUE_RXNUM) ? \
	 RAVB_STQUEUE_TXNUM : RAVB_STQUEUE_RXNUM)

/**
 * Number of HW queue resource which is
 * under control AVB streaming driver
 */
#define RAVB_HWQUEUE_RESERVEDNUM (2) /* BestEffort, NetworkControl */
#define RAVB_HWQUEUE_TXNUM (2)
#define RAVB_HWQUEUE_RXNUM (16)
#define RAVB_HWQUEUE_NUM \
	(RAVB_HWQUEUE_TXNUM + RAVB_HWQUEUE_RXNUM) /* exclude BE/NC queue */

/* ringsize of descriptor chain */
#define RAVB_RINGSIZE (256)

/* maximum number of entry each streaming device */
#define RAVB_ENTRY_THRETH (RAVB_RINGSIZE)

/* CBS bandwidth acceptable limit */
#define RAVB_CBS_BANDWIDTH_LIMIT \
	((u64)((U32_MAX * 750000ull) / 1000000ull)) /* 75% */

/* transmit descriptor processed interrupt bit offset */
#define TDP_BIT_OFFSET	16
/* receive descriptor processed interrupt bit offset */
#define RDP_BIT_OFFSET	0

/* descriptor processed interrupt factor */
enum DESC_DIE_DPF {
	DESC_DIE_DPF_NONE = 0,
	DESC_DIE_DPF_01,
	DESC_DIE_DPF_02,
	DESC_DIE_DPF_03,
	DESC_DIE_DPF_04,
	DESC_DIE_DPF_05,
	DESC_DIE_DPF_06,
	DESC_DIE_DPF_07,
	DESC_DIE_DPF_08,
	DESC_DIE_DPF_09,
	DESC_DIE_DPF_10,
	DESC_DIE_DPF_11,
	DESC_DIE_DPF_12,
	DESC_DIE_DPF_13,
	DESC_DIE_DPF_14,
	DESC_DIE_DPF_15
};

struct ravb_user_page {
	struct page *page;
	dma_addr_t page_dma;
	struct list_head list;
};

struct stream_entry {
	int vecsize;
	int total_bytes;
	int errors;
	struct ravb_desc *descs[EAVB_ENTRYVECNUM];
	struct ravb_desc pre_enc[EAVB_ENTRYVECNUM];
	dma_addr_t dma_descs[EAVB_ENTRYVECNUM];
	struct eavb_entry msg;

	struct stqueue_info *stq;
	struct list_head list;
};

enum AVB_EVENT {
	AVB_EVENT_CLEAR   = 0x00000000,
	AVB_EVENT_ATTACH  = 0x00000001,
	AVB_EVENT_DETACH  = 0x00000002,
	AVB_EVENT_TXINT   = 0x00000010,
	AVB_EVENT_RXINT   = 0x00000020,
	AVB_EVENT_TIMEOUT = 0x00000040,
	AVB_EVENT_UNLOAD  = 0x00000100,
};

struct schedule_info {
};

enum AVB_STATE {
	AVB_STATE_SLEEP,
	AVB_STATE_IDLE,
	AVB_STATE_ACTIVE,
	AVB_STATE_WAITCOMPLETE,
};

/* structure of packet statistics */
struct packet_stats {
	u64 rx_packets;		/* total packets received	*/
	u64 tx_packets;		/* total packets transmitted	*/
	u64 rx_bytes;		/* total bytes received		*/
	u64 tx_bytes;		/* total bytes transmitted	*/
	u64 rx_errors;		/* bad packets received		*/
	u64 tx_errors;		/* packet transmit problems	*/

	/* detailed rx_errors: */
	u64 rx_length_errors;
	u64 rx_over_errors;	/* receiver ring buff overflow	*/
	u64 rx_crc_errors;	/* recved pkt with crc error	*/
	u64 rx_frame_errors;	/* recv'd frame alignment error */
	u64 rx_fifo_errors;	/* recv'r fifo overrun		*/
	u64 rx_missed_errors;	/* receiver missed packet	*/
};

/* structure of driver statistics */
struct driver_stats {
	/* received/transmitted interrupts */
	u64 rx_interrupts;
	u64 tx_interrupts;
	/* built descriptor as a Rx/Tx chain */
	u64 rx_current;
	u64 tx_current;
	/* consumption descriptor as a Rx/Tx chain */
	u64 rx_dirty;
	u64 tx_dirty;
	/* current entries in a wait queue for Rx/Tx */
	u64 rx_entry_wait;
	u64 tx_entry_wait;
	/* current entries in a complete queue for Rx/Tx */
	u64 rx_entry_complete;
	u64 tx_entry_complete;
};

/* structure of stream queue */
struct stqueue_info {
	u32 index;
	enum AVB_STATE state;

	int qno;

	struct eavb_entrynum entrynum;
	enum eavb_block blockmode;
	struct eavb_cbsparam cbs;
	struct schedule_info schedInfo;

	struct list_head entryWaitQueue;
	struct list_head entryLogQueue;
	struct list_head userpages;

	struct packet_stats pstats;
	struct driver_stats dstats;

	struct hwqueue_info *hwq;

	struct kobject kobj;
	wait_queue_head_t waitEvent;

	struct list_head list;

	unsigned int flags;

	struct eavb_entry ebuf[RAVB_ENTRY_THRETH];
	bool cancel;
};

#define to_stq(x) container_of(x, struct stqueue_info, kobj)
#define stq_name(x) kobject_name(&(x)->kobj)

/* structure of HW queue */
struct hwqueue_info {
	s32 index; /* 0-1:Tx, 2-:Rx */
	enum AVB_STATE state;
	int defunct;

	bool tx;
	int chno;
	int qno;
	u8 streamID[8];

	dma_addr_t ring_dma;
	struct ravb_desc *ring;
	s32 ringsize;
	s32 curr;
	s32 remain;
	s32 minremain;
	s32 pendingEvents;

	struct semaphore sem;

	struct list_head activeStreamQueue;
	struct list_head completeWaitQueue;

	struct packet_stats pstats;
	struct driver_stats dstats;

	DECLARE_BITMAP(stream_map, RAVB_STQUEUE_NUM);
	struct stqueue_info *stqueueInfoTable[RAVB_STQUEUE_NUM];
	struct kset *attached;

	struct device device;
	bool device_add_flag;
	wait_queue_head_t waitEvent;
	struct task_struct *task;
	struct hrtimer timer;
	int irq;
	int irq_coalesce_frame_count;
};

#define hwq_name(x) kobject_name(&(x)->device.kobj)

/* structure of streaming API */
struct streaming_private {
	struct hwqueue_info hwqueueInfoTable[RAVB_HWQUEUE_NUM];
	struct eavb_cbsinfo cbsInfo;

	struct class *avb_class;
	dev_t dev;
	struct cdev cdev;
	struct device device;

	struct semaphore sem;

	struct list_head userpages;
};

#define to_stp(x) container_of(x, struct streaming_private, device)
#define stp_name(x) kobject_name(&(x)->device.kobj)

extern struct streaming_private *stp_ptr;

int register_streamID(struct hwqueue_info *hwq, u8 streamID[8]);
const char *avb_state_to_str(enum AVB_STATE state);

#endif	/* #ifndef __RAVB_STREAMING_H__ */

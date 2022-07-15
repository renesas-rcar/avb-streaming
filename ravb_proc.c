/*************************************************************************/ /*
 avb-streaming

 Copyright (C) 2014-2018,2020-2021 Renesas Electronics Corporation

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
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/cdev.h>
#include <linux/sh_eth.h>

#include <linux/net_tstamp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include "../drivers/net/ethernet/renesas/ravb.h"
#include "ravb_eavb.h"
#include "ravb_streaming.h"

static char *interface;
module_param(interface, charp, 0440);

static bool debug;
module_param(debug, bool, 0440);

/**
 *  /proc/avb/hw/
 *
 *  /proc/avb/hw/descriptors
 *  Queue  Type  Size    Used   Free   Min.Free  State
 *  S15      Tx  9999    9999   9999       9999  xxxxxxxx
 *
 *  /proc/avb/hw/filters
 *  Stream Type Queue Chno  Filter
 *  0      tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
 *  1      tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
 *  ....
 *  15     tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
 *
 *  /proc/avb/hw/timestamps
 *  TX  {setting, setting}
 *  RX  {setting, setting}
 *
 *  /proc/avb/hw/cbs
 *  Stream  Type Fraction IdleSlope SendSlope HiCredit LoCredit
 *  0       Tx       nnn% nnnnnnnnn nnnnnnnnn nnnnnnnn nnnnnnnn
 *  1
 *
 *  To Do:
 *  /proc/avb/driver/
 *
 *  /proc/avb/driver/queues/
 *  {type} {current used} {maximum used} {current remain} {Max allowed}
 *  EntryFreeQueue
 *  MessageFreeQueue
 *  HardwareQueue_TX{n}_ActiveStreamQueue
 *  HardwareQueue_TX{n}_CompletedStreamQueue
 *  HardwareQueue_TX{n}_EventMessageQueue
 *  HardwareQueue_RX{n}_ActiveStreamQueue
 *  HardwareQueue_RX{n}_CompletedStreamQueue
 *  HardwareQueue_RX{n}_EventMessageQueue
 *
 *  /proc/avb/driver/userpages
 *
 *  /proc/avb/network/
 *
 *  /proc/avb/network/{rx|tx}
 *  Class       Frames        Bytes Errors Frames/Sec Bytes/Sec
 *  S15   123456789012 123456789012 123456  123456789 123456789
 *  To Do:
 *   o Tx Latency (PHY <-> FIFO time)
 *   o Rx Latency (PHY <-> FIFO time)
 *
 *  To Do:
 *  /proc/avb/network/errors
 *  ICD=nnn
 *  Tx Timeout=nnn
 *  Rx CRC=nnn, RFE=nnn, RTLF=nnn, RTFS=nnn, CEEF=nnn
 *
 *  /proc/avb/network/phy
 *  priv->phydev->phy_id = {n}
 *  priv->link 1 = UP, 0 = DOWN
 *  priv->phy_interface = PHY_INTERFACE_MODE_XXX
 *  priv->msg_enable = {n}
 *  priv->speed = 10/100/1000
 *  priv->duplex 1 = FD, 0 = HD;
 *  priv->no_ether_link:1
 *  priv->ether_link_active_low:1
 */

enum RAVB_PROC_COLLECT {
	RAVB_PROC_COLLECT_CURRENT = 0,
	RAVB_PROC_COLLECT_PREV,
	RAVB_PROC_COLLECT_NUM,
};

/**
 * @brief  AVB TX class passed from MAC layer to HW Layer
 */
enum AVB_TX_CLASS {
	AVB_TX_CLASS_BEST_EFFORT = 0,     /*  */
	AVB_TX_CLASS_NETWORK_CONTROL,     /*  */
	AVB_TX_CLASS_STREAM_B,            /*  */
	AVB_TX_CLASS_STREAM_A,            /*  */
	AVB_TX_CLASS_NUM
};

/**
 * @brief  AVB RX class passed from MAC layer to HW Layer
 */
enum AVB_RX_CLASS {
	AVB_RX_CLASS_BEST_EFFORT = 0,
	AVB_RX_CLASS_NETWORK_CONTROL,
	AVB_RX_CLASS_STREAM_0,
	AVB_RX_CLASS_STREAM_1,
	AVB_RX_CLASS_STREAM_2,
	AVB_RX_CLASS_STREAM_3,
	AVB_RX_CLASS_STREAM_4,
	AVB_RX_CLASS_STREAM_5,
	AVB_RX_CLASS_STREAM_6,
	AVB_RX_CLASS_STREAM_7,
	AVB_RX_CLASS_STREAM_8,
	AVB_RX_CLASS_STREAM_9,
	AVB_RX_CLASS_STREAM_10,
	AVB_RX_CLASS_STREAM_11,
	AVB_RX_CLASS_STREAM_12,
	AVB_RX_CLASS_STREAM_13,
	AVB_RX_CLASS_STREAM_14,
	AVB_RX_CLASS_STREAM_15,
	AVB_RX_CLASS_NUM
};

/**
 *  @brief  Proc Statistics Collect - common to Tx & Rx
 *
 *  Recording takes place to values.
 */
struct ravb_proc_stats_collect_t {
	u64 frames;            /* @brief Total frames in this queue sent/received */
	u64 bytes;             /* @brief Total bytes in this queue sent/received */
	u64 errors;            /* @brief Total Send/Receive errors in this queue */
	u64 frames_per_second; /* @brief Frames per second sent/received */
	u64 bytes_per_second;  /* @brief Bytes per second sent/received */
};

/**
 *  @brief  Proc Statistics - common to Tx & Rx
 *
 *  Recording takes place to one Collect, so that the other is used to
 *  populate /proc statistics.
 */
struct ravb_proc_stats_t {
	union {
		enum AVB_TX_CLASS tx_class; /* @brief AVB Tx Class */
		enum AVB_RX_CLASS rx_class; /* @brief AVB Rx Class */
	} u;

	/* Errors.ICD (MAC) */
	/* Errors.Tx.Timeout */
	/* Errors.Rx.FIFO Queue Full */
	/* Errors.Rx.No descriptors */
	/* CRC, RFE, RTLF, RTFS, CEEF */

	struct ravb_proc_stats_collect_t collect[RAVB_PROC_COLLECT_NUM];

	/* @brief Previous second and currently being recorded */
};

/**
 * @brief  Proc Tx & Rx statistics for all classes
 */
struct ravb_proc_info_t {
	struct net_device          *ndev;
	struct streaming_private   *stp;
	struct timer_list          timer;
	u32			   seconds;
	struct ravb_proc_stats_t tx_stats[AVB_TX_CLASS_NUM];
	struct ravb_proc_stats_t rx_stats[AVB_RX_CLASS_NUM];
};

static struct ravb_proc_info_t ravb_proc_info;

/**
 * @brief  Return the English string for the AVB Driver States
 *
 * @param  state       Driver state
 *
 * @return Pointer to a static string
 */
static const char *query_avb_state(enum AVB_STATE state)
{
	switch (state) {
	case AVB_STATE_SLEEP:        return "sleep";
	case AVB_STATE_IDLE:         return "idle";
	case AVB_STATE_ACTIVE:       return "active";
	case AVB_STATE_WAITCOMPLETE: return "waitcomplete";
	default:                     return "invalid";
	}
}

/**
 * @brief  Return the English string for the AVB TX Class
 *
 * @param  cls         HW Tx Class
 *
 * @return Pointer to a static string
 */
static const char *query_avb_tx_class(enum AVB_TX_CLASS cls)
{
	switch (cls) {
	case AVB_TX_CLASS_BEST_EFFORT:     return "BE";
	case AVB_TX_CLASS_NETWORK_CONTROL: return "NC";
	case AVB_TX_CLASS_STREAM_B:        return "S0";
	case AVB_TX_CLASS_STREAM_A:        return "S1";
	default:                           return "??";
	}
}

/**
 * @brief  Return the English string for the AVB RX Class
 *
 * @param  cls         HW Rx Class
 *
 * @return Pointer to a static string
 */
static const char *query_avb_rx_class(enum AVB_RX_CLASS cls)
{
	switch (cls) {
	case AVB_RX_CLASS_BEST_EFFORT:     return "BE";
	case AVB_RX_CLASS_NETWORK_CONTROL: return "NC";
	case AVB_RX_CLASS_STREAM_0:        return "S0";
	case AVB_RX_CLASS_STREAM_1:        return "S1";
	case AVB_RX_CLASS_STREAM_2:        return "S2";
	case AVB_RX_CLASS_STREAM_3:        return "S3";
	case AVB_RX_CLASS_STREAM_4:        return "S4";
	case AVB_RX_CLASS_STREAM_5:        return "S5";
	case AVB_RX_CLASS_STREAM_6:        return "S6";
	case AVB_RX_CLASS_STREAM_7:        return "S7";
	case AVB_RX_CLASS_STREAM_8:        return "S8";
	case AVB_RX_CLASS_STREAM_9:        return "S9";
	case AVB_RX_CLASS_STREAM_10:       return "S10";
	case AVB_RX_CLASS_STREAM_11:       return "S11";
	case AVB_RX_CLASS_STREAM_12:       return "S12";
	case AVB_RX_CLASS_STREAM_13:       return "S13";
	case AVB_RX_CLASS_STREAM_14:       return "S14";
	case AVB_RX_CLASS_STREAM_15:       return "S15";
	default:                           return "???";
	}
}

/**
 * @brief  Return the English string for the Timestamp Rx control
 *
 * @param  rx_ctrl Timestamp Rx control value
 *
 * @return Pointer to a static string
 */
static const char *query_tstamp_rx_ctrl(u32 rx_ctrl)
{
	if (rx_ctrl & RAVB_RXTSTAMP_ENABLED) {
		switch (rx_ctrl & RAVB_RXTSTAMP_TYPE) {
		case RAVB_RXTSTAMP_TYPE_ALL:            return ",All";
		case RAVB_RXTSTAMP_TYPE_V2_L2_EVENT:    return ",V2L2";
		default:                                return ",Unknown";
		}
	} else {
		return "";
	}
}

/**
 * @brief  Return the English string for the PHY interface
 *
 * @param  phy_interface Phy interface value
 *
 * @return Pointer to a static string
 */
static const char *query_phy_interface(phy_interface_t phy_interface)
{
	switch (phy_interface) {
	case PHY_INTERFACE_MODE_GMII:           return "GMII";
	case PHY_INTERFACE_MODE_MII:            return "MII";
	case PHY_INTERFACE_MODE_RMII:           return "RMII";
	case PHY_INTERFACE_MODE_NA:             return "NA";
	case PHY_INTERFACE_MODE_SGMII:          return "SGMII";
	case PHY_INTERFACE_MODE_TBI:            return "TBI";
	case PHY_INTERFACE_MODE_REVMII:         return "REVMII";
	case PHY_INTERFACE_MODE_RGMII:          return "RGMII";
	case PHY_INTERFACE_MODE_RGMII_ID:       return "RGMII_ID";
	case PHY_INTERFACE_MODE_RGMII_RXID:     return "RGMII_RXID";
	case PHY_INTERFACE_MODE_RGMII_TXID:     return "RGMII_TXID";
	case PHY_INTERFACE_MODE_RTBI:           return "RTBI";
	case PHY_INTERFACE_MODE_SMII:           return "SMII";
	case PHY_INTERFACE_MODE_XGMII:          return "XGMII";
	case PHY_INTERFACE_MODE_MOCA:           return "MOCA";
	case PHY_INTERFACE_MODE_QSGMII:         return "QSGMII";
	default:                                return "UNKNOWN";
	}
}

/**
 * @brief  Collect statistics counters from ravb driver
 */
static void stats_collect_update(void)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct net_device *ndev = info->ndev;
	struct ravb_private *priv = netdev_priv(ndev);
	struct streaming_private *stp = info->stp;
	struct hwqueue_info *hwq;
	struct stqueue_info *stq;
	struct kobject *stq_kobj;

	struct ravb_proc_stats_t *stats;
	struct ravb_proc_stats_collect_t *collect;

	int h;

	/* Collect best effort/network control queue */
	for (h = 0; h < 2; h++) {
		stats = &info->rx_stats[h];
		collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];

		collect->frames = priv->stats[h].rx_packets;
		collect->bytes = priv->stats[h].rx_bytes;
		collect->errors = priv->stats[h].rx_errors +
			priv->stats[h].rx_over_errors +
			priv->stats[h].rx_fifo_errors;

		stats = &info->tx_stats[h];
		collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];

		collect->frames = priv->stats[h].tx_packets;
		collect->bytes = priv->stats[h].tx_bytes;
		collect->errors = priv->stats[h].tx_errors +
			priv->stats[h].tx_carrier_errors;
	}

	/* Collect streaming queues */
	for (h = 0; h < ARRAY_SIZE(stp->hwqueueInfoTable); h++) {
		hwq = &stp->hwqueueInfoTable[h];
		if (hwq->tx) {
			stats = &info->tx_stats[hwq->chno];
			collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];

			collect->frames = hwq->pstats.tx_packets;
			collect->bytes = hwq->pstats.tx_bytes;
			collect->errors = hwq->pstats.tx_errors;

			list_for_each_entry(stq_kobj, &hwq->attached->list, entry) {
				stq = to_stq(stq_kobj);
				collect->frames += stq->pstats.tx_packets;
				collect->bytes += stq->pstats.tx_bytes;
				collect->errors += stq->pstats.tx_errors;
			}
		} else {
			stats = &info->rx_stats[hwq->chno];
			collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];

			collect->frames = hwq->pstats.rx_packets;
			collect->bytes = hwq->pstats.rx_bytes;
			collect->errors = hwq->pstats.rx_errors;

			list_for_each_entry(stq_kobj, &hwq->attached->list, entry) {
				stq = to_stq(stq_kobj);
				collect->frames += stq->pstats.rx_packets;
				collect->bytes += stq->pstats.rx_bytes;
				collect->errors += stq->pstats.rx_errors;
			}
		}
	}
}

/**
 * @brief  Show formatted Stream RX/TX statistics
 */
static int stats_show_network(struct seq_file *m, void *v, bool tx)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct ravb_proc_stats_t *stats;
	struct ravb_proc_stats_collect_t *collect_prev;
	struct ravb_proc_stats_collect_t *collect;
	struct ravb_proc_stats_collect_t collect_total;

	int a = 0;
	int array_size;
	const char *class_name;

	/**
	 * /proc/avb/network/{rx|tx}
	 * Class       Frames        Bytes Errors Bytes/Sec Frames/Sec
	 * S15   123456789012 123456789012 123456 123456789  123456789
	 */

	memset(&collect_total, 0, sizeof(collect_total));

	seq_puts(m, "Class       Frames        Bytes Errors Frames/Sec Bytes/Sec\n");

	stats_collect_update();

	array_size = (tx) ?
		 ARRAY_SIZE(info->tx_stats) : ARRAY_SIZE(info->rx_stats);

	for (a = 0; a < array_size; a++) {
		if (tx) {
			stats = &info->tx_stats[a];
			class_name = query_avb_tx_class(stats->u.tx_class);
		} else {
			stats = &info->rx_stats[a];
			class_name = query_avb_rx_class(stats->u.rx_class);
		}

		collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];
		collect_prev = &stats->collect[RAVB_PROC_COLLECT_PREV];

		seq_printf(m,
			   "%-5s %12llu %12llu %6llu  %9llu %9llu\n",
			   class_name,
			   collect->frames,
			   collect->bytes,
			   collect->errors,
			   collect_prev->frames_per_second,
			   collect_prev->bytes_per_second);

		collect_total.frames            += collect->frames;
		collect_total.bytes             += collect->bytes;
		collect_total.errors            += collect->errors;
		collect_total.frames_per_second += collect_prev->frames_per_second;
		collect_total.bytes_per_second  += collect_prev->bytes_per_second;
	}

	seq_printf(m,
		   "      ------------ ------------ ------ ---------- ---------\n"
		   "      %12llu %12llu %6llu  %9llu %9llu\n",
		   collect_total.frames,
		   collect_total.bytes,
		   collect_total.errors,
		   collect_total.frames_per_second,
		   collect_total.bytes_per_second);

	return 0;
}

/**
 * @brief  Show formatted Stream RX statistics
 */
static int stats_show_network_rx(struct seq_file *m, void *v)
{
	return stats_show_network(m, v, false);
}

/**
 * @brief  Show formatted Stream TX statistics
 */
static int stats_show_network_tx(struct seq_file *m, void *v)
{
	return stats_show_network(m, v, true);
}

/**
 * @brief  Show formatted PHY status
 */
static int stats_show_network_phy(struct seq_file *m, void *v)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct net_device *ndev = info->ndev;
	struct ravb_private *priv = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	seq_printf(m,
		   "Link:      %s\n"
		   "Speed:     %u\n"
		   "ID:        %u\n"
		   "Interface: %u (%s)\n",
		   priv->link ? "Up" : "Down",
		   priv->speed,
		   phydev->phy_id,
		   priv->phy_interface,
		   query_phy_interface(priv->phy_interface));

	return 0;
}

#define HW_DESCRIPTORS_FORMAT "%-3s      %2s  %4u    %4u   %4u       %4u  %s\n"

/**
 * @brief  Show formatted Driver/HW Descriptor statistics
 */
static int stats_show_hw_descriptors(struct seq_file *m, void *v)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct net_device *ndev = info->ndev;
	struct ravb_private *priv = netdev_priv(ndev);
	struct streaming_private *stp = info->stp;
	struct hwqueue_info *hwq;

	int h = 0;

	/**
	 * /proc/avb/hw/descriptors
	 * Queue  Type  Size    Used   Free   Min.Free  State
	 * S15      Tx  9999    9999   9999       9999  xxxxxxxx
	 */
	seq_puts(m, "Queue  Type  Size    Used   Free   Min.Free  State\n");

	/* Show best effort/network control tx queue */
	for (h = 0; h < 2; h++)
		seq_printf(m,
			   HW_DESCRIPTORS_FORMAT,
			   query_avb_tx_class(h),
			   "Tx",
			   priv->num_tx_ring[h],
			   priv->cur_tx[h] - priv->dirty_tx[h],
			   priv->num_tx_ring[h] - (priv->cur_tx[h] - priv->dirty_tx[h]),
			   0, /* reserved */
			   (priv->cur_tx[h] - priv->dirty_tx[h]) ? "active" : "idle");

	/* Show streaming tx queue */
	for (h = 0; h < ARRAY_SIZE(stp->hwqueueInfoTable); h++) {
		hwq = &stp->hwqueueInfoTable[h];
		if (!hwq->tx)
			continue;

		seq_printf(m,
			   HW_DESCRIPTORS_FORMAT,
			   query_avb_tx_class(hwq->chno),
			   "Tx",
			   hwq->ringsize,
			   hwq->ringsize - hwq->remain,
			   hwq->remain,
			   hwq->minremain,
			   query_avb_state(hwq->state));
	}

	/* Show best effort/network control rx queue */
	for (h = 0; h < 2; h++)
		seq_printf(m,
			   HW_DESCRIPTORS_FORMAT,
			   query_avb_rx_class(h),
			   "Rx",
			   priv->num_rx_ring[h],
			   priv->cur_rx[h] - priv->dirty_rx[h],
			   priv->num_rx_ring[h] - (priv->cur_rx[h] - priv->dirty_rx[h]),
			   0, /* reserved */
			   (priv->cur_rx[h] - priv->dirty_rx[h]) ? "active" : "idle");

	/* Show streaming rx queue */
	for (h = 0; h < ARRAY_SIZE(stp->hwqueueInfoTable); h++) {
		hwq = &stp->hwqueueInfoTable[h];
		if (hwq->tx)
			continue;

		seq_printf(m,
			   HW_DESCRIPTORS_FORMAT,
			   query_avb_rx_class(hwq->chno),
			   "Rx",
			   hwq->ringsize,
			   hwq->ringsize - hwq->remain,
			   hwq->remain,
			   hwq->minremain,
			   query_avb_state(hwq->state));
	}

	return 0;
}

/**
 * @brief  Show formatted Stream RX Filters
 */
static int stats_show_hw_filters(struct seq_file *m, void *v)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct streaming_private *stp = info->stp;
	struct hwqueue_info *hwq;

	int h = 0;

	/**
	 * /proc/avb/hw/filters
	 * Stream Type Queue Chno  Filter
	 * 0      tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
	 * 1      tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
	 * ....
	 * 15     tt   nnn         xx:xx:xx:xx:xx:xx:xx:xx
	 */
	seq_puts(m, "Stream Type Queue Chno  Filter\n");
	for (h = 0; h < ARRAY_SIZE(stp->hwqueueInfoTable); h++) {
		hwq = &stp->hwqueueInfoTable[h];

		if (hwq->tx) {
			seq_printf(m,
				   "%-2u     Tx     %3u  %3u\n",
				   hwq->index,
				   hwq->qno,
				   hwq->chno);
		} else {
			seq_printf(m,
				   "%-2u     Rx     %3u  %3u  %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				   hwq->index,
				   hwq->qno,
				   hwq->chno,
				   hwq->streamID[0], hwq->streamID[1],
				   hwq->streamID[2], hwq->streamID[3],
				   hwq->streamID[4], hwq->streamID[5],
				   hwq->streamID[6], hwq->streamID[7]);
		}
	}

	return 0;
}

/**
 * @brief  Show formatted Stream TX Credit Based Shaping
 */
static int stats_show_hw_cbs(struct seq_file *m, void *v)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct streaming_private *stp = info->stp;

	int c = 0;

	/**
	 * /proc/avb/hw/cbs
	 * Stream  Type Fraction IdleSlope SendSlope HiCredit LoCredit
	 * 0       Tx       nnn% nnnnnnnnn nnnnnnnnn nnnnnnnn nnnnnnnn
	 * 1
	 */
	seq_puts(m, "Stream  Type Fraction IdleSlope SendSlope HiCredit LoCredit\n");
	for (c = 0; c < ARRAY_SIZE(stp->cbsInfo.param); c++) {
		seq_printf(m,
			   "%-2u      Tx       %3lu%% %9u %9u %8u %8u\n",
			   c,
			   stp->cbsInfo.param[c].bandwidthFraction / (0xfffffffful / 100),
			   stp->cbsInfo.param[c].idleSlope,
			   -stp->cbsInfo.param[c].sendSlope,
			   stp->cbsInfo.param[c].hiCredit,
			   stp->cbsInfo.param[c].loCredit);
	}

	return 0;
}

/**
 * @brief  Show formatted Tx & Rx timestamping options
 */
static int stats_show_hw_timestamp(struct seq_file *m, void *v)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;
	struct net_device *ndev = info->ndev;
	struct ravb_private *priv = netdev_priv(ndev);

	/**
	 * /proc/avb/hw/timestamps
	 * TX  {setting, setting}
	 * RX  {setting, setting}
	 */
	seq_printf(m,
		   "Tx  %s\n"
		   "Rx  %s%s\n",
		   (priv->tstamp_tx_ctrl & RAVB_TXTSTAMP_ENABLED) ? "Enabled" : "Disabled",
		   (priv->tstamp_rx_ctrl & RAVB_RXTSTAMP_ENABLED) ? "Enabled" : "Disabled",
		   query_tstamp_rx_ctrl(priv->tstamp_rx_ctrl));

	return 0;
}

/**
 * @brief  Show formatted driver queue counts
 */
static int stats_show_driver_queues(struct seq_file *m, void *v)
{
	/* TODO */
	return 0;
}

/**
 * @brief  Show formatted driver userpages
 */
static int stats_show_driver_userpages(struct seq_file *m, void *v)
{
	/* TODO */
	return 0;
}

static int stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, PDE_DATA(inode), NULL);
}

static const struct proc_ops stats_proc_fops = {
	.proc_open              = stats_proc_open,
	.proc_read              = seq_read,
	.proc_lseek             = seq_lseek,
	.proc_release			= single_release,
};

/**
 * @brief  One-second timer
 *
 * @param  arg     Pointer to ravb_proc_info in which created the timer
 */
static void proc_timer_update(struct timer_list *arg)
{
	struct ravb_proc_info_t *info = from_timer(info, arg, timer);
	struct ravb_proc_stats_t *stats;
	struct ravb_proc_stats_collect_t *collect_prev;
	struct ravb_proc_stats_collect_t *collect;

	/* unsigned long           flags = 0; */
	int a = 0;

	/* spin_lock_irqsave(&priv->lock, flags); */

	stats_collect_update();

	info->seconds++;

	for (a = 0; a < ARRAY_SIZE(info->rx_stats); a++) {
		stats = &info->rx_stats[a];
		collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];
		collect_prev = &stats->collect[RAVB_PROC_COLLECT_PREV];

		collect->frames_per_second =
			collect->frames - collect_prev->frames;
		collect->bytes_per_second =
			collect->bytes - collect_prev->bytes;
		memcpy(collect_prev, collect, sizeof(*collect));
	}

	for (a = 0; a < ARRAY_SIZE(info->tx_stats); a++) {
		stats = &info->tx_stats[a];
		collect = &stats->collect[RAVB_PROC_COLLECT_CURRENT];
		collect_prev = &stats->collect[RAVB_PROC_COLLECT_PREV];

		collect->frames_per_second =
			collect->frames - collect_prev->frames;
		collect->bytes_per_second =
			collect->bytes - collect_prev->bytes;
		memcpy(collect_prev, collect, sizeof(*collect));
	}

	/* spin_unlock_irqrestore(&priv->lock, flags); */

	info->timer.expires += (unsigned long)HZ;
	add_timer(&info->timer);
}

/**
 * @brief  Initialise a one second kernel timer
 *	   to produce per-second AVB statistics
 */
static void proc_timer_initialise(void)
{
	struct ravb_proc_info_t *info = &ravb_proc_info;

	timer_setup(&info->timer, proc_timer_update, 0);
	info->timer.expires  = jiffies + (unsigned long)HZ;

	add_timer(&info->timer);
}

struct stats_proc_entry {
	const char *name;
	int (*show)(struct seq_file *, void *);
	bool debug;
};

static const struct stats_proc_entry proc_data_network[] = {
	{ "rx", stats_show_network_rx, false },
	{ "tx", stats_show_network_tx, false },
	{ "phy", stats_show_network_phy, false },
};

static const struct stats_proc_entry proc_data_hw[] = {
	{ "filters", stats_show_hw_filters, false },
	{ "descriptors", stats_show_hw_descriptors, true },
	{ "cbs", stats_show_hw_cbs, false },
	{ "timestamps", stats_show_hw_timestamp, false },
};

static const struct stats_proc_entry proc_data_driver[] = {
	{ "queues", stats_show_driver_queues, true },
	{ "userpages", stats_show_driver_userpages, true },
};

static struct {
	const char *name;
	int n_data;
	const struct stats_proc_entry *data_entries;
	bool debug;
} proc_dir[] = {
	{ "network", ARRAY_SIZE(proc_data_network), proc_data_network, false },
	{ "hw", ARRAY_SIZE(proc_data_hw), proc_data_hw, false },
	{ "driver", ARRAY_SIZE(proc_data_driver), proc_data_driver, true },
};

static struct proc_dir_entry *stats_proc_root;

/**
 * @brief  Create /proc entries
 */
static void stats_create_proc_entry(void)
{
	int i, j;
	struct proc_dir_entry *proc_dir_entry;
	const struct stats_proc_entry *proc_data;

	/* Initialise statistics */
	memset(&ravb_proc_info, 0, sizeof(ravb_proc_info));
	for (i = 0; i < ARRAY_SIZE(ravb_proc_info.rx_stats); i++)
		ravb_proc_info.rx_stats[i].u.rx_class = i;
	for (i = 0; i < ARRAY_SIZE(ravb_proc_info.tx_stats); i++)
		ravb_proc_info.tx_stats[i].u.tx_class = i;

	/* Create the root */
	stats_proc_root = proc_mkdir("avb", NULL);

	/* Create proc data entries */
	for (i = 0; i < ARRAY_SIZE(proc_dir); i++) {
		if (proc_dir[i].debug && !debug)
			continue;

		proc_dir_entry = proc_mkdir(proc_dir[i].name, stats_proc_root);
		proc_data = proc_dir[i].data_entries;
		for (j = 0; j < proc_dir[i].n_data; j++) {
			if (proc_data[j].debug && !debug)
				continue;

			proc_create_data(proc_data[j].name,
					 0444,
					 proc_dir_entry,
					 &stats_proc_fops,
					 proc_data[j].show);
		}
	}
}

/**
 * @brief  Remove /proc entries
 */
static void stats_remove_proc_entry(void)
{
	proc_remove(stats_proc_root);
}

/**
 * @brief  Callback of device find
 */
static int ravb_proc_match(struct device *dev, void *data)
{
	if (!dev_name(dev))
		return 0;
	return !strcmp(dev_name(dev), (char *)data);
}

/**
 * @brief  Check for supported device
 */
static inline bool match_dev(struct net_device *ndev, struct device **rstp_dev)
{
	struct device *stp_dev;

	if (!ndev)
		return false;

	stp_dev = device_find_child(&ndev->dev, "avb_ctrl", ravb_proc_match);
	if (stp_dev && rstp_dev)
		*rstp_dev = stp_dev;

	return stp_dev;
}

/**
 * @brief  Initialise /proc
 */
static int ravb_proc_init(void)
{
	int err = -ENODEV;
	struct net_device *ndev;
	struct streaming_private *stp;
	struct device *stp_dev = NULL;

	pr_info("init enter\n");

	/*
	 * Default behavior is automatically detect the ravb interface
	 * unless the user used (interface) parameter to explicitly
	 * specify a network interface.
	 */
	if (!interface) {
		rcu_read_lock();
		/* search for ravb interface */
		for_each_netdev_rcu(&init_net, ndev) {
			if (match_dev(ndev, &stp_dev)) {
				dev_hold(ndev);
				interface = ndev->name;
				break;
			}
		}
		rcu_read_unlock();
		if (!interface)
			goto no_device;
	} else {
		ndev = dev_get_by_name(&init_net, interface);
		if (!ndev)
			goto no_device;

		if (!match_dev(ndev, &stp_dev))
			goto no_device_match;
	}

	stp = to_stp(stp_dev);

	if (!try_module_get(stp->cdev.owner))
		goto no_device_match;

	pr_info("found AVB device is %s@%s\n",
		netdev_name(ndev), stp_name(stp));

	stats_create_proc_entry();
	ravb_proc_info.ndev = ndev;
	ravb_proc_info.stp = stp;
	proc_timer_initialise();

	pr_info("init finish\n");

	return 0;

no_device_match:
	dev_put(ndev);
no_device:
	pr_err("init failed, error=%d\n", err);
	return err;
}

/**
 * @brief  Cleanup /proc
 */
static void ravb_proc_cleanup(void)
{
	pr_info("cleanup enter\n");

	del_timer(&ravb_proc_info.timer);
	stats_remove_proc_entry();
	module_put(ravb_proc_info.stp->cdev.owner);
	dev_put(ravb_proc_info.ndev);

	pr_info("cleanup finish\n");
}

module_init(ravb_proc_init);
module_exit(ravb_proc_cleanup);

MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_DESCRIPTION("Renesas AVB extension statistics module");
MODULE_LICENSE("Dual MIT/GPL");

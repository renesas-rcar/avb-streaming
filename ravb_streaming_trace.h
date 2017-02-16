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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM avb

#if !defined(__RAVB_STREAMING_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __RAVB_STREAMING_TRACE_H__

#include <linux/tracepoint.h>
#include "ravb_streaming.h"

#define show_avb_states(state) \
	__print_symbolic(state, \
		{ 0, "sleep" }, \
		{ 1, "idle" }, \
		{ 2, "active" }, \
		{ 3, "waitcomplete" })

TRACE_EVENT(avb_state,
	TP_PROTO(s32 index, int stqno, bool stq, enum AVB_STATE state),
	TP_ARGS(index, stqno, stq, state),

	TP_STRUCT__entry(
		__field(s32,		index)
		__field(int,		stqno)
		__field(bool,		stq)
		__field(enum AVB_STATE,	state)
	),
	TP_fast_assign(
		__entry->index	= index;
		__entry->stqno	= stqno;
		__entry->stq	= stq;
		__entry->state	= state;
	),
	TP_printk("hwq.%d.%d: %s %s",
		__entry->index,
		__entry->stqno,
		(__entry->stq) ? "stq" : "hwq",
		show_avb_states(__entry->state))
);

#define trace_avb_hwq_state(index, state) \
	trace_avb_state(index, -1, 0, state)
#define trace_avb_stq_state(index, stqno, state) \
	trace_avb_state(index, stqno, 1, state)

#define show_avb_events(ev) \
	__print_symbolic(ev, \
		{ 0x0000000, "clear" }, \
		{ 0x0000001, "attach" }, \
		{ 0x0000002, "detach" }, \
		{ 0x0000010, "txint" }, \
		{ 0x0000020, "rxint" }, \
		{ 0x0000040, "timeout" }, \
		{ 0x0000100, "unload" })

TRACE_EVENT(avb_event,
	TP_PROTO(s32 index, enum AVB_STATE state,
		 enum AVB_EVENT ev, s32 pending),
	TP_ARGS(index, state, ev, pending),

	TP_STRUCT__entry(
		__field(s32,		index)
		__field(enum AVB_STATE,	state)
		__field(enum AVB_EVENT,	ev)
		__field(s32,		pending)
	),
	TP_fast_assign(
		__entry->index		= index;
		__entry->state		= state;
		__entry->ev		= ev;
		__entry->pending	= pending;
	),
	TP_printk("hwq.%d: %s %s 0x%07x",
		__entry->index,
		show_avb_events(__entry->ev),
		show_avb_states(__entry->state),
		__entry->pending)
);

#define show_avb_entry_events(ev) \
	__print_symbolic(ev, \
		{ 1, "get" }, \
		{ 2, "put" }, \
		{ 3, "encode" }, \
		{ 4, "decode" }, \
		{ 5, "accept" })

TRACE_EVENT(avb_entry,
	TP_PROTO(uintptr_t e, int ev),
	TP_ARGS(e, ev),

	TP_STRUCT__entry(
		__field(uintptr_t,	e)
		__field(int,		ev)
	),
	TP_fast_assign(
		__entry->e	= e;
		__entry->ev	= ev;
	),
	TP_printk("0x%08lx %s",
		__entry->e,
		show_avb_entry_events(__entry->ev)
	)
);

#define trace_avb_entry_get(e) \
	trace_avb_entry((uintptr_t)e, 1)
#define trace_avb_entry_put(e) \
	trace_avb_entry((uintptr_t)e, 2)
#define trace_avb_entry_encode(e) \
	trace_avb_entry((uintptr_t)e, 3)
#define trace_avb_entry_decode(e) \
	trace_avb_entry((uintptr_t)e, 4)

#if (EAVB_ENTRYVECNUM != 2)
	#error "TODO need implementation"
#else
TRACE_EVENT(avb_entry_accept,
	TP_PROTO(uintptr_t e, int ev, s32 index, int stqno, u32 seqno,
		 uintptr_t base0, u32 len0, u32 base1, u32 len1),
	TP_ARGS(e, ev, index, stqno,
		seqno, base0, len0, base1, len1),

	TP_STRUCT__entry(
		__field(u32,	e)
		__field(int,	ev)
		__field(s32,	index)
		__field(int,	stqno)
		__field(u32,	seqno)
		__field(u32,	base0)
		__field(u32,	len0)
		__field(u32,	base1)
		__field(u32,	len1)
	),
	TP_fast_assign(
		__entry->e	= e;
		__entry->ev	= ev;
		__entry->index	= index;
		__entry->stqno	= stqno;
		__entry->seqno	= seqno;
		__entry->base0	= base0;
		__entry->len0	= len0;
		__entry->base1	= base1;
		__entry->len1	= len1;
	),
	TP_printk("0x%08x %s hwq.%d.%d 0x%08x 0x%08x %d 0x%08x %d",
		__entry->e,
		show_avb_entry_events(__entry->ev),
		__entry->index,
		__entry->stqno,
		__entry->seqno,
		__entry->base0,
		__entry->len0,
		__entry->base1,
		__entry->len1)
);
#endif

#define trace_avb_entry_accept_wrap(e) \
		trace_avb_entry_accept((uintptr_t)e, 5, \
				e->stq->hwq->index, e->stq->qno, \
				e->msg.seq_no, \
				e->msg.vec[0].base, e->msg.vec[0].len, \
				e->msg.vec[1].base, e->msg.vec[1].len) \

#if !defined(CONFIG_RAVB_STREAMING_FTRACE_DESC)
#define trace_avb_desc(a, b, c, d, e, f, g, h, i)
#else

#define show_avb_dts(dt) \
	__print_symbolic(dt, \
		{ 5,	"FSTART" }, \
		{ 4,	"FMID" }, \
		{ 6,	"FEND" }, \
		{ 7,	"FSINGLE" }, \
		{ 8,	"LINK" }, \
		{ 9,	"LINKFIX" }, \
		{ 10,	"EOS" }, \
		{ 12,	"FEMPTY" }, \
		{ 13,	"FEMPTY_IS" }, \
		{ 14,	"FEMPTY_IC" }, \
		{ 15,	"FEMPTY_ND" }, \
		{ 2,	"LEMPTY" }, \
		{ 3,	"EEMPTY" })

TRACE_EVENT(avb_desc,
	TP_PROTO(s32 index, int stqno, void *e, void *desc, u32 dt,
		 u32 dptr, u32 ds, bool en, bool tx),
	TP_ARGS(index, stqno, e, desc, dt, dptr, ds, en, tx),

	TP_STRUCT__entry(
		__field(s32,	index)
		__field(int,	stqno)
		__field(void *,	e)
		__field(void *,	desc)
		__field(u32,	dt)
		__field(u32,	dptr)
		__field(u32,	ds)
		__field(bool,	en)
		__field(bool,	tx)
	),
	TP_fast_assign(
		__entry->index	= index;
		__entry->stqno	= stqno;
		__entry->e	= e;
		__entry->desc	= desc;
		__entry->dt	= dt;
		__entry->dptr	= dptr;
		__entry->ds	= ds;
		__entry->en	= en;
		__entry->tx	= tx;
	),
	TP_printk("hwq.%d.%d: %p desc.%s.%s %p %s 0x%08x %d",
		  __entry->index,
		  __entry->stqno,
		  __entry->e,
		  __entry->en ? "en" : "de",
		  __entry->tx ? "tx" : "rx",
		  __entry->desc,
		  show_avb_dts(__entry->dt),
		  __entry->dptr,
		  __entry->ds)
);
#endif

#define trace_avb_desc_encode_rx(index, stqno, e, desc, dt, dptr, ds) \
	trace_avb_desc(index, stqno, e, desc, dt, dptr, ds, 1, 0)
#define trace_avb_desc_encode_tx(index, stqno, e, desc, dt, dptr, ds) \
	trace_avb_desc(index, stqno, e, desc, dt, dptr, ds, 1, 1)
#define trace_avb_desc_decode_rx(index, stqno, e, desc, dt, dptr, ds) \
	trace_avb_desc(index, stqno, e, desc, dt, dptr, ds, 0, 0)
#define trace_avb_desc_decode_tx(index, stqno, e, desc, dt, dptr, ds) \
	trace_avb_desc(index, stqno, e, desc, dt, dptr, ds, 0, 1)

#if !defined(CONFIG_RAVB_STREAMING_FTRACE_LOCK)
#define trace_avb_lock(a, b, c, d)
#else

#define show_avb_locktype(locktype) \
	__print_symbolic(locktype, \
		{ 0x0000001, "spin.lock.irq" }, \
		{ 0x0000002, "spin.unlock.irq" }, \
		{ 0x0000003, "spin.lock.noirq" }, \
		{ 0x0000004, "spin.unlock.noirq" }, \
		{ 0x0000011, "wait.wake_up" }, \
		{ 0x0000012, "wait.sleep" }, \
		{ 0x0000021, "sem.take" }, \
		{ 0x0000022, "sem.give" })

TRACE_EVENT(avb_lock,
	TP_PROTO(s32 index, int stqno, int line, int locktype),
	TP_ARGS(index, stqno, line, locktype),

	TP_STRUCT__entry(
		__field(s32,		index)
		__field(int,		stqno)
		__field(int,		line)
		__field(int,		locktype)
	),
	TP_fast_assign(
		__entry->index		= index;
		__entry->stqno		= stqno;
		__entry->line		= line;
		__entry->locktype	= locktype;
	),
	TP_printk("hwq.%d.%d: %04d %s",
		__entry->index,
		__entry->stqno,
		__entry->line,
		show_avb_locktype(__entry->locktype))
);
#endif

#define trace_avb_spin_lock(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000001)
#define trace_avb_spin_unlock(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000002)
#define trace_avb_spin_lock_irqsave(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000003)
#define trace_avb_spin_unlock_irqrestore(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000004)

#define trace_avb_wait_wakeup(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000011)
#define trace_avb_wait_sleep(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000012)

#define trace_avb_sem_take(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000021)
#define trace_avb_sem_give(index, stqno) \
	trace_avb_lock(index, stqno, __LINE__, 0x0000022)

#endif /* __RAVB_STREAMING_TRACE_H__ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE ravb_streaming_trace
#include <trace/define_trace.h>

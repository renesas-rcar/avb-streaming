#ifndef __RAVB_EAVB_H__
#define __RAVB_EAVB_H__

#define EAVB_ENTRYVECNUM (2)
#define EAVB_TXSTREAMNUM (16)
#define EAVB_RXSTREAMNUM (16)

struct eavb_entryvec {
	uint32_t base;
	uint32_t len;
};

struct eavb_entry {
	uint32_t seq_no;
	struct eavb_entryvec vec[EAVB_ENTRYVECNUM];
};

enum eavb_streamclass {
	EAVB_CLASSB,
	EAVB_CLASSA,

	EAVB_CLASS_MAX,
};

struct eavb_cbsparam {
	uint32_t bandwidthFraction;
	uint32_t idleSlope;
	uint32_t sendSlope;
	uint32_t hiCredit;
	uint32_t loCredit;
};

struct eavb_txparam {
	struct eavb_cbsparam cbs;
};

struct eavb_rxparam {
	uint8_t streamid[8];
};

struct eavb_cbsinfo {
	uint32_t bandwidthFraction;
	struct eavb_cbsparam param[EAVB_CLASS_MAX];
};

enum eavb_block {
	EAVB_BLOCK_NOWAIT,
	EAVB_BLOCK_WAITALL,
};

enum eavb_optionid {
	EAVB_OPTIONID_BLOCKMODE = 1
};

struct eavb_option {
	enum eavb_optionid id;
	uint32_t param;
};

struct eavb_entrynum {
	uint32_t accepted;
	uint32_t processed;
	uint32_t completed;
};

#ifdef __KERNEL__
/**
 * Streaming driver I/F function for kernel driver
 */
/**
 * AVB_DEVNAME depends on the driver configuration RAVB_HWQUEUE_TXNUM,
 * RAVB_HWQUEUE_RXNUM, RAVB_HWQUEUE_NUM in the kernel driver internal header.
 */
enum AVB_DEVNAME {
	AVB_DEVNAME_TX0 = 0,
	AVB_DEVNAME_TX1 = 1,
	AVB_DEVNAME_RX0 = 2,
	AVB_DEVNAME_RX1 = 3,
	AVB_DEVNAME_RX2 = 4,
	AVB_DEVNAME_RX3 = 5,
	AVB_DEVNAME_RX4 = 6,
	AVB_DEVNAME_RX5 = 7,
	AVB_DEVNAME_RX6 = 8,
	AVB_DEVNAME_RX7 = 9,
	AVB_DEVNAME_RX8 = 10,
	AVB_DEVNAME_RX9 = 11,
	AVB_DEVNAME_RX10 = 12,
	AVB_DEVNAME_RX11 = 13,
	AVB_DEVNAME_RX12 = 14,
	AVB_DEVNAME_RX13 = 15,
	AVB_DEVNAME_RX14 = 16,
	AVB_DEVNAME_RX15 = 17,
};

struct ravb_streaming_kernel_if {
	void *handle;
	int (*read)(void *handle, struct eavb_entry *buf, unsigned int num);
	int (*write)(void *handle, struct eavb_entry *buf, unsigned int num);
	long (*set_txparam)(void *handle, struct eavb_txparam *txparam);
	long (*get_txparam)(void *handle, struct eavb_txparam *txparam);
	long (*set_rxparam)(void *handle, struct eavb_rxparam *rxparam);
	long (*get_rxparam)(void *handle, struct eavb_rxparam *rxparam);
	long (*set_option)(void *handle, struct eavb_option *option);
	long (*get_option)(void *handle, struct eavb_option *option);
	long (*get_entrynum)(void *handle, struct eavb_entrynum *entrynum);
	long (*blocking_cancel)(void *handle);
};

extern int ravb_streaming_open_stq_kernel(
	enum AVB_DEVNAME dev_name, struct ravb_streaming_kernel_if *kif,
	unsigned int flags);
extern int ravb_streaming_release_stq_kernel(void *handle);
#endif

/**
 * avbtool
 */
/* these strings are set to whatever the driver author decides... */
struct eavb_avbtool_drvinfo {
	uint8_t driver[32];	/* driver short name */
	uint8_t version[32];	/* driver version string */
	uint8_t fw_version[32];	/* firmware version string */
	uint8_t bus_info[32];	/* Bus info for this IF. */
	uint8_t reserved1[32];
	uint8_t reserved2[12];
	uint32_t n_priv_flags;
	uint32_t n_stats;	/* number of uint64_t's from EAVB_GSTATS */
	uint32_t testinfo_len;
	uint32_t eedump_len;
	uint32_t regdump_len;
};

/* for configuring RX/TX ring parameters */
struct eavb_avbtool_ringparam {
	uint32_t rx_max_pending;
	uint32_t rx_mini_max_pending;
	uint32_t rx_jumbo_max_pending;
	uint32_t tx_max_pending;
	uint32_t rx_pending;
	uint32_t rx_mini_pending;
	uint32_t rx_jumbo_pending;
	uint32_t tx_pending;
};

/* for configuring number of network channel */
struct eavb_avbtool_channels {
	uint32_t max_rx;
	uint32_t max_tx;
	uint32_t max_other;
	uint32_t max_combined;
	uint32_t rx_count;
	uint32_t tx_count;
	uint32_t other_count;
	uint32_t combined_count;
};

#define EAVB_GSTRING_LEN		32
enum eavb_avbtool_stringset {
	EAVB_SS_STATS = 0,
};

/* for passing string sets for data tagging */
struct eavb_avbtool_gstrings {
	uint32_t string_set;	/* string set id e.c. AVB_SS_STATS, etc */
	uint32_t len;		/* number of strings in the string set */
	uint8_t data[0];
};

struct eavb_avbtool_sset_info {
	uint64_t sset_mask;	/* input: each bit selects an sset to query
				 * output: each bit a returned sset
				 */
	uint32_t data[0];	/* EAVB_SS_xxx count, in order, based on bits
				 * in sset_mask.  One bit implies one
				 * uint32_t, two bits implies two
				 * uint32_t's, etc.
				 */
};

/* for dumping NIC-specific statistics */
struct eavb_avbtool_stats {
	uint32_t n_stats;	/* number of uint64_t's being returned */
	uint64_t data[0];
};

/**
 *for mappage/unmappage
 */
struct eavb_dma_alloc {
	uint32_t dma_paddr;
	void *dma_vaddr;
	unsigned int mmap_size;
};

#define EAVB_MAGIC 'R'

#define EAVB_SETTXPARAM     _IOW(EAVB_MAGIC, 3, struct eavb_txparam)
#define EAVB_GETTXPARAM     _IOR(EAVB_MAGIC, 4, struct eavb_txparam)
#define EAVB_SETRXPARAM     _IOW(EAVB_MAGIC, 5, struct eavb_rxparam)
#define EAVB_GETRXPARAM     _IOR(EAVB_MAGIC, 6, struct eavb_rxparam)
#define EAVB_GETCBSINFO     _IOR(EAVB_MAGIC, 7, struct eavb_cbsinfo)
#define EAVB_SETOPTION      _IOW(EAVB_MAGIC, 8, struct eavb_option)
#define EAVB_GETOPTION      _IOR(EAVB_MAGIC, 9, struct eavb_option)

/* for avbtool */
#define EAVB_AVBTOOL_OFFSET (0x20)
#define EAVB_AVBTOOL_NR(n) (EAVB_AVBTOOL_OFFSET+(n))

#define EAVB_GDRVINFO       _IOR(EAVB_MAGIC,  EAVB_AVBTOOL_NR(0x03), struct eavb_avbtool_drvinfo)
#define EAVB_GRINGPARAM     _IOR(EAVB_MAGIC,  EAVB_AVBTOOL_NR(0x10), struct eavb_avbtool_ringparam)
#define EAVB_GSSET_INFO     _IOWR(EAVB_MAGIC, EAVB_AVBTOOL_NR(0x37), struct eavb_avbtool_sset_info)
#define EAVB_GSTRINGS       _IOWR(EAVB_MAGIC, EAVB_AVBTOOL_NR(0x1b), struct eavb_avbtool_gstrings)
#define EAVB_GSTATS         _IOR(EAVB_MAGIC,  EAVB_AVBTOOL_NR(0x1d), struct eavb_avbtool_stats)
#define EAVB_GCHANNELS      _IOR(EAVB_MAGIC,  EAVB_AVBTOOL_NR(0x3c), struct eavb_avbtool_channels)

/* for debug or test */
#define EAVB_MAPPAGE        _IOR(EAVB_MAGIC, 1, struct eavb_dma_alloc)
#define EAVB_UNMAPPAGE      _IOW(EAVB_MAGIC, 2, struct eavb_dma_alloc)

#endif /* __RAVB_EAVB_H__ */

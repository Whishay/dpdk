/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/queue.h>
#include <sys/stat.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_debug.h>
#include <rte_cycles.h>
#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_string_fns.h>
#include <rte_flow.h>

#include "testpmd.h"
typedef struct {
    uint64_t idx;
    uint64_t rx;
    uint64_t tx;
    uint64_t dp;
    uint64_t rxs;
    uint64_t txs;
    uint64_t cyl;
    uint64_t rx_cyl;
    uint64_t tx_cyl;
    uint64_t rx_desc;
    uint64_t tx_desc;
}fwd_log;

#define FLOG 1000000
static uint64_t idx[2];
#if 1
static uint64_t fidx[2];
fwd_log flog[2][FLOG];

static int clear_log;
//extern uint32_t *rx_batch_desc_log;
static inline void forward_log(uint16_t pid, uint64_t idx, uint16_t rx_desc, uint64_t rx, uint16_t tx_desc, 
                uint64_t tx, uint64_t dp, uint64_t rxs, uint64_t txs, uint64_t rx_cyl, uint64_t tx_cyl, uint64_t cyl)
{
    if (unlikely(clear_log)) {
        clear_log++;
        clear_log = 0;
        memset(flog, 0, sizeof(flog));
        memset(fidx, 0, sizeof(fidx));
    }

    if (unlikely(fidx[pid] >= FLOG))
        fidx[pid] = 0;

    flog[pid][fidx[pid]].idx = idx;
    flog[pid][fidx[pid]].rx = rx;
    flog[pid][fidx[pid]].tx = tx;
    flog[pid][fidx[pid]].dp = dp;
    flog[pid][fidx[pid]].rxs = rxs;
    flog[pid][fidx[pid]].txs = txs;
    flog[pid][fidx[pid]].rx_cyl = rx_cyl;
    flog[pid][fidx[pid]].rx_desc= rx_desc;
    flog[pid][fidx[pid]].tx_cyl = tx_cyl;
    flog[pid][fidx[pid]].tx_desc= tx_desc;
    flog[pid][fidx[pid]].cyl = cyl;
    fidx[pid]++;
}
#endif 

/*
 * Forwarding of packets in I/O mode.
 * Forward packets "as-is".
 * This is the fastest possible forwarding operation, as it does not access
 * to packets data.
 */
static void
pkt_burst_io_forward(struct fwd_stream *fs)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	uint16_t nb_rx_desc, nb_tx_desc;
	uint16_t nb_rx;
	uint16_t nb_tx;
	uint32_t retry;

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	uint64_t start_tsc;
	uint64_t end_tsc;
	uint64_t rx_cycles;
	uint64_t core_cycles;
#endif

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	start_tsc = rte_rdtsc();
#endif

	/*
	 * Receive a burst of packets and forward them.
	 */
	nb_rx = rte_eth_rx_burst(fs->rx_port, fs->rx_queue,
			pkts_burst, nb_pkt_per_burst);
        nb_rx_desc = nb_rx >> 8;
        nb_rx = nb_rx & 0xff;
	if (unlikely(nb_rx == 0)){
                idx[fs->rx_port]++;
		return;
        }
	fs->rx_packets += nb_rx;

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	end_tsc = rte_rdtsc();
	rx_cycles = (end_tsc - start_tsc);
#endif

#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
	fs->rx_burst_stats.pkt_burst_spread[nb_rx]++;
#endif
	nb_tx = rte_eth_tx_burst(fs->tx_port, fs->tx_queue,
			pkts_burst, nb_rx);
        nb_tx_desc = (nb_tx >> 8);
        nb_tx = nb_tx & 0xff;
	/*
	 * Retry if necessary
	 */
	if (unlikely(nb_tx < nb_rx) && fs->retry_enabled) {
		retry = 0;
		while (nb_tx < nb_rx && retry++ < burst_tx_retry_num) {
			rte_delay_us(burst_tx_delay_time);
			nb_tx += rte_eth_tx_burst(fs->tx_port, fs->tx_queue,
					&pkts_burst[nb_tx], nb_rx - nb_tx);
		}
	}
	fs->tx_packets += nb_tx;
#ifdef RTE_TEST_PMD_RECORD_BURST_STATS
	fs->tx_burst_stats.pkt_burst_spread[nb_tx]++;
#endif

#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
	end_tsc = rte_rdtsc();
	core_cycles = (end_tsc - start_tsc);
	fs->core_cycles = (uint64_t) (fs->core_cycles + core_cycles);
        forward_log(fs->rx_port, idx[fs->rx_port]++, nb_rx_desc, nb_rx, nb_tx_desc, nb_tx, fs->fwd_dropped, fs->rx_packets, fs->tx_packets, rx_cycles, core_cycles-rx_cycles, core_cycles);
#endif
	if (unlikely(nb_tx < nb_rx)) {
		fs->fwd_dropped += (nb_rx - nb_tx);
		do {
			rte_pktmbuf_free(pkts_burst[nb_tx]);
		} while (++nb_tx < nb_rx);
	}
//#ifdef RTE_TEST_PMD_RECORD_CORE_CYCLES
#ifdef 0
	end_tsc = rte_rdtsc();
	core_cycles = (end_tsc - start_tsc);
	fs->core_cycles = (uint64_t) (fs->core_cycles + core_cycles);
#endif
}

struct fwd_engine io_fwd_engine = {
	.fwd_mode_name  = "io",
	.port_fwd_begin = NULL,
	.port_fwd_end   = NULL,
	.packet_fwd     = pkt_burst_io_forward,
};

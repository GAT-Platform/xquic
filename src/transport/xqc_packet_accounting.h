#ifndef XQC_PACKET_ACCOUNTING_H
#define XQC_PACKET_ACCOUNTING_H

#include <xquic/xquic.h>
#include "src/transport/xqc_packet.h"

/* Retain metadata independently of packet_out ownership: ACK of a retransmission
 * can retire the original packet_out before the original's ACK arrives. */
#define XQC_PACKET_ACCOUNTING_LIMIT 8192
typedef struct xqc_packet_accounting_entry_s xqc_packet_accounting_entry_t;
typedef struct {
    uint64_t prior_delivered, delivered_us, first_sent_us, sent_us, pn, pns;
    unsigned flags;
} xqc_delivery_anchor_t;
typedef struct {
    xqc_packet_accounting_entry_t *head;
    size_t retained;
    unsigned incomplete;
    xqc_packet_stats_v1_t counters;
    xqc_delivery_observation_v1_t observation;
    xqc_delivery_anchor_t anchor;
    uint64_t first_sent_us, delivered_us, observation_path, ack_before;
    unsigned path_initialized, unsupported_path, ack_active, anchor_valid, batch_flags;
} xqc_packet_accounting_t;

/* Observation-only clock: legacy xqc_monotonic_timestamp defaults to wall time. */
uint64_t xqc_delivery_timestamp(void);
int xqc_delivery_rate(uint64_t bytes, uint64_t interval_us, uint64_t *rate);
void xqc_packet_accounting_sent_at(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn, uint64_t bytes, int retransmission,
    uint64_t now, uint64_t inflight, unsigned flags);
void xqc_packet_accounting_begin_ack(xqc_packet_accounting_t *a, uint64_t path);
void xqc_packet_accounting_end_ack(xqc_packet_accounting_t *a, uint64_t now, uint64_t min_rtt);

void xqc_packet_accounting_sent(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn, uint64_t bytes, int retransmission);
void xqc_packet_accounting_ack(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t low, uint64_t high);
void xqc_packet_accounting_lost(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn);
void xqc_packet_accounting_destroy(xqc_packet_accounting_t *a);

#endif

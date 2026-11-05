#ifndef XQC_PACKET_ACCOUNTING_H
#define XQC_PACKET_ACCOUNTING_H

#include <xquic/xquic.h>
#include "src/transport/xqc_packet.h"

/* Retain metadata independently of packet_out ownership: ACK of a retransmission
 * can retire the original packet_out before the original's ACK arrives. */
#define XQC_PACKET_ACCOUNTING_LIMIT 8192
typedef struct xqc_packet_accounting_entry_s xqc_packet_accounting_entry_t;
typedef struct {
    xqc_packet_accounting_entry_t *head;
    size_t retained;
    unsigned incomplete;
    xqc_packet_stats_v1_t counters;
} xqc_packet_accounting_t;

void xqc_packet_accounting_sent(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn, uint64_t bytes, int retransmission);
void xqc_packet_accounting_ack(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t low, uint64_t high);
void xqc_packet_accounting_lost(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn);
void xqc_packet_accounting_destroy(xqc_packet_accounting_t *a);

#endif

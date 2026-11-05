#include "src/transport/xqc_packet_accounting.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct xqc_packet_accounting_entry_s {
    struct xqc_packet_accounting_entry_s *next;
    uint64_t path, pn, bytes;
    unsigned pns, lost;
};

static void
xqc_packet_accounting_add(xqc_packet_accounting_t *a, uint64_t *value, uint64_t n)
{
    if (UINT64_MAX - *value < n) {
        *value = UINT64_MAX;
        a->incomplete = 1;
    } else {
        *value += n;
    }
}

void
xqc_packet_accounting_sent(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn, uint64_t bytes, int retransmission)
{
    xqc_packet_accounting_entry_t *e;
    if (a->incomplete) {
        return;
    }
    /* This hook represents one successful transmission, not a send attempt. */
    for (e = a->head; e; e = e->next) {
        if (e->path == path && e->pns == pns && e->pn == pn) {
            a->incomplete = 1;
            return;
        }
    }
    if (a->retained == XQC_PACKET_ACCOUNTING_LIMIT) {
        a->incomplete = 1;
        return;
    }
    e = calloc(1, sizeof(*e));
    if (!e) {
        a->incomplete = 1;
        return;
    }
    e->path = path;
    e->pns = pns;
    e->pn = pn;
    e->bytes = bytes;
    e->next = a->head;
    a->head = e;
    ++a->retained;
    xqc_packet_accounting_add(a, &a->counters.sent_packets, 1);
    xqc_packet_accounting_add(a, &a->counters.sent_bytes, bytes);
    if (retransmission) {
        xqc_packet_accounting_add(a, &a->counters.retransmitted_packets, 1);
        xqc_packet_accounting_add(a, &a->counters.retransmitted_bytes, bytes);
    }
}

void
xqc_packet_accounting_ack(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t low, uint64_t high)
{
    xqc_packet_accounting_entry_t **link = &a->head;
    while (*link) {
        xqc_packet_accounting_entry_t *e = *link;
        if (e->path == path && e->pns == pns && e->pn >= low && e->pn <= high) {
            xqc_packet_accounting_add(a, &a->counters.acked_packets, 1);
            xqc_packet_accounting_add(a, &a->counters.acked_bytes, e->bytes);
            *link = e->next;
            free(e);
            --a->retained;
        } else {
            link = &e->next;
        }
    }
}

void
xqc_packet_accounting_lost(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn)
{
    xqc_packet_accounting_entry_t *e;
    for (e = a->head; e; e = e->next) {
        if (e->path == path && e->pns == pns && e->pn == pn && !e->lost) {
            e->lost = 1;
            xqc_packet_accounting_add(a, &a->counters.lost_packets, 1);
            xqc_packet_accounting_add(a, &a->counters.lost_bytes, e->bytes);
            return;
        }
    }
}

void
xqc_packet_accounting_destroy(xqc_packet_accounting_t *a)
{
    while (a->head) {
        xqc_packet_accounting_entry_t *next = a->head->next;
        free(a->head);
        a->head = next;
    }
    memset(a, 0, sizeof(*a));
}

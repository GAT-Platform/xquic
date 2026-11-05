#include "src/transport/xqc_packet_accounting.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

uint64_t
xqc_delivery_timestamp(void)
{
#ifdef _WIN32
    LARGE_INTEGER ticks, frequency;
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency)
        || ticks.QuadPart < 0 || frequency.QuadPart <= 0) return 0;
    uint64_t value = (uint64_t)ticks.QuadPart, hz = (uint64_t)frequency.QuadPart;
    return (value / hz) * 1000000 + (value % hz) * 1000000 / hz;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0) return 0;
    return (uint64_t)value.tv_sec * 1000000 + (uint64_t)value.tv_nsec / 1000;
#endif
}

struct xqc_packet_accounting_entry_s {
    struct xqc_packet_accounting_entry_s *next;
    uint64_t path, pn, bytes;
    unsigned pns, lost;
    xqc_delivery_anchor_t delivery;
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
    xqc_packet_accounting_sent_at(a, path, pns, pn, bytes, retransmission, 0, 0, 0);
}

void
xqc_packet_accounting_sent_at(xqc_packet_accounting_t *a, uint64_t path,
    unsigned pns, uint64_t pn, uint64_t bytes, int retransmission,
    uint64_t now, uint64_t inflight, unsigned flags)
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
    if (!a->path_initialized) {
        a->observation_path = path;
        a->path_initialized = 1;
    } else if (a->observation_path != path) {
        a->unsupported_path = 1;
    }
    if (inflight == 0) {
        a->first_sent_us = now;
        a->delivered_us = now;
    }
    e->delivery.prior_delivered = a->counters.acked_bytes;
    e->delivery.delivered_us = a->delivered_us;
    e->delivery.first_sent_us = a->first_sent_us;
    e->delivery.sent_us = now;
    e->delivery.flags = flags;
    e->delivery.pn = pn;
    e->delivery.pns = pns;
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
            if (a->ack_active) {
                xqc_delivery_anchor_t *c = &e->delivery, *old = &a->anchor;
                a->batch_flags |= c->flags;
                if (!a->anchor_valid || c->prior_delivered > old->prior_delivered
                    || (c->prior_delivered == old->prior_delivered && c->sent_us > old->sent_us)
                    || (c->prior_delivered == old->prior_delivered && c->sent_us == old->sent_us
                        && (c->pns > old->pns || (c->pns == old->pns && c->pn > old->pn))))
                {
                    a->anchor = *c;
                    a->anchor_valid = 1;
                }
            }
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

/* Exact floor(bytes * 8,000,000 / interval) without a wide-integer requirement.
 * The fractional multiply/divide keeps a remainder smaller than interval. */
int
xqc_delivery_rate(uint64_t bytes, uint64_t interval, uint64_t *rate)
{
    if (!interval) return -1;
    uint64_t whole = bytes / interval, rem = bytes % interval;
    if (whole > UINT64_MAX / 8000000) return -1;
    uint64_t fraction = 0, acc = 0;
    for (int bit = 22; bit >= 0; --bit) {
        fraction *= 2;
        if (acc >= interval - acc) { acc -= interval - acc; ++fraction; }
        else { acc *= 2; }
        if (8000000U & (1U << bit)) {
            if (acc >= interval - rem) { acc -= interval - rem; ++fraction; }
            else { acc += rem; }
        }
    }
    whole *= 8000000;
    if (fraction > UINT64_MAX - whole) return -1;
    *rate = whole + fraction;
    return 0;
}

void
xqc_packet_accounting_begin_ack(xqc_packet_accounting_t *a, uint64_t path)
{
    a->ack_before = a->counters.acked_bytes;
    a->ack_active = 1;
    a->anchor_valid = 0;
    a->batch_flags = 0;
    if (a->path_initialized && path != a->observation_path) a->unsupported_path = 1;
}

void
xqc_packet_accounting_end_ack(xqc_packet_accounting_t *a, uint64_t now, uint64_t min_rtt)
{
    a->ack_active = 0;
    if (!a->anchor_valid || a->counters.acked_bytes == a->ack_before) return;
    xqc_delivery_observation_v1_t *o = &a->observation;
    uint64_t seq = o->seq;
    memset(o, 0, sizeof(*o));
    o->seq = seq == UINT64_MAX ? seq : seq + 1;
    o->path_id = a->observation_path;
    o->ack_time_us = now;
    o->anchor_pn = a->anchor.pn;
    o->anchor_pns = a->anchor.pns;
    o->flags = a->batch_flags;
    o->total_acked_bytes = a->counters.acked_bytes;
    o->newly_acked_bytes = a->counters.acked_bytes - a->ack_before;
    if (a->incomplete || seq == UINT64_MAX) { o->status = XQC_DELIVERY_INCOMPLETE; return; }
    if (a->unsupported_path) { o->status = XQC_DELIVERY_UNSUPPORTED_PATH; return; }
    const xqc_delivery_anchor_t *c = &a->anchor;
    if (!c->sent_us || c->sent_us < c->first_sent_us || now < c->sent_us
        || now < c->delivered_us || a->counters.acked_bytes < c->prior_delivered)
    { o->status = XQC_DELIVERY_INVALID_CLOCK; return; }
    o->send_elapsed_us = c->sent_us - c->first_sent_us;
    o->ack_elapsed_us = now - c->delivered_us;
    o->interval_us = o->send_elapsed_us > o->ack_elapsed_us ? o->send_elapsed_us : o->ack_elapsed_us;
    o->delivered_bytes = a->counters.acked_bytes - c->prior_delivered;
    if (c->sent_us > a->first_sent_us) a->first_sent_us = c->sent_us;
    if (now > a->delivered_us) a->delivered_us = now;
    if (!o->interval_us) { o->status = XQC_DELIVERY_INVALID_INTERVAL; return; }
    if (xqc_delivery_rate(o->delivered_bytes, o->interval_us, &o->rate_bps)) {
        o->status = XQC_DELIVERY_OVERFLOW; return;
    }
    if (min_rtt && o->interval_us < min_rtt) o->flags |= XQC_DELIVERY_SHORT_INTERVAL;
    o->status = XQC_DELIVERY_VALID;
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

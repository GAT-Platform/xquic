#include <CUnit/CUnit.h>
#include <stdint.h>
#include "src/transport/xqc_packet_accounting.h"

void xqc_test_delivery_observation(void)
{
    uint64_t rate = 0;
    CU_ASSERT_EQUAL(xqc_delivery_rate(1000, 100000, &rate), 0);
    CU_ASSERT_EQUAL(rate, 80000);
    CU_ASSERT_EQUAL(xqc_delivery_rate(1250, 500000, &rate), 0);
    CU_ASSERT_EQUAL(rate, 20000);
    CU_ASSERT_EQUAL(xqc_delivery_rate(UINT64_C(1099511627776), 1000000, &rate), 0);
    CU_ASSERT_EQUAL(rate, UINT64_C(8796093022208));
    CU_ASSERT_EQUAL(xqc_delivery_rate(UINT64_MAX, 8000000, &rate), 0);
    CU_ASSERT_EQUAL(rate, UINT64_MAX);
    CU_ASSERT_NOT_EQUAL(xqc_delivery_rate(UINT64_MAX, 1, &rate), 0);
    CU_ASSERT_NOT_EQUAL(xqc_delivery_rate(1, 0, &rate), 0);
    CU_ASSERT_EQUAL(xqc_delivery_rate(1, 8000001, &rate), 0);
    CU_ASSERT_EQUAL(rate, 0);
    /* Independent wide-integer oracle exercises rounding and overflow. */
    uint64_t seed = 31;
    for (unsigned i = 0; i < 1000; ++i) {
        seed = seed * UINT64_C(6364136223846793005) + 1;
        uint64_t bytes = seed;
        seed = seed * UINT64_C(6364136223846793005) + 1;
        uint64_t interval = seed | 1;
        __uint128_t expected = (__uint128_t)bytes * 8000000 / interval;
        int rc = xqc_delivery_rate(bytes, interval, &rate);
        CU_ASSERT_EQUAL(rc == 0, expected <= UINT64_MAX);
        if (rc == 0) CU_ASSERT_EQUAL(rate, (uint64_t)expected);
    }

    xqc_packet_accounting_t a = {0};
    xqc_packet_accounting_sent_at(&a, 0, 2, 1, 1000, 0, 1000000, 0, 0);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 1, 1);
    xqc_packet_accounting_end_ack(&a, 1100000, 100000);
    CU_ASSERT_EQUAL(a.observation.status, XQC_DELIVERY_VALID);
    CU_ASSERT_EQUAL(a.observation.rate_bps, 80000);
    CU_ASSERT_EQUAL(a.observation.seq, 1);
    CU_ASSERT_EQUAL(a.observation.newly_acked_bytes, 1000);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 1, 1);
    xqc_packet_accounting_end_ack(&a, 1200000, 100000);
    CU_ASSERT_EQUAL(a.observation.seq, 1);
    CU_ASSERT_EQUAL(a.observation.ack_time_us, 1100000);
    /* New flight does not include the idle time; loss metadata outlives payload. */
    xqc_packet_accounting_sent_at(&a, 0, 2, 2, 1250, 0, 9000000, 0, XQC_DELIVERY_APP_LIMITED);
    xqc_packet_accounting_lost(&a, 0, 2, 2);
    xqc_packet_accounting_sent_at(&a, 0, 2, 3, 1250, 1, 9100000, 0, 0);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 3, 3);
    xqc_packet_accounting_end_ack(&a, 9600000, 100000);
    CU_ASSERT_EQUAL(a.observation.rate_bps, 20000);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 2, 2);
    xqc_packet_accounting_end_ack(&a, 9700000, 100000);
    CU_ASSERT_EQUAL(a.observation.newly_acked_bytes, 1250);
    CU_ASSERT_EQUAL(a.observation.delivered_bytes, 2500);
    CU_ASSERT_EQUAL(a.observation.interval_us, 700000);
    CU_ASSERT_EQUAL(a.observation.rate_bps, 28571);
    CU_ASSERT(a.observation.flags & XQC_DELIVERY_APP_LIMITED);
    CU_ASSERT_EQUAL(a.first_sent_us, 9100000);
    xqc_packet_accounting_destroy(&a);
}

void xqc_test_delivery_batch_and_invalid(void)
{
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        xqc_packet_accounting_t a = {0};
        xqc_packet_accounting_sent_at(&a, 0, 2, 1, 1000, 0, 1000000, 0, 0);
        xqc_packet_accounting_sent_at(&a, 0, 2, 2, 2000, 0, 1100000, 1000, 0);
        xqc_packet_accounting_sent_at(&a, 0, 2, 3, 3000, 0, 1200000, 3000, 0);
        xqc_packet_accounting_begin_ack(&a, 0);
        for (unsigned i = 0; i < 3; ++i) {
            unsigned pn = reverse ? 3 - i : i + 1;
            xqc_packet_accounting_ack(&a, 0, 2, pn, pn);
        }
        xqc_packet_accounting_end_ack(&a, 1250000, 100000);
        CU_ASSERT_EQUAL(a.observation.anchor_pn, 3);
        CU_ASSERT_EQUAL(a.observation.delivered_bytes, 6000);
        CU_ASSERT_EQUAL(a.observation.send_elapsed_us, 200000);
        CU_ASSERT_EQUAL(a.observation.ack_elapsed_us, 250000);
        CU_ASSERT_EQUAL(a.observation.rate_bps, 192000);
        /* Crafted ACK compression: measured send span dominates ACK span. */
        a.first_sent_us = 1000000; a.delivered_us = 1390000;
        xqc_packet_accounting_sent_at(&a, 0, 2, 4, 12000, 0, 1400000, 1, 0);
        xqc_packet_accounting_begin_ack(&a, 0);
        xqc_packet_accounting_ack(&a, 0, 2, 4, 4);
        xqc_packet_accounting_end_ack(&a, 1400000, 10000);
        CU_ASSERT_EQUAL(a.observation.rate_bps, 240000);
        CU_ASSERT_EQUAL(a.observation.ack_elapsed_us, 10000);
        CU_ASSERT_EQUAL(a.observation.send_elapsed_us, 400000);
        xqc_packet_accounting_destroy(&a);
    }
    xqc_packet_accounting_t a = {0};
    xqc_packet_accounting_sent_at(&a, 0, 2, 1, 1000, 0, 100, 0, 0);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 1, 1);
    xqc_packet_accounting_end_ack(&a, 100, 10);
    CU_ASSERT_EQUAL(a.observation.status, XQC_DELIVERY_INVALID_INTERVAL);
    xqc_packet_accounting_sent_at(&a, 0, 2, 2, 1000, 0, 200, 0, 0);
    xqc_packet_accounting_begin_ack(&a, 0);
    xqc_packet_accounting_ack(&a, 0, 2, 2, 2);
    xqc_packet_accounting_end_ack(&a, 199, 10);
    CU_ASSERT_EQUAL(a.observation.status, XQC_DELIVERY_INVALID_CLOCK);
    xqc_packet_accounting_sent_at(&a, 1, 2, 3, 1000, 0, 300, 0, 0);
    xqc_packet_accounting_begin_ack(&a, 1);
    xqc_packet_accounting_ack(&a, 1, 2, 3, 3);
    xqc_packet_accounting_end_ack(&a, 400, 10);
    CU_ASSERT_EQUAL(a.observation.status, XQC_DELIVERY_UNSUPPORTED_PATH);
    xqc_packet_accounting_destroy(&a);
}

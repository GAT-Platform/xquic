#include <CUnit/CUnit.h>
#include <stdint.h>
#include "src/transport/xqc_packet_accounting.h"

void xqc_test_packet_accounting(void)
{
    xqc_packet_accounting_t a = {0};
    /* Oracle: 100 different transmissions, 60 ACKed, 10 first declared lost,
     * 30 still outstanding. Loss is independent from ACK, never sent-minus-loss. */
    for (unsigned i = 0; i < 100; ++i) {
        xqc_packet_accounting_sent(&a, 0, 2, i, 100 + i, 0);
    }
    xqc_packet_accounting_ack(&a, 0, 2, 0, 59);
    for (unsigned i = 60; i < 70; ++i) {
        xqc_packet_accounting_lost(&a, 0, 2, i);
        xqc_packet_accounting_lost(&a, 0, 2, i);
    }
    CU_ASSERT_EQUAL(a.counters.sent_packets, 100);
    CU_ASSERT_EQUAL(a.counters.sent_bytes, 14950);
    CU_ASSERT_EQUAL(a.counters.acked_packets, 60);
    CU_ASSERT_EQUAL(a.counters.acked_bytes, 7770);
    CU_ASSERT_EQUAL(a.counters.lost_packets, 10);
    CU_ASSERT_EQUAL(a.counters.lost_bytes, 1645);
    xqc_packet_accounting_ack(&a, 0, 2, 10, 50);
    xqc_packet_accounting_ack(&a, 0, 2, 50, 59);
    CU_ASSERT_EQUAL(a.counters.acked_packets, 60);
    CU_ASSERT_EQUAL(a.counters.acked_bytes, 7770);
    /* Retired payloads do not retire ledger metadata. A new packet carrying
     * the same data is independently ACKed, then the original arrives late. */
    xqc_packet_accounting_sent(&a, 0, 2, 100, 160, 1);
    xqc_packet_accounting_ack(&a, 0, 2, 100, 100);
    CU_ASSERT_EQUAL(a.counters.acked_packets, 61);
    xqc_packet_accounting_ack(&a, 0, 2, 60, 60);
    CU_ASSERT_EQUAL(a.counters.acked_packets, 62);
    CU_ASSERT_EQUAL(a.counters.acked_bytes, 8090);
    CU_ASSERT_EQUAL(a.counters.lost_packets, 10);
    CU_ASSERT_EQUAL(a.counters.retransmitted_packets, 1);
    CU_ASSERT_EQUAL(a.counters.retransmitted_bytes, 160);
    xqc_packet_accounting_destroy(&a);
    CU_ASSERT_EQUAL(a.retained, 0);
    CU_ASSERT_EQUAL(a.counters.sent_packets, 0);

    /* Same packet number in another number space/path is not a duplicate. */
    xqc_packet_accounting_sent(&a, 0, 0, 1, 1200, 0);
    xqc_packet_accounting_sent(&a, 0, 1, 1, 200, 0);
    xqc_packet_accounting_sent(&a, 1, 1, 1, 300, 0);
    xqc_packet_accounting_ack(&a, 0, 1, 0, 100);
    CU_ASSERT_EQUAL(a.counters.acked_bytes, 200);
    xqc_packet_accounting_ack(&a, 1, 1, 1, 1);
    CU_ASSERT_EQUAL(a.counters.acked_bytes, 500);
    xqc_packet_accounting_lost(&a, 0, 0, 999); /* unsent number */
    xqc_packet_accounting_ack(&a, 0, 2, 0, UINT64_MAX);
    CU_ASSERT_EQUAL(a.counters.lost_packets, 0);
    CU_ASSERT_EQUAL(a.counters.acked_packets, 2);
    xqc_packet_accounting_destroy(&a);

    /* Saturation never wraps a 64-bit counter into a plausible smaller value. */
    a.counters.sent_bytes = UINT64_MAX - 1;
    xqc_packet_accounting_sent(&a, 0, 2, 1, 2, 0);
    CU_ASSERT_EQUAL(a.counters.sent_bytes, UINT64_MAX);
    CU_ASSERT_TRUE(a.incomplete);
    xqc_packet_accounting_sent(&a, 0, 2, 2, 20, 0);
    CU_ASSERT_EQUAL(a.counters.sent_packets, 1);
    xqc_packet_accounting_destroy(&a);
    xqc_packet_accounting_sent(&a, 0, 2, 1, 100, 0);
    xqc_packet_accounting_sent(&a, 0, 2, 1, 100, 0);
    CU_ASSERT_TRUE(a.incomplete);
    CU_ASSERT_EQUAL(a.counters.sent_packets, 1);
    xqc_packet_accounting_destroy(&a);

    /* Bounded unresolved history, including losses whose ACK never arrives. */
    for (unsigned i = 0; i <= XQC_PACKET_ACCOUNTING_LIMIT; ++i) {
        xqc_packet_accounting_sent(&a, 0, 2, i, 1, 0);
    }
    CU_ASSERT_EQUAL(a.retained, XQC_PACKET_ACCOUNTING_LIMIT);
    CU_ASSERT_TRUE(a.incomplete);
    xqc_packet_accounting_ack(&a, 0, 2, 0, UINT64_MAX);
    CU_ASSERT_EQUAL(a.retained, 0);
    CU_ASSERT_TRUE(a.incomplete); /* missing send history cannot heal itself */
    xqc_packet_accounting_destroy(&a);
}

void xqc_test_packet_accounting_reordered_oracle(void)
{
    xqc_packet_accounting_t a = {0};
    unsigned char acked[200] = {0}, lost[200] = {0};
    uint64_t expected_ack = 0, expected_bytes = 0, expected_loss = 0, loss_bytes = 0;
    for (unsigned i = 0; i < 200; ++i) {
        xqc_packet_accounting_sent(&a, i % 2, i % 3, i, 50 + i * 7, 0);
    }
    /* Independent array oracle with deterministic reordering and duplicates. */
    uint32_t seed = 31;
    for (unsigned i = 0; i < 2000; ++i) {
        seed = seed * 1664525U + 1013904223U;
        unsigned n = seed % 200;
        if (i % 4 == 0) {
            xqc_packet_accounting_lost(&a, n % 2, n % 3, n);
            if (!acked[n] && !lost[n]) {
                lost[n] = 1; ++expected_loss; loss_bytes += 50 + n * 7;
            }
        } else {
            xqc_packet_accounting_ack(&a, n % 2, n % 3, n, n);
            if (!acked[n]) {
                acked[n] = 1; ++expected_ack; expected_bytes += 50 + n * 7;
            }
        }
        CU_ASSERT_EQUAL(a.counters.acked_packets, expected_ack);
        CU_ASSERT_EQUAL(a.counters.acked_bytes, expected_bytes);
        CU_ASSERT_EQUAL(a.counters.lost_packets, expected_loss);
        CU_ASSERT_EQUAL(a.counters.lost_bytes, loss_bytes);
    }
    CU_ASSERT_FALSE(a.incomplete);
    xqc_packet_accounting_destroy(&a);
}

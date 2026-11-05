/**
 * @copyright Copyright (c) 2022, Alibaba Group Holding Limited
 */

#include <CUnit/CUnit.h>
#include "xquic/xquic.h"
#include "src/transport/xqc_conn.h"
#include "src/transport/xqc_client.h"
#include "xquic/xquic_typedef.h"
#include "src/common/xqc_str.h"
#include "src/congestion_control/xqc_new_reno.h"
#include "xqc_common_test.h"
#include "src/transport/xqc_engine.h"
#include "src/transport/xqc_send_ctl.h"
#include "src/transport/xqc_packet_out.h"
#include "src/transport/xqc_frame.h"

void
xqc_test_conn_create()
{
    xqc_engine_t *engine = test_create_engine();
    CU_ASSERT(engine != NULL);

    const xqc_cid_t *cid = test_cid_connect(engine);
    CU_ASSERT_NOT_EQUAL(cid, NULL);

    xqc_packet_stats_v1_t stats;
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(NULL, cid, &stats), -XQC_EPARAM);
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, NULL, &stats), -XQC_EPARAM);
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, cid, NULL), -XQC_EPARAM);
    xqc_cid_t unknown = {0};
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, &unknown, &stats), -XQC_ECONN_NFOUND);
    CU_ASSERT_FALSE(stats.complete);
    xqc_delivery_observation_v1_t observation;
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(NULL, cid, &observation), -XQC_EPARAM);
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, NULL, &observation), -XQC_EPARAM);
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, cid, NULL), -XQC_EPARAM);
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, &unknown, &observation), -XQC_ECONN_NFOUND);
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, cid, &observation), XQC_OK);
    CU_ASSERT_EQUAL(observation.status, XQC_DELIVERY_NO_SAMPLE);
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, cid, &stats), XQC_OK);
    CU_ASSERT_TRUE(stats.complete);
    CU_ASSERT_EQUAL(stats.acked_bytes, 0);
    xqc_connection_t *conn = xqc_engine_conns_hash_find(engine, cid, 's');
    xqc_packet_accounting_sent(&conn->packet_accounting, 0, 2, 123, 700, 0);
    xqc_packet_accounting_ack(&conn->packet_accounting, 0, 2, 123, 123);
    conn->conn_initial_path->path_send_ctl->ctl_first_rtt_sample_time = 1;
    conn->conn_initial_path->path_send_ctl->ctl_latest_rtt = 12345;
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, cid, &stats), XQC_OK);
    CU_ASSERT_EQUAL(stats.acked_bytes, 700);
    CU_ASSERT_EQUAL(stats.latest_rtt, 12345);
    /* Exercise the production hooks with distinct plaintext/encrypted lengths.
     * No packet_out is left in the unacked queue: ACK metadata must survive it. */
    xqc_packet_out_t packet = {0};
    packet.po_pkt.pkt_pns = XQC_PNS_APP_DATA;
    packet.po_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER;
    packet.po_pkt.pkt_num = 12345;
    packet.po_frame_types = XQC_FRAME_BIT_PING;
    packet.po_used_size = 100;
    packet.po_enc_size = 116;
    packet.po_sent_time = 1000000;
    packet.po_flag = XQC_POF_IN_FLIGHT | XQC_POF_PMTUD_PROBING;
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    ctl->ctl_bytes_in_flight = 0;
    conn->conn_settings.disable_pn_skipping = 1;
    xqc_send_ctl_on_packet_sent(ctl, conn->conn_initial_path->path_pn_ctl, &packet, 1000000);
    xqc_ack_info_t ack = {0};
    ack.pns = XQC_PNS_APP_DATA;
    ack.n_ranges = 1;
    ack.ranges[0].low = ack.ranges[0].high = 12345;
    CU_ASSERT_EQUAL(xqc_send_ctl_on_ack_received(ctl, conn->conn_initial_path->path_pn_ctl,
        conn->conn_send_queue, &ack, 1100000, 1), XQC_OK);
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, cid, &observation), XQC_OK);
    CU_ASSERT_EQUAL(observation.status, XQC_DELIVERY_VALID);
    CU_ASSERT_EQUAL(observation.delivered_bytes, 116);
    CU_ASSERT_TRUE(observation.interval_us > 0);
    if (observation.interval_us > 0)
        CU_ASSERT_EQUAL(observation.rate_bps, 116ULL * 8000000 / observation.interval_us);
    CU_ASSERT_TRUE(observation.exported_at_us >= observation.ack_time_us);
    CU_ASSERT_TRUE(observation.exported_at_us - observation.ack_time_us < 1000000);
    CU_ASSERT_EQUAL(observation.total_acked_bytes, 816);
    conn->packet_accounting.incomplete = 1;
    CU_ASSERT_EQUAL(xqc_conn_get_delivery_observation_v1(engine, cid, &observation), XQC_OK);
    CU_ASSERT_EQUAL(observation.status, XQC_DELIVERY_INCOMPLETE);
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, cid, &stats), XQC_OK);
    CU_ASSERT_FALSE(stats.complete);

    xqc_engine_destroy(engine);
}


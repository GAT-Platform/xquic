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
xqc_test_idle_timer_restart()
{
    xqc_engine_t *engine = test_create_engine();
    const xqc_cid_t *cid = test_cid_connect(engine);
    xqc_connection_t *conn = xqc_engine_conns_hash_find(engine, cid, 's');
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    conn->local_settings.max_idle_timeout = 10000;
    conn->conn_settings.disable_pn_skipping = 1;
    conn->idle_timer_restarted_after_recv = 0; /* fixture: a packet was just received */
    xqc_timer_set(&conn->conn_timer_manager, XQC_TIMER_CONN_IDLE, 1000000, 10000000);
    xqc_packet_out_t packet = {0};
    packet.po_pkt.pkt_pns = XQC_PNS_APP_DATA;
    packet.po_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER;
    packet.po_pkt.pkt_num = 12345;
    packet.po_frame_types = XQC_FRAME_BIT_ACK;
    packet.po_used_size = 14;
    packet.po_enc_size = 30;
    packet.po_flag = XQC_POF_IN_FLIGHT | XQC_POF_PMTUD_PROBING;
    xqc_send_ctl_on_packet_sent(ctl, conn->conn_initial_path->path_pn_ctl, &packet, 2000000);
    CU_ASSERT_EQUAL(conn->conn_timer_manager.timer[XQC_TIMER_CONN_IDLE].expire_time, 11000000);
    packet.po_frame_types = XQC_FRAME_BIT_PING;
    packet.po_sent_time = 3000000;
    xqc_send_ctl_on_packet_sent(ctl, conn->conn_initial_path->path_pn_ctl, &packet, 3000000);
    CU_ASSERT_EQUAL(conn->conn_timer_manager.timer[XQC_TIMER_CONN_IDLE].expire_time, 13000000);
    /* Repeated unanswered PINGs must not keep a dead peer alive forever. */
    ++packet.po_pkt.pkt_num;
    packet.po_sent_time = 9000000;
    xqc_send_ctl_on_packet_sent(ctl, conn->conn_initial_path->path_pn_ctl, &packet, 9000000);
    CU_ASSERT_EQUAL(conn->conn_timer_manager.timer[XQC_TIMER_CONN_IDLE].expire_time, 13000000);
    xqc_engine_destroy(engine);
}

void
xqc_test_server_feedback_ping()
{
    xqc_engine_t *engine = test_create_engine();
    const xqc_cid_t *cid = test_cid_connect(engine);
    xqc_connection_t *conn = xqc_engine_conns_hash_find(engine, cid, 's');
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    conn->conn_type = XQC_CONN_TYPE_SERVER;
    conn->conn_flag |= XQC_CONN_FLAG_HANDSHAKE_COMPLETED | XQC_CONN_FLAG_HANDSHAKE_CONFIRMED;
    conn->conn_settings.ping_on = 1;
    conn->last_app_data_send_time = 1000000;
    xqc_timer_t *timer = &conn->conn_timer_manager.timer[XQC_TIMER_PING];
    xqc_timer_unset(&conn->conn_timer_manager, XQC_TIMER_PING);
    /* Healthy/empty server has no periodic feedback-probe traffic. */
    timer->timeout_cb(XQC_TIMER_PING, 2000000, conn);
    CU_ASSERT_FALSE(conn->conn_flag & XQC_CONN_FLAG_PING);
    CU_ASSERT_TRUE(timer->timer_is_set);
    ctl->ctl_pto_count = 3;
    ctl->ctl_bytes_ack_eliciting_inflight[XQC_PNS_APP_DATA] = 100;
    timer->timeout_cb(XQC_TIMER_PING, 3000000, conn);
    CU_ASSERT_TRUE(conn->conn_flag & XQC_CONN_FLAG_PING);
    conn->conn_flag &= ~XQC_CONN_FLAG_PING;
    ctl->ctl_bytes_ack_eliciting_inflight[XQC_PNS_APP_DATA] = 0;
    timer->timeout_cb(XQC_TIMER_PING, 4000000, conn);
    CU_ASSERT_FALSE(conn->conn_flag & XQC_CONN_FLAG_PING);
    ctl->ctl_bytes_ack_eliciting_inflight[XQC_PNS_APP_DATA] = 100;
    conn->last_app_data_send_time = 4500000;
    timer->timeout_cb(XQC_TIMER_PING, 5000000, conn);
    CU_ASSERT_FALSE(conn->conn_flag & XQC_CONN_FLAG_PING);
    /* Preserve existing client idle keepalive. */
    conn->conn_type = XQC_CONN_TYPE_CLIENT;
    timer->timeout_cb(XQC_TIMER_PING, 6000000, conn);
    CU_ASSERT_TRUE(conn->conn_flag & XQC_CONN_FLAG_PING);
    conn->conn_flag &= ~XQC_CONN_FLAG_PING;
    conn->conn_settings.ping_on = 0;
    xqc_timer_unset(&conn->conn_timer_manager, XQC_TIMER_PING);
    timer->timeout_cb(XQC_TIMER_PING, 8000000, conn);
    CU_ASSERT_FALSE(conn->conn_flag & XQC_CONN_FLAG_PING);
    CU_ASSERT_FALSE(timer->timer_is_set);
    xqc_engine_destroy(engine);
}

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


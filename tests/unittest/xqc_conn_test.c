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
    conn->packet_accounting.incomplete = 1;
    CU_ASSERT_EQUAL(xqc_conn_get_packet_stats_v1(engine, cid, &stats), XQC_OK);
    CU_ASSERT_FALSE(stats.complete);

    xqc_engine_destroy(engine);
}


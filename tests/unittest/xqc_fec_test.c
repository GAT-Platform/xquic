/**
 * @copyright Copyright (c) 2022, Alibaba Group Holding Limited
 */

#include <CUnit/CUnit.h>
#include "xqc_fec_test.h"
#include "xqc_fec_scheme_test.h"
#include "include/xquic/xquic.h"
#include "src/transport/xqc_fec.h"
#include "src/transport/xqc_conn.h"
#include "src/transport/xqc_packet_out.h"
#include "src/transport/xqc_packet_in.h"
#include "src/transport/xqc_send_ctl.h"
#include "src/transport/xqc_send_queue.h"
#include "src/transport/xqc_frame_parser.h"
#include "xqc_common_test.h"

xqc_fec_schemes_e fec_schemes[XQC_FEC_MAX_SCHEME_NUM] = {0, XQC_XOR_CODE, XQC_REED_SOLOMON_CODE, XQC_PACKET_MASK_CODE};

void
xqc_test_fec_scheme_setter()
{
    xqc_int_t ret, encoder_scheme_len;
    xqc_fec_schemes_e encoder_schemes[XQC_FEC_MAX_SCHEME_NUM];

    ret = xqc_set_fec_schemes(fec_schemes, 4, encoder_schemes, &encoder_scheme_len);
    CU_ASSERT(ret == XQC_OK && encoder_scheme_len == 3);
}

void
xqc_test_fec_negotiation()
{
    xqc_int_t ret;
    xqc_connection_t *conn = test_engine_connect_fec();
    xqc_connection_t *conn_server = test_engine_connect_fec_server();
    xqc_transport_params_t params = {0};
    xqc_trans_settings_t *ls = &conn->local_settings;

    params.fec_version = XQC_ERR_FEC_VERSION;
    ret = xqc_negotiate_fec_schemes(conn, params);
    CU_ASSERT(ret == -XQC_EFEC_NOT_SUPPORT_FEC);

    params.fec_version = XQC_FEC_02;
    params.enable_encode_fec = 1;
    params.enable_decode_fec = 1;
    ls->enable_encode_fec = 1;
    ls->enable_decode_fec = 1;

    params.fec_encoder_schemes_num = 2;
    params.fec_decoder_schemes_num = 2;
    ret = xqc_negotiate_fec_schemes(conn, params);
    CU_ASSERT(ret == -XQC_EFEC_NOT_SUPPORT_FEC);

    xqc_engine_destroy(conn->engine);
    ls = &conn_server->local_settings;

    params.fec_encoder_schemes[0] = 20;
    params.fec_decoder_schemes[0] = 20;
    params.fec_encoder_schemes_num = 1;
    params.fec_decoder_schemes_num = 1;

    ls->fec_encoder_schemes[0] = 8;
    ls->fec_decoder_schemes[0] = 8;
    ls->fec_encoder_schemes_num = 1;
    ls->fec_decoder_schemes_num = 1;
    ret = xqc_negotiate_fec_schemes(conn_server, params);
    CU_ASSERT(ret == -XQC_EFEC_NOT_SUPPORT_FEC);

    xqc_engine_destroy(conn_server->engine);
}


void
xqc_test_write_repair_packet()
{
    uint8_t bm_mode;
    xqc_int_t ret;
    xqc_list_head_t *prev = NULL;
    xqc_connection_t *conn = test_engine_connect_fec();

    bm_mode = 0;

    ret = xqc_write_repair_packets(conn, 0, prev, 0, bm_mode);
    CU_ASSERT(ret == XQC_OK);

    conn->conn_settings.fec_params.fec_max_symbol_num_per_block = 4;
    conn->conn_settings.fec_params.fec_code_rate = 1;
    conn->fec_ctl->fec_send_symbol_num[0] = 3;
    ret = xqc_write_repair_packets(conn, 0, prev, 1, bm_mode);
    CU_ASSERT(ret == -XQC_EFEC_SCHEME_ERROR);

    /** test repair packets number larger than maximun repair number */
    conn->fec_ctl->fec_send_required_repair_num[bm_mode] = XQC_REPAIR_LEN + 1;
    ret = xqc_send_repair_packets(conn, XQC_PACKET_MASK_CODE, prev, bm_mode);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    /** test repair packets number larger than source symbol number */
    conn->fec_ctl->fec_send_symbol_num[bm_mode] = 1;
    conn->fec_ctl->fec_send_required_repair_num[bm_mode] = conn->fec_ctl->fec_send_symbol_num[bm_mode] + 1;
    ret = xqc_send_repair_packets(conn, XQC_PACKET_MASK_CODE, prev, bm_mode);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    /** test invalid scheme */
    conn->fec_ctl->fec_send_symbol_num[bm_mode] = 4;
    conn->fec_ctl->fec_send_required_repair_num[bm_mode] = 1;
    ret = xqc_send_repair_packets(conn, 0, prev, bm_mode);
    CU_ASSERT(ret == -XQC_EFEC_SCHEME_ERROR);


    xqc_engine_destroy(conn->engine);
}

void
xqc_test_gen_fec_frames()
{
    xqc_int_t ret, padding_len, limit;
    xqc_connection_t *conn = test_engine_connect_fec();
    xqc_packet_out_t* packet_out = xqc_packet_out_create(XQC_QUIC_MAX_MSS);
    packet_out->po_used_size = 1000;
    padding_len = 500;
    limit = 1200;
    ret = xqc_gen_padding_frame_with_len(conn, packet_out, padding_len, limit);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    // invalid po_used_size
    packet_out->po_used_size = XQC_QUIC_MAX_MSS + 1;
    ret = xqc_gen_sid_frame(conn, packet_out);
    CU_ASSERT(ret == -XQC_EPARAM);

    // invalid fec_send_block_num
    conn->fec_ctl->fec_send_block_num[0] = XQC_FEC_MAX_BLOCK_NUM + 1;
    packet_out->po_reserved_size = 12;
    packet_out->po_used_size = 0;
    ret = xqc_gen_sid_frame(conn, packet_out);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    ret = xqc_gen_repair_frame(conn, NULL, 0, 0, 0);
    CU_ASSERT(ret == -XQC_EPARAM);

    conn->conn_settings.fec_params.fec_ele_bit_size = 0;
    ret = xqc_gen_repair_frame(conn, packet_out, 0, 0, 0);
    CU_ASSERT(ret == -XQC_EPARAM);

    xqc_packet_out_destroy(packet_out);
    xqc_engine_destroy(conn->engine);
}

void
xqc_test_chk_fec_param()
{
    xqc_int_t ret;
    xqc_connection_t *conn = test_engine_connect_fec();

    /** test invalid repair numbers */
    ret = xqc_check_fec_params(conn, XQC_FEC_MAX_SYMBOL_NUM_PBLOCK, XQC_REPAIR_LEN + 1, XQC_SYMBOL_CACHE_LEN, XQC_MAX_SYMBOL_SIZE);
    CU_ASSERT(ret == -XQC_EFEC_SCHEME_ERROR);

    /** test too low repair number */
    ret = xqc_check_fec_params(conn, XQC_FEC_MAX_SYMBOL_NUM_PBLOCK, 0, XQC_SYMBOL_CACHE_LEN, XQC_MAX_SYMBOL_SIZE);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    /** test invalid src symbol number */
    ret = xqc_check_fec_params(conn, XQC_FEC_MAX_SYMBOL_NUM_PBLOCK + 1, XQC_REPAIR_LEN, XQC_SYMBOL_CACHE_LEN, XQC_MAX_SYMBOL_SIZE);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    /** test invalid symbol window */
    ret = xqc_check_fec_params(conn, XQC_FEC_MAX_SYMBOL_NUM_PBLOCK, XQC_REPAIR_LEN, XQC_SYMBOL_CACHE_LEN + 1, XQC_MAX_SYMBOL_SIZE);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    /** test invalid symbol size */
    ret = xqc_check_fec_params(conn, XQC_FEC_MAX_SYMBOL_NUM_PBLOCK, XQC_REPAIR_LEN, XQC_SYMBOL_CACHE_LEN, XQC_MAX_SYMBOL_SIZE + 1);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    xqc_engine_destroy(conn->engine);
}

void
xqc_test_encoder_chk_param()
{
    xqc_int_t ret, rpr_syb_num;
    xqc_connection_t *conn = test_engine_connect_fec();

    ret = xqc_fec_encoder_check_params(conn, 2, XQC_XOR_CODE, XQC_MAX_SYMBOL_SIZE);
    CU_ASSERT(ret == -XQC_EPARAM);
    
    ret = xqc_fec_encoder_check_params(conn, 1, XQC_XOR_CODE, XQC_MAX_SYMBOL_SIZE + 1);
    CU_ASSERT(ret == -XQC_EPARAM);

    xqc_engine_destroy(conn->engine);
}

void
xqc_test_process_src_syb()
{
    uint8_t bm_mode;
    xqc_int_t ret;
    xqc_connection_t *conn = test_engine_connect_fec();
    xqc_packet_out_t* packet_out = xqc_packet_out_create(XQC_QUIC_MAX_MSS);
    
    /** test invalid fec_params */
    bm_mode = 0;
    packet_out->po_stream_fec_blk_mode = bm_mode;
    conn->fec_ctl->fec_send_required_repair_num[bm_mode] = 0;
    ret = xqc_process_fec_protected_packet(conn, packet_out);
    CU_ASSERT(ret == -XQC_EPARAM);

    /** test invalid process of write sid frame */
    conn->fec_ctl->fec_send_block_mode_size[bm_mode] = 4;
    conn->fec_ctl->fec_send_required_repair_num[bm_mode] = 1;
    conn->conn_settings.fec_params.fec_max_window_size = XQC_SYMBOL_CACHE_LEN;
    packet_out->po_used_size = 1100;
    packet_out->po_payload = packet_out->po_buf + 16;
    packet_out->po_flag |= XQC_POF_STREAM_NO_LEN;
    ret = xqc_process_fec_protected_packet(conn, packet_out);
    CU_ASSERT(ret == XQC_OK);

    /**  */
    packet_out->po_flag &= ~XQC_POF_STREAM_NO_LEN;
    conn->fec_ctl->fec_send_block_num[bm_mode] = XQC_FEC_MAX_BLOCK_NUM + 1;
    packet_out->po_reserved_size = 12;
    ret = xqc_process_fec_protected_packet(conn, packet_out);
    CU_ASSERT(ret == -XQC_EFEC_SYMBOL_ERROR);

    xqc_packet_out_destroy(packet_out);
    xqc_engine_destroy(conn->engine);
}

/* Internal processing hook, intentionally not part of the public API. */
extern xqc_int_t xqc_conn_on_pkt_processed(xqc_connection_t *, xqc_packet_in_t *, xqc_usec_t);

static void
xqc_test_packet_observer_callback(void *context, uint32_t event, uint64_t path,
    uint64_t pn, uint64_t at, uint64_t bytes, uint32_t flags)
{
    uint64_t *values = context;
    values[0]++; values[1] = event; values[2] = path; values[3] = pn;
    values[4] = at; values[5] = bytes; values[6] = flags;
}

static void
xqc_test_packet_observer()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    CU_ASSERT_PTR_NOT_NULL_FATAL(conn);
    uint64_t values[7] = {0};
    xqc_conn_set_packet_observer(NULL, NULL, NULL);
    xqc_conn_set_packet_observer(conn, xqc_test_packet_observer_callback, values);
    xqc_packet_in_t pi = {0};
    pi.pi_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER;
    pi.pi_pkt.pkt_pns = XQC_PNS_APP_DATA;
    pi.pi_pkt.pkt_num = 17;
    pi.pi_frame_types = XQC_FRAME_BIT_STREAM;
    pi.buf_size = 1200;
    conn->conn_flag |= XQC_CONN_FLAG_ADDR_VALIDATED;
    CU_ASSERT(xqc_conn_on_pkt_processed(conn, &pi, 1000000) == XQC_OK);
    CU_ASSERT(values[0] == 1 && values[1] == XQC_OBSERVE_RX && values[3] == 17);
    CU_ASSERT(values[4] == 1000000 && values[5] == 1200 && values[6] == XQC_OBSERVE_DATA);
    // Reconstructed FEC data is not a physical network arrival.
    pi.pi_flag |= XQC_PIF_FEC_RECOVERED;
    pi.pi_pkt.pkt_num++;
    CU_ASSERT(xqc_conn_on_pkt_processed(conn, &pi, 1000100) == XQC_OK);
    CU_ASSERT(values[0] == 1);
    pi.pi_flag = 0; pi.pi_frame_types = XQC_FRAME_BIT_REPAIR_SYMBOL;
    pi.pi_pkt.pkt_num++;
    CU_ASSERT(xqc_conn_on_pkt_processed(conn, &pi, 1000200) == XQC_OK);
    CU_ASSERT(values[0] == 2 && values[6] == XQC_OBSERVE_REPAIR);
    /* 0-RTT shares the APP_DATA PN space but is not a 1-RTT observation. */
    xqc_packet_out_t *po = xqc_packet_out_create(XQC_QUIC_MAX_MSS);
    CU_ASSERT_PTR_NOT_NULL_FATAL(po);
    po->po_pkt.pkt_type = XQC_PTYPE_0RTT;
    po->po_pkt.pkt_pns = XQC_PNS_APP_DATA;
    po->po_pkt.pkt_num = 21;
    po->po_frame_types = XQC_FRAME_BIT_PADDING;
    po->po_enc_size = 100;
    xqc_path_ctx_t *path = conn->conn_initial_path;
    xqc_send_ctl_on_packet_sent(path->path_send_ctl, xqc_get_pn_ctl(conn, path), po, 1000300);
    CU_ASSERT(values[0] == 2);
    po->po_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER;
    xqc_send_ctl_on_packet_sent(path->path_send_ctl, xqc_get_pn_ctl(conn, path), po, 1000400);
    CU_ASSERT(values[0] == 3 && values[1] == XQC_OBSERVE_TX && values[3] == 21 && values[5] == 100);
    xqc_packet_out_destroy(po);
    xqc_conn_set_packet_observer(conn, NULL, NULL);
    pi.pi_pkt.pkt_num++;
    CU_ASSERT(xqc_conn_on_pkt_processed(conn, &pi, 1000300) == XQC_OK);
    CU_ASSERT(values[0] == 3);
    xqc_engine_destroy(conn->engine);
}

static void
xqc_test_fec_runtime_pause()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    CU_ASSERT_PTR_NOT_NULL_FATAL(conn);
    CU_ASSERT_PTR_NOT_NULL_FATAL(conn->fec_ctl);
    xqc_fec_runtime_stats_t stats = {0};
    CU_ASSERT(xqc_conn_pause_fec_encoding(NULL, 1) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 2) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(NULL, &stats) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, NULL) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, &stats) == -XQC_EPARAM);
    stats.struct_size = sizeof(stats);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, &stats) == XQC_OK);
    CU_ASSERT(!stats.negotiated && !stats.encoding_enabled);
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 0) == -XQC_ESTATE);
    // Initial OFF does not destroy FEC negotiation or decoder state.
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 1) == XQC_OK);
    CU_ASSERT(conn->conn_settings.enable_encode_fec == 1);
    CU_ASSERT(conn->conn_settings.enable_decode_fec == 1);
    conn->conn_flag |= XQC_CONN_FLAG_HANDSHAKE_COMPLETED;
    conn->conn_settings.fec_params.fec_encoder_scheme = XQC_XOR_CODE;
    conn->conn_settings.fec_params.fec_decoder_scheme = XQC_XOR_CODE;
    conn->conn_settings.fec_params.fec_code_rate = .2;
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 0) == XQC_OK);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, &stats) == XQC_OK);
    CU_ASSERT(stats.negotiated && stats.encoding_enabled && stats.transitions == 2);
    conn->fec_ctl->fec_send_symbol_num[0] = 3;
    const uint32_t block = conn->fec_ctl->fec_send_block_num[0];
    conn->fec_ctl->fec_recover_pkt_cnt = 7;
    conn->fec_recovered_packets = 7;
    conn->fec_recovered_bytes = 700;
    conn->fec_repair_wait_us = 300;
    conn->fec_received_repair_packets = 12;
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 1) == XQC_OK);
    CU_ASSERT(conn->fec_ctl->fec_send_symbol_num[0] == 0);
    CU_ASSERT(conn->fec_ctl->fec_send_block_num[0] != block);
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 1) == XQC_OK);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, &stats) == XQC_OK);
    CU_ASSERT(stats.encoding_paused && !stats.encoding_enabled && stats.transitions == 3);
    CU_ASSERT(stats.discarded_source_symbols == 3 && stats.recovered_packets == 7);
    CU_ASSERT(stats.recovered_bytes == 700 && stats.repair_wait_us == 300);
    CU_ASSERT(stats.received_repair_packets == 12);
    xqc_packet_out_t packet = {0};
    packet.po_flag = XQC_POF_USE_FEC;
    packet.po_frame_types = XQC_FRAME_BIT_STREAM;
    conn->conn_settings.fec_params.fec_protected_frames = XQC_FRAME_BIT_STREAM;
    CU_ASSERT(xqc_is_packet_fec_protected(conn, &packet) == -XQC_EFEC_NOT_SUPPORT_FEC);
    CU_ASSERT(xqc_process_fec_protected_packet(conn, &packet) == XQC_OK);
    CU_ASSERT(conn->fec_ctl->fec_send_symbol_num[0] == 0);
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 0) == XQC_OK);
    CU_ASSERT(xqc_is_packet_fec_protected(conn, &packet) == XQC_OK);
    conn->conn_settings.fec_level = XQC_FEC_STREAM_LEVEL;
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 1) == -XQC_EFEC_NOT_SUPPORT_FEC);
    conn->conn_settings.fec_level = XQC_FEC_CONN_LEVEL;
    conn->conn_settings.enable_encode_fec = 0;
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 0) == -XQC_EFEC_NOT_SUPPORT_FEC);
    CU_ASSERT(xqc_conn_get_fec_runtime_stats(conn, &stats) == XQC_OK && !stats.negotiated);
    conn->conn_settings.enable_encode_fec = 1;
    conn->conn_state = XQC_CONN_STATE_CLOSING;
    CU_ASSERT(xqc_conn_pause_fec_encoding(conn, 1) == -XQC_ESTATE);
    xqc_engine_destroy(conn->engine);
}

static uint32_t xqc_test_probe_rate(void *opaque) { (void)opaque; return 500000; }

static void
xqc_test_probe_pacing()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    CU_ASSERT_PTR_NOT_NULL_FATAL(conn);
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    xqc_pacing_t *pacing = &ctl->ctl_pacing;
    uint64_t before = xqc_pacing_rate_calc(pacing);
    uint64_t cwnd = ctl->ctl_cong_callback->xqc_cong_ctl_get_cwnd(ctl->ctl_cong);
    CU_ASSERT(before > 1);
    CU_ASSERT(xqc_conn_set_probe_pacing(NULL, 1000, 900000) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 1, 0) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 1, 1000001) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, UINT64_MAX, 900000) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, before/2, 900000) == XQC_OK);
    CU_ASSERT(xqc_pacing_rate_calc(pacing) == before/2);
    CU_ASSERT(ctl->ctl_cong_callback->xqc_cong_ctl_get_cwnd(ctl->ctl_cong) == cwnd);
    xqc_pacing_on_app_limit(pacing);
    CU_ASSERT(pacing->bytes_budget <= 2*XQC_MSS);
    pacing->pending_budget = 10*XQC_MSS;
    xqc_pacing_on_timeout(pacing);
    CU_ASSERT(pacing->bytes_budget <= 2*XQC_MSS && !pacing->pending_budget);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 1, 900000) == XQC_OK);
    pacing->bytes_budget = 0;
    pacing->last_sent_time = xqc_monotonic_timestamp();
    xqc_pacing_on_app_limit(pacing);
    xqc_pacing_on_app_limit(pacing);
    CU_ASSERT(pacing->bytes_budget == 0);
    xqc_timer_unset(&ctl->path_timer_manager, XQC_TIMER_PACING);
    CU_ASSERT(!xqc_pacing_can_write(pacing, XQC_MSS));
    CU_ASSERT(ctl->path_timer_manager.timer[XQC_TIMER_PACING].expire_time <= xqc_monotonic_timestamp()+900000);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 0, 0) == XQC_OK);
    CU_ASSERT(xqc_pacing_rate_calc(pacing) == before);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, before/2, 900000) == XQC_OK);
    conn->probe_pacing_expires = xqc_monotonic_timestamp()-1;
    CU_ASSERT(xqc_pacing_rate_calc(pacing) == before);
    xqc_pacing_on_app_limit(pacing);
    CU_ASSERT(pacing->bytes_budget == 10*XQC_MSS);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, before/2, 900000) == XQC_OK);
    CU_ASSERT(pacing->bytes_budget <= 2*XQC_MSS);
    const xqc_cong_ctrl_callback_t *saved = ctl->ctl_cong_callback;
    xqc_cong_ctrl_callback_t fixed = *saved;
    fixed.xqc_cong_ctl_get_pacing_rate = xqc_test_probe_rate;
    ctl->ctl_cong_callback = &fixed;
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 1000, 900000) == XQC_OK);
    CU_ASSERT(xqc_pacing_rate_calc(pacing) == 1000);
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 0, 0) == XQC_OK);
    CU_ASSERT(xqc_pacing_rate_calc(pacing) == 500000);
    ctl->ctl_cong_callback = saved;
    conn->conn_state = XQC_CONN_STATE_CLOSING;
    CU_ASSERT(xqc_conn_set_probe_pacing(conn, 1000, 900000) == -XQC_ESTATE);
    xqc_engine_destroy(conn->engine);
}

static unsigned xqc_test_ecn_notifications;
static void xqc_test_ecn_notify(void *opaque, xqc_usec_t sent_at)
{
    (void)opaque; CU_ASSERT(sent_at == 123); ++xqc_test_ecn_notifications;
}

static void
xqc_test_ecn_validation()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    CU_ASSERT_PTR_NOT_NULL_FATAL(conn);
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    CU_ASSERT(xqc_conn_enable_ecn(NULL) == -XQC_EPARAM);
    CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK);
    const xqc_cong_ctrl_callback_t *saved = ctl->ctl_cong_callback;
    xqc_cong_ctrl_callback_t callbacks = *saved;
    callbacks.xqc_cong_ctl_on_ecn = xqc_test_ecn_notify;
    ctl->ctl_cong_callback = &callbacks;
    conn->ecn_sent_ect0 = 3;
    xqc_ack_info_t ack = {0}; ack.pns = XQC_PNS_APP_DATA; ack.n_ranges = 1;
    ack.ranges[0].high = 5; ack.has_ecn = 1; ack.ecn[0] = 1;
    xqc_send_ctl_validate_ecn(ctl, &ack, 1, 123);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE);
    ack.ranges[0].high = 6; ack.ecn[2] = 1;
    xqc_send_ctl_validate_ecn(ctl, &ack, 1, 123);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE && conn->ecn_events == 1);
    CU_ASSERT(xqc_test_ecn_notifications == 1);
    /* A reordered ACK with smaller/missing counts cannot fail or recut. */
    ack.ranges[0].high = 5; ack.has_ecn = 0;
    xqc_send_ctl_validate_ecn(ctl, &ack, 1, 123);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE && conn->ecn_events == 1);
    /* A lost ACK can make cumulative growth exceed newly acknowledged marks. */
    ack.ranges[0].high = 7; ack.has_ecn = 1; ack.ecn[2] = 2;
    callbacks.xqc_cong_ctl_on_ecn = NULL;
    callbacks.xqc_cong_ctl_on_lost = xqc_test_ecn_notify;
    xqc_send_ctl_validate_ecn(ctl, &ack, 0, 123);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE && conn->ecn_events == 2);
    CU_ASSERT(xqc_test_ecn_notifications == 2);
    ack.ranges[0].high = 8; ack.ecn[2] = 1; // rollback on a fresh ACK
    xqc_send_ctl_validate_ecn(ctl, &ack, 0, 123);
    CU_ASSERT(conn->ecn_state == XQC_ECN_FAILED && conn->ecn_events == 2);
    CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK && conn->ecn_state == XQC_ECN_FAILED);
    xqc_ecn_stats_t stats = {0};
    CU_ASSERT(xqc_conn_get_ecn_stats(conn, &stats) == -XQC_EPARAM);
    stats.struct_size = sizeof(stats);
    CU_ASSERT(xqc_conn_get_ecn_stats(conn, &stats) == XQC_OK);
    CU_ASSERT(stats.sent_ect0 == 3 && stats.peer_ce == 2 && stats.congestion_events == 2);
    ctl->ctl_cong_callback = saved;
    xqc_engine_destroy(conn->engine);
    for (unsigned mode = 0; mode < 4; ++mode) {
        conn = test_engine_connect_fec(); ctl = conn->conn_initial_path->path_send_ctl;
        CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK);
        conn->ecn_sent_ect0 = 3;
        memset(&ack, 0, sizeof(ack)); ack.pns = XQC_PNS_APP_DATA; ack.n_ranges = 1;
        ack.ranges[0].high = 10; ack.has_ecn = mode != 0;
        if (mode == 2) { ack.ecn[0] = 1; ack.ecn[1] = 1; }
        if (mode == 3) ack.ecn[0] = 4;
        xqc_send_ctl_validate_ecn(ctl, &ack, 1, 123);
        CU_ASSERT(conn->ecn_state == XQC_ECN_FAILED && !conn->ecn_events);
        xqc_engine_destroy(conn->engine);
    }
}

static void
xqc_test_ecn_probe_and_wire()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    xqc_path_ctx_t *path = conn->conn_initial_path;
    CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK);
    conn->conn_flag |= XQC_CONN_FLAG_HANDSHAKE_COMPLETED;
    xqc_packet_out_t *po = xqc_packet_out_create(1500);
    CU_ASSERT_PTR_NOT_NULL_FATAL(po);
    po->po_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER; po->po_pkt.pkt_pns = XQC_PNS_APP_DATA;
    po->po_frame_types = XQC_FRAME_BIT_ACK;
    const xqc_usec_t now = xqc_monotonic_timestamp();
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 0);
    po->po_frame_types = XQC_FRAME_BIT_PING;
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 2);
    conn->ecn_testing_started = now; conn->ecn_sent_ect0 = 10;
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 0 && conn->ecn_state == XQC_ECN_UNKNOWN);
    conn->ecn_state = XQC_ECN_TESTING; conn->ecn_sent_ect0 = 1;
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now+3*xqc_send_ctl_calc_pto(path->path_send_ctl)) == 0);
    conn->ecn_state = XQC_ECN_CAPABLE; path->path_send_ctl->ctl_pto_count = 2;
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 2);
    path->path_send_ctl->ctl_pto_count = 3;
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 0 && conn->ecn_state == XQC_ECN_FAILED);
    path->path_send_ctl->ctl_pto_count = 0;
    CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK);
    CU_ASSERT(xqc_conn_select_ecn(conn, path, po, now) == 0 && conn->ecn_state == XQC_ECN_FAILED);
    /* Receive counters count authenticated nonduplicate physical packets only. */
    xqc_packet_in_t pi = {0}; pi.pi_path_id = 0; pi.pi_pkt.pkt_pns = XQC_PNS_APP_DATA;
    pi.pi_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER; pi.pi_pkt.pkt_num = 41;
    pi.pi_frame_types = XQC_FRAME_BIT_PING; pi.pi_ecn = 2; pi.pkt_recv_time = now;
    xqc_conn_record_single(conn, &pi); xqc_conn_record_single(conn, &pi);
    CU_ASSERT(conn->ecn_rx[XQC_PNS_APP_DATA][0] == 1);
    pi.pi_pkt.pkt_num = 42; pi.pi_ecn = 3; pi.pi_flag = XQC_PIF_FEC_RECOVERED;
    xqc_conn_record_single(conn, &pi);
    CU_ASSERT(conn->ecn_rx[XQC_PNS_APP_DATA][2] == 0);
    pi.pi_flag = 0; pi.pi_pkt.pkt_num = 43; xqc_conn_record_single(conn, &pi);
    pi.pi_pkt.pkt_pns = XQC_PNS_INIT; pi.pi_pkt.pkt_num = 3; pi.pi_ecn = 1;
    xqc_conn_record_single(conn, &pi);
    CU_ASSERT(conn->ecn_rx[XQC_PNS_INIT][1] == 1 && conn->ecn_rx[XQC_PNS_APP_DATA][1] == 0);
    xqc_pn_ctl_t *pn = xqc_get_pn_ctl(conn, path);
    int gap = 0; xqc_packet_number_t largest = 0;
    po->po_frame_types = 0;
    ssize_t length = xqc_gen_ack_frame(conn, po, now+1000, 3,
        &pn->ctl_recv_record[XQC_PNS_APP_DATA], now, &gap, &largest);
    CU_ASSERT_FATAL(length > 0);
    CU_ASSERT(po->po_buf[0] == 3);
    pi.pos = po->po_buf; pi.last = po->po_buf+length; pi.pi_pkt.pkt_pns = XQC_PNS_APP_DATA;
    xqc_ack_info_t ack = {0};
    CU_ASSERT(xqc_parse_ack_frame(&pi, conn, &ack) == XQC_OK && pi.pos == pi.last);
    CU_ASSERT(ack.has_ecn && ack.ecn[0] == 1 && ack.ecn[1] == 0 && ack.ecn[2] == 1);
    unsigned char truncated[] = {3,7,0,0,0,1,0};
    pi.pos = truncated; pi.last = truncated+sizeof(truncated);
    CU_ASSERT(xqc_parse_ack_frame(&pi, conn, &ack) != XQC_OK);
    unsigned char legacy[] = {2,7,0,0,0};
    pi.pos = legacy; pi.last = legacy+sizeof(legacy);
    CU_ASSERT(xqc_parse_ack_frame(&pi, conn, &ack) == XQC_OK && !ack.has_ecn && !ack.ecn[2]);
    xqc_packet_out_destroy(po);
    xqc_engine_destroy(conn->engine);
}

static void
xqc_test_ecn_retained_ack()
{
    xqc_connection_t *conn = test_engine_connect_fec();
    xqc_send_ctl_t *ctl = conn->conn_initial_path->path_send_ctl;
    xqc_pn_ctl_t *pn = xqc_get_pn_ctl(conn, conn->conn_initial_path);
    xqc_send_queue_t *queue = conn->conn_send_queue;
    xqc_packet_out_t *po = xqc_packet_out_get(queue);
    CU_ASSERT_PTR_NOT_NULL_FATAL(po);
    CU_ASSERT(xqc_conn_enable_ecn(conn) == XQC_OK);
    conn->conn_settings.disable_pn_skipping = 1;
    conn->ecn_sent_ect0 = 1;
    const xqc_usec_t now = xqc_monotonic_timestamp();
    po->po_pkt.pkt_pns = XQC_PNS_APP_DATA; po->po_pkt.pkt_type = XQC_PTYPE_SHORT_HEADER;
    po->po_pkt.pkt_num = 10; po->po_path_id = 0; po->po_ecn = 2;
    po->po_used_size = 100; po->po_sent_time = now-1000;
    po->po_frame_types = XQC_FRAME_BIT_PING;
    /* A retransmission reference keeps this physically ACKed original in the
     * unacked list. A later ACK covering it must not count its ECN mark twice. */
    po->po_origin_ref_cnt = 1;
    xqc_send_queue_insert_unacked(po, &queue->sndq_unacked_packets[XQC_PNS_APP_DATA], queue);
    pn->ctl_largest_sent[XQC_PNS_APP_DATA] = 11;
    xqc_ack_info_t ack = {0}; ack.pns = XQC_PNS_APP_DATA; ack.n_ranges = 1;
    ack.ranges[0].low = ack.ranges[0].high = 10; ack.has_ecn = 1; ack.ecn[0] = 1;
    CU_ASSERT(xqc_send_ctl_on_ack_received(ctl, pn, queue, &ack, now, 1) == XQC_OK);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE && po->po_acked);
    ack.ranges[0].high = 11; // newly ACKed Not-ECT plus the retained original
    CU_ASSERT(xqc_send_ctl_on_ack_received(ctl, pn, queue, &ack, now+1000, 1) == XQC_OK);
    CU_ASSERT(conn->ecn_state == XQC_ECN_CAPABLE);
    po->po_origin_ref_cnt = 0;
    xqc_send_queue_maybe_remove_unacked(po, queue, NULL);
    xqc_engine_destroy(conn->engine);
}

void
xqc_test_fec()
{
    xqc_test_packet_observer();
    xqc_test_fec_runtime_pause();
    xqc_test_probe_pacing();
    xqc_test_ecn_validation();
    xqc_test_ecn_retained_ack();
    xqc_test_ecn_probe_and_wire();
    xqc_test_fec_scheme_setter();
    xqc_test_fec_negotiation();
    xqc_test_write_repair_packet();
    xqc_test_gen_fec_frames();
    xqc_test_chk_fec_param();
    xqc_test_encoder_chk_param();
    xqc_test_process_src_syb();
}

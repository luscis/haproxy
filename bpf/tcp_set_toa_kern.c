// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Easystack */

#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct tcp_toa_option {
	__u8 kind;
	__u8 len;
	__u16 port;
	__u32 addr;
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, __be32);
	__type(value, struct tcp_toa_option);
} toa_conn_store SEC(".maps");


SEC("sockops")
int set_toa_tcp_bs(struct bpf_sock_ops *skops) {
	int rv = 7;
	int op = (int) skops->op;
	struct bpf_sock *sk = skops->sk;

	if (!sk)
		return 1;

	struct tcp_toa_option *data = NULL;
	struct tcp_toa_option useropt = {
		.kind = 254,
		.len = 0,
		.port = 0,
		.addr = 0,
	};

	data = bpf_sk_storage_get(&toa_conn_store, sk, &useropt, BPF_SK_STORAGE_GET_F_CREATE);
	if (!data)
		return 1;

	switch (op) {
	case BPF_SOCK_OPS_TCP_CONNECT_CB: 
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB: {
		bpf_sock_ops_cb_flags_set(skops, 
			skops->bpf_sock_ops_cb_flags |
			BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG);
		break;
	}

	case BPF_SOCK_OPS_HDR_OPT_LEN_CB: {
		int option_len = sizeof(struct tcp_toa_option);

		if (skops->args[1] + option_len <= 40) {
			rv = option_len;
		} else {
			rv = 0;
		}

		bpf_reserve_hdr_opt(skops, rv, 0);
		break;
	}

	case BPF_SOCK_OPS_WRITE_HDR_OPT_CB: {
		struct tcp_toa_option opt = {
			.kind = data->kind,
			.len  = 8,
			.port = bpf_htons(data->port),
			.addr = bpf_htonl(data->addr),
		};

		int ret = bpf_store_hdr_opt(skops, &opt, sizeof(opt), 0);

		bpf_printk("set_toa_tcp_bs port=%d\n", opt.port);
		break;
	}

	default:
		rv = -1;
	}

	skops->reply = rv;

	return 1;
}

char _license[] SEC("license") = "GPL";
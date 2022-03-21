#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <arpa/inet.h>

#include "nlroute.h"

static int set_iface(const char *name, int up)
{
	int idx;

	idx = nlr_iface_idx(name);
	if (idx < 0)
		return -1;

	return nlr_set_iface(idx, up);
}

#define IFACE_IDX_FAILED(ifname) printf("Failed to determine index of \"%s\" iface\n", ifname);

static int set_iface_addr(const char *name, const char * s_addr)
{
	int idx;
	unsigned char addr[6];

	idx = nlr_iface_idx(name);
	if (idx < 0) {
		IFACE_IDX_FAILED(name);
		return -1;
	}

	sscanf(s_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
	       &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]);

	return nlr_set_mac_addr(idx, addr);
}

static int manage_addr(const char *iface, const char *s_addr,
		       int (*f)(int, in_addr_t, int))
{
	char buf[32], *p;
	in_addr_t addr;
	int iface_idx, prefix_len;

	p = strchr(s_addr, '/');
	if (!p)
		return -1;

	if (sscanf(p + 1, "%d", &prefix_len) != 1)
		return -1;

	if (prefix_len < 0 || prefix_len > 32)
		return -1;

	memcpy(buf, s_addr, p - s_addr);
	*(buf + (p - s_addr)) = '\0';
	addr = inet_addr(buf);

	iface_idx = nlr_iface_idx(iface);
	if (iface_idx < 0) {
		IFACE_IDX_FAILED(iface);
		return -1;
	}

	return f(iface_idx, addr, prefix_len);
}

/* Addr: 10.10.10.10/24 */
static int add_addr(const char *iface, const char *addr)
{
	return manage_addr(iface, addr, nlr_add_addr);
}

static int del_addr(const char *iface, const char *addr)
{
	return manage_addr(iface, addr, nlr_del_addr);
}

static int get_addr(const char *iface)
{
	int iface_idx = -1, err;
	char buf[32], *name;
	struct in_addr in;
	struct nlr_addr *addr, *p;

	if (iface) {
		iface_idx = nlr_iface_idx(iface);
		if (iface_idx < 0) {
			IFACE_IDX_FAILED(iface);
			return -1;
		}
	}

	addr = nlr_get_addr(iface_idx, &err);

	for (p = addr; p; p = p->pnext) {
		name = iface ? (char *)iface : nlr_iface_name(p->iface_idx);
		if (!name) {
			printf("Failed to determine name of iface #%d\n", p->iface_idx);
			continue;
		}

		in.s_addr = p->addr;
		printf("%s %s/%d\n", name, inet_ntoa(in), p->prefix_len);

		if (name != iface)
			free(name);
	}

	nlr_addr_free(addr);

	return err;
}

const char *nlr_iface_type2str(enum nlr_iface_type type)
{
	static const char *t[] = {
		[NLR_IFACE_LOOPBACK] = "Loopback",
		[NLR_IFACE_ETHER] = "Ethernet",
	};

	return type >= 0 && type < sizeof(t)/sizeof(t[0]) && t[type] ?
		t[type] : "Unknown";
}

static int get_iface_info(const char *iface_name)
{
	struct nlr_iface *iface, *p;
	int iface_idx = -1, err;

	if (iface_name) {
		iface_idx = nlr_iface_idx(iface_name);
		if (iface_idx < 0) {
			IFACE_IDX_FAILED(iface_name);
			return -1;
		}
	}

	iface = nlr_iface(iface_idx, &err);

	for (p = iface; p; p = p->pnext) {
		printf("\niface %s\n"
		       "         idx: %d\n"
		       "        type: %s\n"
		       "         mtu: %d\n"
		       "       state: %s\n"
		       "     carrier: %s\n"
		       "        addr: %02x:%02x:%02x:%02x:%02x:%02x\n"
		       "    tx_bytes: %ld\n"
		       "  tx_packets: %ld\n"
		       "    rx_bytes: %ld\n"
		       "  rx_packets: %ld\n",
		       p->name, p->idx, nlr_iface_type2str(p->type),
		       p->mtu, p->is_up ? "Up" : "Down",
		       p->carrier_on ? "Yes" : "No",
		       p->addr[0], p->addr[1], p->addr[2],
		       p->addr[3], p->addr[4], p->addr[5],
		       p->stats.tx_bytes, p->stats.tx_packets,
		       p->stats.rx_bytes, p->stats.rx_packets
		);
	}

	nlr_iface_free(iface);

	return err;
}

static void help(void)
{
	printf("\nUsage: [OPTIONS] OBJECT CMD [CMD_OPTIONS]" \
	       "\nOptions: -d -- log level info, -d2 -- debug, -h -- help" \
	       "\n$ ip link [show [IFACE]]" \
	       "\n$ ip link set IFACE up|down"
	       "\n$ ip link set IFACE addr hh:hh:hh:hh:hh:hh" \
	       "\n$ ip addr [show [IFACE]]" \
	       "\n$ ip addr add|del IFACE ADDR/BITS" \
	       "\n"
	);
}

enum object {
	OBJECT_INVAL,
	OBJECT_LINK,
	OBJECT_ADDR,
};

enum cmd {
	CMD_INVAL,
	CMD_LINK_SHOW,
	CMD_LINK_SET_UP,
	CMD_LINK_SET_DOWN,
	CMD_LINK_SET_ADDR,
	CMD_ADDR_ADD,
	CMD_ADDR_DEL,
	CMD_ADDR_SHOW,
};

int main(int argc, char *argv[])
{
	int r = -1, i, logmask;
	char *iface;
	enum cmd cmd = CMD_INVAL;
	enum object obj = OBJECT_INVAL;
	const char *addr;

	if (!argv[1] || !strcmp(argv[1], "-h")) {
		help();
		return 0;
	}

	i = 1;

	logmask = LOG_MASK(LOG_ERR);
	if (!strcmp(argv[i], "-d")) {
		logmask |= LOG_MASK(LOG_INFO);
		i++;
	} else if (!strcmp(argv[i], "-d2")) {
		logmask |= LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG);
		i++;
	}

	if (!argv[i]) {
		help();
		goto fin;
	}

	if (!strcmp(argv[i], "link")) {
		obj = OBJECT_LINK;
		i++;
		if (!argv[i]) {
			cmd = CMD_LINK_SHOW;
			iface = NULL;
		} else if (!strcmp(argv[i], "show")) {
			cmd = CMD_LINK_SHOW;
			iface = argv[i + 1];
		} else if (!strcmp(argv[i], "set")) {
			iface = argv[++i];
			if (iface && argv[++i]) {
				if (!strcmp(argv[i], "up"))
					cmd = CMD_LINK_SET_UP;
				else if (!strcmp(argv[i], "down"))
					cmd = CMD_LINK_SET_DOWN;
				else if (!strcmp(argv[i], "addr")) {
					cmd = CMD_LINK_SET_ADDR;
					addr = argv[++i];
				}
			}
		}
	} else if (!strcmp(argv[i], "addr")) {
		obj = OBJECT_ADDR;
		i++;
		if (argv[i]) {
			if (!strcmp(argv[i], "show")) {
				cmd = CMD_ADDR_SHOW;
				iface = argv[i + 1];
			} else if (!strcmp(argv[i], "add")) {
				iface = argv[++i];
				if (iface && argv[++i]) {
					cmd = CMD_ADDR_ADD;
					addr = argv[i];
				}
			} else if (!strcmp(argv[i], "del")) {
				iface = argv[++i];
				if (iface && argv[++i]) {
					cmd = CMD_ADDR_DEL;
					addr = argv[i];
				}
			}
		}
	}

	if (obj == OBJECT_INVAL || cmd == CMD_INVAL) {
		help();
		goto fin;
	}

	openlog(NULL, LOG_PERROR, LOG_USER);
	setlogmask(logmask);

	if (nlr_init())
		goto fin;

	if (obj == OBJECT_LINK) {
		switch (cmd) {
			case CMD_LINK_SHOW:
				r = get_iface_info(iface);
				break;

			case CMD_LINK_SET_UP:
				r = set_iface(iface, 1);
				break;

			case CMD_LINK_SET_DOWN:
				r = set_iface(iface, 0);
				break;

			case CMD_LINK_SET_ADDR:
				r = set_iface_addr(iface, addr);
				break;
		}
	} else if (obj == OBJECT_ADDR) {
		switch (cmd) {
			case CMD_ADDR_ADD:
				r = add_addr(iface, addr);
				break;

			case CMD_ADDR_DEL:
				r = del_addr(iface, addr);
				break;

			case CMD_ADDR_SHOW:
				r = get_addr(iface);
				break;
		}
	}

fin:
	nlr_fin();
	closelog();

	printf("%s\n", r ? "Failed" : "Done");

	return r;
}

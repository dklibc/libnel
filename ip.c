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

	if (sscanf(s_addr, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
	       &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5])
	       != 6) {
	       printf("Invalid format of MAC addr\n");
	       return -1;
	}

	return nlr_set_mac_addr(idx, addr);
}

/* "x.x.x.x" or "y.y.y.y/n" */
static int parse_addr(const char *s, in_addr_t *addr,
	int *plen)
{
	char buf[32], *p, c;

	strncpy(buf, s, sizeof(buf));
	p = strchr(buf, '/');
	if (!p) {
		*plen = 32;
	} else {
		if (sscanf(p + 1, "%d%c", plen, &c) != 1 ||
		  *plen < 0 || *plen > 32) {
			return -1;
		}
		*p = '\0';
	}
	*addr = inet_addr(buf);
	return (*addr != INADDR_NONE) ? 0 : -1;
}

static int manage_addr(const char *iface, const char *s_addr,
		       int (*f)(int, in_addr_t, int))
{
	in_addr_t addr;
	int iface_idx, plen;

	iface_idx = nlr_iface_idx(iface);
	if (iface_idx < 0) {
		IFACE_IDX_FAILED(iface);
		return -1;
	}

	if (parse_addr(s_addr, &addr, &plen)) {
		printf("Invalid address format\n");
		return -1;
	}

	return f(iface_idx, addr, plen);
}

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

int main(int argc, char *argv[])
{
	int r = 1, i, logmask;
	const char *obj, *cmd;

	if (!argv[1] || !strcmp(argv[1], "-h")) {
		help();
		return 0;
	}

	/* Parse common options */
	i = 1;
	logmask = LOG_MASK(LOG_ERR);
	if (!strcmp(argv[i], "-d")) {
		logmask |= LOG_MASK(LOG_INFO);
		i++;
	} else if (!strcmp(argv[i], "-d2")) {
		logmask |= LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG);
		/* Enable debugging in the libnel */
		setenv("NLOG_DEBUG", "1", 0);
		i++;
	}

	if (!argv[i]) {
		help();
		return 0;
	}

	openlog(NULL, LOG_PERROR, LOG_USER);
	setlogmask(logmask);

	if (nlr_init()) {
		printf("nlroute init failed\n");
		return -1;
	}

	obj = argv[i];
	cmd = argv[i + 1];
	argv = argv + i + 2;
	if (!strcmp(obj, "link")) {
		if (!cmd) {
			r = get_iface_info(NULL);
		} else if (!strcmp(cmd, "show")) {
			r = get_iface_info(argv[0]);
		} else if (!strcmp(cmd, "set")) {
			if (argv[0]) { /* IFACE */
				if (!strcmp(argv[1], "up"))
					r = set_iface(argv[0], 1);
				else if (!strcmp(argv[1], "down"))
					r = set_iface(argv[0], 0);
				else if (!strcmp(argv[1], "addr")) {
					if (argv[2]) {
						r = set_iface_addr(argv[0],
						  argv[2]);
					}
				}
			}
		}
	} else if (!strcmp(obj, "addr")) {
		if (!cmd) {
			r = get_addr(NULL);
		} else if (!strcmp(cmd, "show")) {
			r = get_addr(argv[0]);
		} else if (!strcmp(cmd, "add")) {
			if (argv[0] && argv[1]) {
				r = add_addr(argv[0], argv[1]);
			}
		} else if (!strcmp(cmd, "del")) {
			if (argv[0] && argv[1]) {
				r = del_addr(argv[0], argv[1]);
			}
		}
	}

	if (r > 0)
		help();
	else
		printf("%s\n", r ? "Failed" : "Done");

	nlr_fin();

	closelog();

	return r;
}

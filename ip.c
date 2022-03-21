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

static const char *route_type_str[] = {
	[RTN_UNICAST] = "unicast",
	[RTN_BROADCAST] = "broadcast",
	[RTN_LOCAL] = "local",
	[RTN_UNREACHABLE] = "unreachable",
	[RTN_BLACKHOLE] = "blackhole",
	[RTN_PROHIBIT] = "prohibit",
};

static const char *route_scope_str[] = {
	[RT_SCOPE_HOST] = "host",
	[RT_SCOPE_LINK] = "link",
	[RT_SCOPE_UNIVERSE] = "universe",
	[RT_SCOPE_NOWHERE] = "nowhere",
};

static const char *route_proto_str[] = {
	[RTPROT_KERNEL] = "kernel", /* Route installed by kernel */
	[RTPROT_STATIC] = "static", /* Route installed by admin */
	[RTPROT_BOOT] = "boot", /* Route installed during boot */
	[RTPROT_DHCP] = "dhcp", /* Route installed by DHCP client */
	[RTPROT_BGP] = "bgp", /* BGP routes */
	[RTPROT_ISIS] = "isis", /* ISIS routes */
	[RTPROT_OSPF] = "ospf", /* OSPF routes*/
};

static const char *route_table_str[] = {
	[RT_TABLE_MAIN] = "main",
	[RT_TABLE_LOCAL] = "local",
};

static const char *code2str(int code, const char *str[], int nstrs)
{
	static char buf[4];

	if (code < 0 || code >= nstrs || !str[code])
		snprintf(buf, sizeof(buf), "%d", code);
	return str[code];
}

static int str2code(const char *s, const char *str[], int n)
{
	int i;
	char c;

	for (i = 0; i < n && (!str[i] || strcmp(str[i], s)); i++);
	if (i < n)
		return i;
	return i < n || sscanf(s, "%u%c", &i, &c) == 1 ? i : -1;
}

static int str_cmp(const void *e1, const void *e2)
{
	const char *s1 = *(const char **)e1, *s2 = *(const char **)e2;
	if (!s2)
		return -1;
	if (!s1)
		return 1;
	return strcmp(s1, s2);
}

static void print_code_strs(const char *str[], int nstrs)
{
	const char **ord;
	int i;

	ord = malloc(nstrs * sizeof(str[0]));
	if (!ord)
		return;
	memcpy(ord, str, nstrs * sizeof(str[0]));
	qsort(ord, nstrs, sizeof(ord[0]), str_cmp);
	for (i = 0; i < nstrs && ord[i]; i++)
		printf(" * %s\n", ord[i]);
	printf("\n");
	free(ord);
}

/* Return number of elements in static array */
#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))

#define CODE2STR(code, str) (code2str(code, str, COUNT_OF(str)))

static void print_route(struct nlr_route *r)
{
	struct in_addr in;
	const char *oif;

	/* Mimic "ip route" output */

	if (r->dest) {
		in.s_addr = r->dest;
		if (r->dest_plen == 32)
			printf("%s", inet_ntoa(in));
		else
			printf("%s/%d", inet_ntoa(in), r->dest_plen);
		if (r->gw) {
			in.s_addr = r->gw;
			printf(" via %s", inet_ntoa(in));
		}
	} else {
		in.s_addr = r->gw;
		printf("default via %s", inet_ntoa(in));
	}

	/*
	if (r->table != RT_TABLE_MAIN && r->table != RT_TABLE_LOCAL) {
		printf(" table %s", CODE2STR(r->table, route_table_str));
	}
	*/

	/*
	if (r->type != RTN_UNICAST) {
		printf(" type %s", CODE2STR(r->type, route_type_str));
	}
	*/

	oif = nlr_iface_name(r->oif);
	printf(" dev %s", oif);
	free((void *)oif);

	printf(" proto %s", CODE2STR(r->proto, route_proto_str));

	if (r->scope != RT_SCOPE_UNIVERSE)
		printf(" scope %s", CODE2STR(r->scope, route_scope_str));

	if (r->prefsrc) {
		in.s_addr = r->prefsrc;
		printf(" src %s", inet_ntoa(in));
	}

	if (r->flags & RTNH_F_LINKDOWN)
		printf(" linkdown");
	printf("\n");
}

/* 24 --> 255.255.255.0 */
static unsigned netmask(int plen)
{
	return htonl(~0 << (32 - plen));
}

static void init_route_filter(struct nlr_route *filter)
{
	filter->dest = INADDR_NONE;
	filter->dest_plen = -1;
	filter->gw = INADDR_NONE;
	filter->table = -1;
	filter->type = -1;
	filter->scope = -1;
	filter->proto = -1;
}

static int get_route(const char *s_addr)
{
	struct nlr_route *r, *p, *q, filter;
	int err;
	in_addr_t addr;

	addr = inet_addr(s_addr);
	if (addr == INADDR_ANY) {
		printf("Invalid address format\n");
		return -1;
	}
	init_route_filter(&filter);
	//filter.table = RT_TABLE_MAIN;
	r = nlr_get_routes(&filter, &err);
	if (err) {
		nlr_free_routes(r);
		return -1;
	}
	if (!r)
		return 0;
	for (p = r, q = NULL; p; p = p->pnext) {
		if ((addr & netmask(p->dest_plen)) == p->dest) {
			if (!q || p->dest_plen > q->dest_plen)
				q = p;
		}
	}
	if (q)
		print_route(q);
	nlr_free_routes(r);
	return 0;
}


#define STR2CODE(s, str) str2code(s, str, COUNT_OF(str))
#define PRINT_CODE_STRS(str) print_code_strs(str, COUNT_OF(str))

static int show_routes(char *w[])
{
	int err;
	struct nlr_route *h, *r, filter;
	int i, mimic_iproute = 1;

	init_route_filter(&filter);

	if (!w || !w[0])
		goto w_processing_done;

	if (!strcmp(w[0], "all")) {
		if (w[1]) {
			printf("Expecting nothing after \"all\"\n");
			return -1;
		}
		mimic_iproute = 0;
		goto w_processing_done;
	}

	for (i = 0; w[i]; i += 2) {
		if (!w[i + 1]) {
			printf("Option \"%s\" expected value!\n", w[i]);
			return -1;
		}
		if (!strcmp(w[i], "dest")) {
			if (filter.dest != INADDR_NONE) {
			}
			if (parse_addr(w[i + 1], &filter.dest,
				&filter.dest_plen)) {
				printf("Invalid format of destination address\n");
				return -1;
			}
		} else if (!strcmp(w[i], "gw")) {
			if (filter.gw != INADDR_NONE) {
			}
			filter.gw = inet_addr(w[i + 1]);
			if (filter.gw == INADDR_NONE) {
				printf("Invalid format of gateway address\n");
				return -1;
			}
		} else if (!strcmp(w[i], "type")) {
			if (filter.type >= 0) {
			}
			filter.type = STR2CODE(w[i + 1], route_type_str);
			if (filter.type < 0) {
				printf("Unknown route type. Use numeric value or one of:\n");
				PRINT_CODE_STRS(route_type_str);
				return -1;
			}
		} else if (!strcmp(w[i], "scope")) {
			if (filter.scope >= 0) {
			}
			filter.scope = STR2CODE(w[i + 1], route_scope_str);
			if (filter.scope < 0) {
				printf("Unknown route scope. Use numeric value or one of:\n");
				PRINT_CODE_STRS(route_scope_str);
				return -1;
			}
		} else if (!strcmp(w[i], "proto")) {
			if (filter.proto >= 0) {
			}
			filter.proto = STR2CODE(w[i + 1], route_proto_str);
			if (filter.proto < 0) {
				printf("Unknown route protocol. Use numeric value or one of:\n");
				PRINT_CODE_STRS(route_proto_str);
				return -1;
			}
		} else if (!strcmp(w[i], "table")) {
			if (filter.table >= 0) {
			}
			filter.table = STR2CODE(w[i + 1], route_table_str);
			if (filter.table < 0) {
				printf("Unknown route table. Use numeric value or one of:\n");
				PRINT_CODE_STRS(route_table_str);
				return -1;
			}
		} else {
			printf("Unknown option: \"%s\"\n", w[i]);
			return -1;
		}
	}
w_processing_done:

	h = nlr_get_routes(&filter, &err);
	if (err)
		return -1;

	if (mimic_iproute) {
		/*
		 * If user doesn't specify a filter, then mimic "ip route
		 * output".
		 */
		for (r = h; r; r = r->pnext) {
			if (r->table != RT_TABLE_MAIN
				&& r->table != RT_TABLE_LOCAL)
				continue;
			if (r->type != RTN_UNICAST && r->type != RTN_LOCAL)
				continue;
			if (r->scope != RT_SCOPE_UNIVERSE
				&& r->scope != RT_SCOPE_LINK)
				continue;
			print_route(r);
		}
	} else {
		for (r = h; r; r = r->pnext)
			print_route(r);
	}

	nlr_free_routes(h);
	return err;
}

static int manage_route(const char *dest, const char *gw,
		       int (*f)(in_addr_t, int, in_addr_t))
{
	in_addr_t dest_addr, gw_addr;
	int dest_plen;

	if (parse_addr(dest, &dest_addr, &dest_plen)) {
		printf("Invalid format of dest addr\n");
		return -1;
	}
	gw_addr = inet_addr(gw);
	if (gw_addr == INADDR_NONE) {
		printf("Invalid gw addr\n");
		return -1;
	}
	return f(dest_addr, dest_plen, gw_addr);
}

static int add_route(const char *dest, const char *gw)
{
	return manage_route(dest, gw, nlr_add_route);
}

static int del_route(const char *dest, const char *gw)
{
	return manage_route(dest, gw, nlr_del_route);
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
	       "\n$ ip route [show [dest ADDR/BITS] [proto PROTO] [type TYPE] [scope SCOPE] [table TABLE] [gw ADDR]]" \
	       "\n$ ip route show all" \
	       "\n$ ip route get ADDR" \
	       "\n$ ip route add|del DEST/BITS via GW" \
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
	} else if (!strcmp(obj, "route")) {
		if (!cmd) {
			r = show_routes(NULL);
		} else if (!strcmp(cmd, "show")) {
			r = show_routes(argv);
		} else if (!strcmp(cmd, "get")) {
			if (!argv[1]) {
				r = get_route(argv[0]);
			}
		} else if (!strcmp(cmd, "add")) {
			if (argv[0] && !strcmp(argv[1], "via")
				&& argv[2]) {
				r = add_route(argv[0], argv[2]);
			}
		} else if (!strcmp(cmd, "del")) {
			if (argv[0] && !strcmp(argv[2], "via")
				&& argv[2]) {
				r = del_route(argv[0], argv[2]);
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

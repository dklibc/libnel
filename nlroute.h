#ifndef _NLROUTE_H
#define _NLROUTE_H

#include <net/if.h>
#include <arpa/inet.h>
#include <linux/rtnetlink.h>

int nlr_init(void);
void nlr_fin(void);

int nlr_iface_idx(const char *name);
char *nlr_iface_name(int idx);

/* See "/usr/include/linux/if_arp.h" */
enum nlr_iface_type {
	NLR_IFACE_ETHER = 1,
	NLR_IFACE_LOOPBACK = 772,
	NLR_IFACE_WIRELESS = 801,
};

struct nlr_iface {
	int idx;
	char *name;
	unsigned char addr[6];
	int mtu;
	enum nlr_iface_type type;
	int is_up;
	int carrier_on;
	struct {
		long tx_bytes, tx_packets;
		long rx_bytes, rx_packets;
	} stats;
	struct nlr_iface *pnext;
};

/*
 * @iface_idx can be <0, in this case return all ifaces.
 * If returns NULL, you can distinguish 'no-ifaces' case and
 * 'error-occured' case by @err value.
 */
struct nlr_iface *nlr_iface(int iface_idx, int *err);

void nlr_iface_free(struct nlr_iface *iface);

int nlr_add_addr(int iface_idx, in_addr_t addr, int prefix_len);
int nlr_del_addr(int iface_idx, in_addr_t addr, int prefix_len);

struct nlr_addr {
	in_addr_t addr;
	int prefix_len;
	struct nlr_addr *pnext;
	int iface_idx;
};

struct nlr_addr *nlr_get_addr(int iface_idx, int *err);

void nlr_addr_free(struct nlr_addr *addr);

int nlr_set_iface(int iface_idx, int up);

#define NLR_IFACE_UP(idx) nlr_set_iface(idx, 1)
#define NLR_IFACE_DOWN(idx) nlr_set_iface(idx, 0)

int nlr_set_mac_addr(int iface_idx, char addr[6]);

struct nlr_route {
	int table;
	int type;
	int scope;
	int proto;
	int metrics;
	unsigned flags;

	in_addr_t dest;
	int dest_plen;
	in_addr_t gw;
	int oif; /* Output interface index */
	in_addr_t prefsrc; /* Preffered source address */

	struct nlr_route *pnext;
};

int nlr_add_route(in_addr_t dest, int dest_plen, in_addr_t gw);
int nlr_del_route(in_addr_t dest, int dest_plen, in_addr_t gw);
/*
 * You can filter what routes you want to get by setting this
 * fields of @filter: @table, @type, @scope, @proto, @dest,
 * @dest_plen, @gw, @oif, @prefsrc. If int field has negative value,
 * it will be treated as unset ("any"). If addr field has INADDR_NONE(-1)
 * value, it will be treated as unset ("any"). @filter can be NULL.
 */
struct nlr_route *nlr_get_routes(struct nlr_route *filter, int *err);
void nlr_free_routes(struct nlr_route *r);

#endif

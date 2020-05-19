/*
 * Due to the bug in the kernel netlink code you can not get addresses
 * of only one interface. Kernel always returns addresses of all interfaces.
 * The same is true when you try to get info about the specified interface.
 * Programs depend on this bug, so it will never be fixed.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "nlcore.h"
#include "nlroute.h"
#include "nlog.h"

static struct nl_sock nlsock;

int nlr_init(void)
{
	nlog_init();

	return nl_open(&nlsock, NETLINK_ROUTE);
}

void nlr_fin(void)
{
	nl_close(&nlsock);
}

static char *add_hdr(char *p, void *hdr, int len)
{
	memcpy(p, hdr, len);

	return p + NLMSG_ALIGN(len);
}

static char *add_rta(char *p, int type, int len, void *data)
{
	struct rtattr *rta = (struct rtattr *)p;

	rta->rta_type = type;
	rta->rta_len = RTA_LENGTH(len);
	memcpy(RTA_DATA(p), data, len);

	return p + RTA_SPACE(len);
}

struct iface_name_cb_priv {
	int idx;
	char name[32];
};

static int iface_name_cb(struct nlmsghdr *nlhdr, void *_priv)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	struct iface_name_cb_priv *priv = (struct iface_name_cb_priv *)_priv;
	int n;

	if (!nlhdr)
		return 0;

	ifi = NLMSG_DATA(nlhdr);

	if (ifi->ifi_index != priv->idx)
		return 0;

	for (rta = IFLA_RTA(ifi), n = RTM_PAYLOAD(nlhdr);
	     RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
		if (rta->rta_type == IFLA_IFNAME) {
			strncpy(priv->name, RTA_DATA(rta), sizeof(priv->name));
			break;
		}
	}

	return 0;
}

char *nlr_iface_name(int idx)
{
	char buf[128], *p;
	struct ifinfomsg ifi;
	struct iface_name_cb_priv priv;

	memset(buf, 0, sizeof(buf));
	p = nlmsg_put_hdr(buf, RTM_GETLINK, NLM_F_DUMP);

	memset(&ifi, 0, sizeof(ifi));
	ifi.ifi_index = idx;
	//ifi.ifi_change = 0xffffffff;
	p = add_hdr(p, &ifi, sizeof(ifi));

	if (nl_send_msg(&nlsock, buf, p - buf))
		return NULL;

	priv.idx = idx;
	priv.name[0] = '\0';

	if (nl_recv_msg(&nlsock, RTM_NEWLINK, iface_name_cb, &priv))
		return NULL;

	return strdup(priv.name);
}

struct iface_idx_cb_priv {
	int idx;
	const char *name;
};

static int iface_idx_cb(struct nlmsghdr *nlhdr, void *_priv)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	struct iface_idx_cb_priv *priv = (struct iface_idx_cb_priv *)_priv;
	int n;

	if (!nlhdr)
		return 0;

	ifi = NLMSG_DATA(nlhdr);

	for (rta = IFLA_RTA(ifi), n = RTM_PAYLOAD(nlhdr);
	     RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
		if (rta->rta_type == IFLA_IFNAME) {
			DEBUG("IFLA_NAME: %s", (char *)RTA_DATA(rta));
			if (!strcmp(priv->name, RTA_DATA(rta))) {
				priv->idx = ifi->ifi_index;
				DEBUG("iface %s has idx %d", priv->name,
				      priv->idx);
			}
			break;
		}
	}

	return 0;
}

int nlr_iface_idx(const char *name)
{
	char buf[128], *p;
	struct ifinfomsg ifi;
	struct iface_idx_cb_priv priv;

	memset(buf, 0, sizeof(buf));

	p = nlmsg_put_hdr(buf, RTM_GETLINK, NLM_F_DUMP);

	memset(&ifi, 0, sizeof(ifi));
	//ifi.ifi_change = 0xffffffff;
	ifi.ifi_flags = 0xffffffff;
	p = add_hdr(p, &ifi, sizeof(ifi));

	if (nl_send_msg(&nlsock, buf, p - buf))
		return -1;

	priv.idx = -1;
	priv.name = name;

	if (nl_recv_msg(&nlsock, RTM_NEWLINK, iface_idx_cb, &priv))
		return -1;

	return priv.idx;
}

void nlr_iface_free(struct nlr_iface *iface)
{
	struct nlr_iface *p;

	while (iface) {
		free(iface->name);
		p = iface->pnext;
		free(iface);
		iface = p;
	}
}

#define IFF_LOWER_UP (1<<16)

struct iface_cb_priv {
	struct nlr_iface *iface;
	int iface_idx;
	int err;
};

static int iface_cb(struct nlmsghdr *nlhdr, void *_priv)
{
	struct ifinfomsg *ifi;
	struct rtattr *rta;
	struct iface_cb_priv *priv = (struct iface_cb_priv *)_priv;
	int n;
	struct nlr_iface *iface;
	struct rtnl_link_stats *stats;

	if (!nlhdr || priv->err)
		return 0;

	ifi = NLMSG_DATA(nlhdr);

	if (priv->iface_idx >= 0 && priv->iface_idx != ifi->ifi_index)
		return 0;

	iface = calloc(1, sizeof(struct nlr_iface));
	if (!iface) {
		ERRNO("failed to alloc nlr_iface");
		priv->err = 1;
		return 0;
	}

	iface->idx = ifi->ifi_index;
	iface->type = ifi->ifi_type;
	iface->is_up = ifi->ifi_flags & IFF_UP;
	iface->carrier_on = ifi->ifi_flags & IFF_LOWER_UP;
	iface->mtu = -1;

	for (rta = IFLA_RTA(ifi), n = RTM_PAYLOAD(nlhdr);
	     RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
		if (rta->rta_type == IFLA_IFNAME) {
			iface->name = strdup(RTA_DATA(rta));
		} else if (rta->rta_type == IFLA_MTU) {
			iface->mtu = *(int *)RTA_DATA(rta);
		} else if (rta->rta_type == IFLA_ADDRESS) {
			memcpy(iface->mac, RTA_DATA(rta), 6);
		} else if (rta->rta_type == IFLA_STATS) {
			stats = (struct rtnl_link_stats *)RTA_DATA(rta);
			iface->stats.tx_bytes = stats->tx_bytes;
			iface->stats.tx_packets = stats->tx_packets;
			iface->stats.rx_bytes = stats->rx_bytes;
			iface->stats.rx_packets = stats->rx_packets;
		}
	}

	iface->pnext = priv->iface;
	priv->iface = iface;

	return 0;
}

struct nlr_iface *nlr_iface(int iface_idx, int *err)
{
	char buf[128], *p;
	struct ifinfomsg ifi;
	struct iface_cb_priv priv;

	if (err)
		*err = -1;

	memset(buf, 0, sizeof(buf));

	p = nlmsg_put_hdr(buf, RTM_GETLINK, NLM_F_DUMP);

	memset(&ifi, 0, sizeof(ifi));
	//ifi.ifi_change = 0xffffffff;
	p = add_hdr(p, &ifi, sizeof(ifi));

	if (nl_send_msg(&nlsock, buf, p - buf))
		return NULL;

	priv.iface = NULL;
	priv.err = 0;
	priv.iface_idx = iface_idx;

	if (nl_recv_msg(&nlsock, RTM_NEWLINK, iface_cb, &priv))
		return NULL;

	if (priv.err) {
		nlr_iface_free(priv.iface);
		return NULL;
	}

	if (err)
		*err = 0;

	return priv.iface;
}

static int iface_set_flags(int iface_idx, int flags)
{
	char buf[128], *p;
	struct ifinfomsg ifi;

	memset(buf, 0, sizeof(buf));

	p = nlmsg_put_hdr(buf, RTM_NEWLINK, NLM_F_ACK);

	memset(&ifi, 0, sizeof(ifi));

	ifi.ifi_index = iface_idx;
	ifi.ifi_flags = flags;
	ifi.ifi_change = 0xffffffff;

	p = add_hdr(p, &ifi, sizeof(ifi));

	if (nl_send_msg(&nlsock, buf, p - buf))
		return -1;

	return nl_wait_ack(&nlsock);
}

int nlr_set_iface(int iface_idx, int up)
{
	return iface_set_flags(iface_idx, up ? IFF_UP : 0);
}

static int manage_addr(int iface_idx, in_addr_t addr, int prefix_len,
		       int type, int flags)
{
	char buf[128], *p;
	struct ifaddrmsg ifa;

	memset(buf, 0, sizeof(buf));

	p = nlmsg_put_hdr(buf, type, flags | NLM_F_ACK);

	memset(&ifa, 0, sizeof(ifa));
	ifa.ifa_family = AF_INET;
	ifa.ifa_prefixlen = prefix_len;
	ifa.ifa_scope = RT_SCOPE_UNIVERSE;
	ifa.ifa_index = iface_idx;

	p = add_hdr(p, &ifa, sizeof(ifa));

	p = add_rta(p, IFA_LOCAL, 4, &addr);
	/* p = add_rta(p, IFA_ADDRESS, 4, &addr); */

	if (nl_send_msg(&nlsock, buf, p - buf))
		return -1;

	return nl_wait_ack(&nlsock);
}

int nlr_add_addr(int iface_idx, in_addr_t addr, int prefix_len)
{
	return manage_addr(iface_idx, addr, prefix_len, RTM_NEWADDR,
			   NLM_F_CREATE | NLM_F_EXCL);
}

int nlr_del_addr(int iface_idx, in_addr_t addr, int prefix_len)
{
	return manage_addr(iface_idx, addr, prefix_len, RTM_DELADDR, 0);
}

struct addr_cb_priv {
	struct nlr_addr *addr;
	int iface_idx;
	int err;
};

static int addr_cb(struct nlmsghdr *nlhdr, void *_priv)
{
	struct addr_cb_priv *priv = (struct addr_cb_priv *)_priv;
	struct ifaddrmsg *ifa;
	struct rtattr *rta;
	int n;
	struct in_addr in;
	struct nlr_addr *addr;

	if (!nlhdr || priv->err)
		return 0;

	ifa = NLMSG_DATA(nlhdr);

	if (ifa->ifa_family != AF_INET)
		return 0;

	if (priv->iface_idx >= 0 && priv->iface_idx != ifa->ifa_index)
		return 0;

	for (rta = IFA_RTA(ifa), n = RTM_PAYLOAD(nlhdr);
	     RTA_OK(rta, n); rta = RTA_NEXT(rta, n)) {
		if (rta->rta_type == IFA_ADDRESS) {
			in.s_addr = *(in_addr_t *)RTA_DATA(rta);
			DEBUG("%d %s/%d", ifa->ifa_index,
			       inet_ntoa(in), ifa->ifa_prefixlen);

			addr = malloc(sizeof(struct nlr_addr));
			if (!addr) {
				ERRNO("failed to alloc nlr_addr");
				priv->err = 1;
				return 0;
			}

			addr->iface_idx = ifa->ifa_index;
			addr->addr = *(in_addr_t *)RTA_DATA(rta);
			addr->prefix_len = ifa->ifa_prefixlen;

			addr->pnext = priv->addr;
			priv->addr = addr;
		}
	}

	return 0;
}

void nlr_addr_free(struct nlr_addr *addr)
{
	struct nlr_addr *p;

	while (addr) {
		p = addr->pnext;
		free(addr);
		addr = p;
	}
}

struct nlr_addr *nlr_get_addr(int iface_idx, int *err)
{
	char buf[64], *p;
	struct ifaddrmsg ifa;
	struct addr_cb_priv priv;

	if (err)
		*err = -1;

	memset(buf, 0, sizeof(buf));

	p = nlmsg_put_hdr(buf, RTM_GETADDR, NLM_F_DUMP);

	memset(&ifa, 0, sizeof(ifa));
	ifa.ifa_family = AF_INET;
	/* ifa.ifa_index = iface_idx; */

	p = add_hdr(p, &ifa, sizeof(ifa));

	if (nl_send_msg(&nlsock, buf, p - buf))
		return NULL;

	priv.addr = NULL;
	priv.err = 0;
	priv.iface_idx = iface_idx;

	if (nl_recv_msg(&nlsock, RTM_NEWADDR, addr_cb, &priv))
		return NULL;

	if (priv.err) {
		nlr_addr_free(priv.addr);
		return NULL;
	}

	if (err)
		*err = 0;

	return priv.addr;
}

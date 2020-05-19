#ifndef _NL80211_H
#define _NL80211_H

int nl80211_init(void);
void nl80211_fin(void);

struct nl80211_iface_stat {
	long tx_bytes, tx_packets;
	long rx_bytes, rx_packets;
};

struct nl80211_iface {
	int idx;
	int wiphy;
	char *name;
	char *ssid;
	int managed; /* 1 -- managed, 0 -- AP */
	unsigned char mac[6];
	int tx_power; /* dBm */
	int channel; /* 1-14 */
	int freq; /* MHz */
	struct nl80211_iface *pnext;
};

void nl80211_iface_free(struct nl80211_iface *iface);

struct nl80211_iface *nl80211_iface(int iface_idx, int *err);

#endif



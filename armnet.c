#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <linux/sockios.h>
/* #include <linux/if.h> */
/* #include <linux/if_arp.h> */
#include <linux/if_tun.h>

static char *brif_name = "br0";
static char *hostif_name = "eth0";

static void write_proc (char *name, char *val);

void
dump (void *buf, int n)
{
	int i;
	int j;
	int c;

	for (i = 0; i < n; i += 16) {
		printf ("%04x: ", i);
		for (j = 0; j < 16; j++) {
			if (i+j < n)
				printf ("%02x ", ((unsigned char *)buf)[i+j]);
			else
				printf ("   ");
		}
		printf ("  ");
		for (j = 0; j < 16; j++) {
			c = ((unsigned char *)buf)[i+j] & 0x7f;
			if (i+j >= n)
				putchar (' ');
			else if (c < ' ' || c == 0x7f)
				putchar ('.');
			else
				putchar (c);
		}
		printf ("\n");

	}
}

static void armnet_setup_bridge (void);

/* 
 * get a mac address assocaited with this machine, even if it is not
 * the primary network adapter
 */
static int
get_hwaddr (unsigned char *hwaddr)
{
	int idx;
	int sock;
	struct ifreq ifr;
	char ifname[IFNAMSIZ+1];

	if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
		return (-1);

	for (idx = 1; ; idx++) {
		memset (&ifr, 0, sizeof ifr);
		ifr.ifr_ifindex = idx;
		if (ioctl (sock, SIOCGIFNAME, &ifr) < 0)
			break;
		strncpy (ifname, ifr.ifr_name, IFNAMSIZ);
		ifname[IFNAMSIZ] = 0;

		memset (&ifr, 0, sizeof ifr);
		strncpy (ifr.ifr_name, ifname, IFNAMSIZ);
		if (ioctl (sock, SIOCGIFHWADDR, &ifr) < 0)
			break;
		if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)
			continue;
		memcpy (hwaddr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
		close (sock);
		return (0);
	}

	close (sock);
	return (-1);
}

int tapfd;
char tapif_name[IFNAMSIZ];

void
armnet_init (void)
{
	unsigned char hwaddr[6];
	struct ifreq ifr;
	int i;
	char cmd[1000];
	char filename[1000];

	armnet_setup_bridge ();

	if (get_hwaddr (hwaddr) < 0) {
		fprintf (stderr, "can't get host hwaddr\n");
		exit (1);
	}

	/*
	 * use host hwaddr, but make it different by toggling the high bit
	 * and setting the local bit
	 */
	hwaddr[0] ^= 0x80;
	hwaddr[0] |= 2;

	printf ("using hwaddr ");
	for (i = 0; i < 6; i++)
		printf ("%s%02x", i ? ":" : "", hwaddr[i]);
	printf ("\n");

	if ((tapfd = open ("/dev/net/tun", O_RDWR)) < 0) {
		perror ("open /dev/net/tun");
		exit (1);
	}

	memset (&ifr, 0, sizeof ifr);
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	if (ioctl(tapfd, TUNSETIFF, &ifr) < 0) {
		perror ("TUNSETIFF");
		exit(1);
	}

	if (ioctl (tapfd, TUNGETIFF, &ifr) < 0) {
		perror ("TUNGETIFF");
		exit (1);
	}
	strcpy (tapif_name, ifr.ifr_name);

	sprintf (cmd, "ifconfig %s 0.0.0.0 promisc up", tapif_name);
	system (cmd);

	sprintf (cmd, "brctl addif %s %s", brif_name, tapif_name);
	system (cmd);

	sprintf (filename, "/proc/sys/net/ipv4/conf/%s/proxy_arp", tapif_name);
	write_proc (filename, "1");
	sprintf (filename, "/proc/sys/net/ipv4/conf/%s/forwarding", tapif_name);
	write_proc (filename, "1");

	fcntl (tapfd, F_SETFL, O_NONBLOCK);
}

static void
write_proc (char *name, char *val)
{
	FILE *f;

	if ((f = fopen (name, "w")) == NULL) {
		fprintf (stderr, "can't write to %s\n", name);
		exit (1);
	}
	fprintf (f, "%s\n", val);
	fclose (f);
}

int
armnet_get_host_params (char *ifname,
			struct in_addr *addr,
			struct in_addr *netmask,
			struct in_addr *broadcast,
			struct in_addr *gateway)
{
	int sock = 0;
	struct sockaddr_in *sin;
	struct ifreq ifr;
	FILE *inf;
	int saw_gw;
	char buf[1000], gwbuf[1000];

	if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
		return (-1);

	strncpy (ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_addr.sa_family = AF_INET;
	if (ioctl (sock, SIOCGIFADDR, &ifr) < 0)
		goto bad;
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	*addr = sin->sin_addr;

	strncpy (ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_addr.sa_family = AF_INET;
	if (ioctl (sock, SIOCGIFNETMASK, &ifr) < 0)
		goto bad;
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	*netmask = sin->sin_addr;


	strncpy (ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_addr.sa_family = AF_INET;
	if (ioctl (sock, SIOCGIFBRDADDR, &ifr) < 0)
		goto bad;
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	*broadcast = sin->sin_addr;

	close (sock);

	if ((inf = popen ("netstat -rn", "r")) == NULL)
		goto bad;

	saw_gw = 0;
	while (fgets (buf, sizeof buf, inf) != NULL) {
		if (sscanf (buf, "0.0.0.0 %s", gwbuf) == 1) {
			gateway->s_addr = inet_addr (gwbuf);
			saw_gw = 1;
			break;
		}
	}
	pclose (inf);

	if (saw_gw == 0)
		goto bad;

	return (0);

bad:
	close (sock);
	return (-1);
}

void
armnet_setup_bridge (void)
{
	char filename[1000];
	struct in_addr addr, netmask, broadcast, gateway;
	char cmd[1000];
	char *outp;

	if (armnet_get_host_params (hostif_name,
				    &addr,
				    &netmask,
				    &broadcast,
				    &gateway) < 0) {
		return;
	}

	printf ("host %s ", inet_ntoa (addr));
	printf ("netmask %s ", inet_ntoa (netmask));
	printf ("broadcast %s\n", inet_ntoa (broadcast));
	printf ("gateway %s\n", inet_ntoa (gateway));

	/* brctl addbr br0 */
	sprintf (cmd, "brctl addbr %s", brif_name);
	system (cmd);

	sprintf (cmd, "brctl addif %s %s", brif_name, hostif_name);
	system (cmd);

	/* echo 1 > /proc/sys/net/ipv4/conf/eth0/forwarding */
	sprintf (filename, "/proc/sys/net/ipv4/conf/%s/forwarding",
		 hostif_name);
	write_proc (filename, "1");

	/* echo 1 > /proc/sys/net/ipv4/conf/br0/forwarding */
	sprintf (filename, "/proc/sys/net/ipv4/conf/%s/forwarding",
		 brif_name);
	write_proc (filename, "1");

	sprintf (cmd, "ifconfig %s promisc 0.0.0.0 promisc",
		 hostif_name);
	system (cmd);

	/* ifconfig br0 ADDR netmask MASK broadcast BRD */
	outp = cmd;
	outp += sprintf (outp, "ifconfig %s %s ", brif_name, 
			 inet_ntoa (addr));
	outp += sprintf (outp, "netmask %s ", inet_ntoa (netmask));
	outp += sprintf (outp, "broadcast %s ", inet_ntoa (broadcast));
	system (cmd);

	sprintf (cmd, "route add default gw %s", inet_ntoa (gateway));
	system (cmd);
	
	sprintf (cmd, "brctl stp %s off", brif_name);
	system (cmd);

	sprintf (cmd, "brctl setfd %s 0", brif_name);
	system (cmd);
		
	sprintf (cmd, "brctl sethello %s 1", brif_name);
	system (cmd);
		
	write_proc ("/proc/sys/net/ipv4/ip_nonlocal_bind", "1");
}

void
armnet_soak (void)
{
	char buf[10000];
	int len;
	fd_set rset;
	struct timeval tv;

	while (1) {
		if (0) {
			FD_ZERO (&rset);
			FD_SET (tapfd, &rset);
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			if (select (tapfd + 1, &rset, NULL, NULL, &tv) < 0) {
				perror ("select");
				exit (1);
			}
			if (FD_ISSET (tapfd, &rset) == 0)
				break;
		}
		len = read (tapfd, buf, sizeof buf);
		if (len < 0) {
			if (errno == EAGAIN)
				break;
			perror ("tap read");
			exit (1);
		}

		printf ("got %d\n", len);
		dump (buf, len);
	}
}

int
main (int argc, char **argv)
{
	armnet_init ();

	while (1) {
		armnet_soak ();
		usleep (200 * 1000);
	}

	return (0);
}


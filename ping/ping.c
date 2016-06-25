/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Muuss.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef lint
char copyright[] =
"@(#) Copyright (c) 1989 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifdef lint
/*static char sccsid[] = "from: @(#)ping.c      5.9 (Berkeley) 5/12/91";*/
static char rcsid[] = "$Id: ping.c,v 1.1 1994/05/23 09:07:13 rzsfl Exp rzsfl $";
#endif /* not lint */

/*
 *                      P I N G . C
 *
 * Using the InterNet Control Message Protocol (ICMP) "ECHO" facility,
 * measure round-trip-delays and packet loss across network paths.
 *
 * Author -
 *      Mike Muuss
 *      U. S. Army Ballistic Research Laboratory
 *      December, 1983
 *
 * Status -
 *      Public Domain.  Distribution Unlimited.
 * Bugs -
 *      More statistics could always be gathered.
 *      This program has to run SUID to ROOT to access the ICMP socket.
 */

#include <sys/file.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFDATALEN      (64 - 8)                /* default data length */
#define MAXIPLEN        60
#define MAXICMPLEN      76
#define MAXPACKET       (65536 - 60 - 8)        /* max packet size */
#define MAXWAIT         10                      /* max seconds to wait for response */
#define NROUTES         9                       /* number of record route slots */

/**
 *  MAX_DUP_CHK is the number of bits in received table,
 *  i.e. the maximum number of received sequence numbers we can keep track of.
 *  Change 128 to 8192 for complete accuracy...
 */
#define MAX_DUP_CHK     (8 * 128)
int mx_dup_ck = MAX_DUP_CHK;
char rcvd_tbl[MAX_DUP_CHK / 8];

#define A(bit)          rcvd_tbl[(bit)>>3]      /* identify byte in array */
#define B(bit)          (1 << ((bit) & 0x07))   /* identify bit in byte */
#define SET(bit)        (A(bit) |= B(bit))
#define CLR(bit)        (A(bit) &= (~B(bit)))
#define TST(bit)        (A(bit) & B(bit))

/* various options */
int options;
#define F_FLOOD         0x001
#define F_INTERVAL      0x002
#define F_NUMERIC       0x004
#define F_PINGFILLED    0x008
#define F_QUIET         0x010
#define F_RROUTE        0x020
#define F_SO_DEBUG      0x040
#define F_SO_DONTROUTE  0x080
#define F_VERBOSE       0x100

char BSPACE = '\b';             /* characters written for flood */
char DOT = '.';

struct sockaddr whereto;        /* who to ping */
char *hostname;                 /* destination */

int sock;                       /* socket file descriptor */
int ident;                      /* process id to identify our packets */

/**
 *  input buffer
 *
 *      ip header    (20 octets)
 *      ip options   (0-40 octets (ipoptlen))
 *      icmp header  (8 octets)
 *      timeval      (8 or 12 octets, only if timing==1)
 *      other data
 */
static u_int8_t inpack[IP_MAXPACKET];
static int ipoptlen;

#define INPACK_IP       ((struct ip *) inpack)
#define INPACK_OPTS     (inpack+sizeof(struct ip))
#define INPACK_ICMP     ((struct icmp *) (inpack+sizeof(struct ip)+ipoptlen))
#define INPACK_PAYLOAD  (INPACK_ICMP->icmp_data)

/**
 *  output buffer
 *
 *      icmp header  (8 octets)
 *      timeval      (8 or 12 octets, only if timing==1)
 *      other data
 *
 * datalen is the length of the other data plus the timeval.
 */
static u_int8_t outpack[IP_MAXPACKET];
static int datalen = DEFDATALEN;

#define OUTPACK_ICMP    ((struct icmp *) outpack)
#define OUTPACK_PAYLOAD (OUTPACK_ICMP->icmp_data)

/* counters */
long npackets = 3;              /* max packets to transmit */
long nreceived;                 /* # of packets we got back */
long nrepeats;                  /* number of duplicates */
long ntransmitted;              /* sequence # for outbound packets = #sent */
int interval = 1;               /* interval between packets */

/* timing */
int timing;                     /* flag to do timing */
long tmin = LONG_MAX;           /* minimum round trip time */
long tmax;                      /* maximum round trip time */
u_long tsum;                    /* sum of all times, for doing average */


static void catcher(), finish(), usage(), pinger();
static void fill(char*, char *) ;
static void tvsub(struct timeval*, struct timeval *) ;
static int in_cksum(u_short*, int);
static char* pr_addr();
static void pr_pack(int, struct sockaddr_in *) ;
static void pr_icmph(struct icmp *) ;
static void pr_retip(struct ip *) ;
static void pr_iph(struct ip *) ;

int main(int argc, char **argv) {
	extern int errno, optind;
	extern char *optarg;
	struct timeval timeout;
	struct hostent *hp;
	struct sockaddr_in *to;
	struct protoent *proto;

	int ch, fdmask, hold, packlen, preload;
	u_char *datap;
	char *target, hnamebuf[MAXHOSTNAMELEN];

	char rspace[3 + 4 * NROUTES + 1];       // record route space

	preload = 0;
	datap = &outpack[8 + sizeof(struct timeval)];

	while ((ch = getopt(argc, argv, "I:LRc:dfh:i:l:np:qrs:t:v")) != EOF)
    switch(ch) {

		case 'c':
			npackets = atoi(optarg);
			if (npackets <= 0) {
				fprintf(stderr, "ping: bad number of packets to transmit.\n");
				exit(1);
			}
			break;

		case 'd':
			options |= F_SO_DEBUG;
			break;

		case 'f':
			if (getuid()) {
				fprintf(stderr, "ping: %s\n", strerror(EPERM));
				exit(1);
			}
			options |= F_FLOOD;
			setbuf(stdout, (char *) NULL);
			break;

		case 'i': // wait between sending packets
			interval = atoi(optarg);
			if (interval <= 0) {
				fprintf(stderr, "ping: bad timing interval.\n");
				exit(1);
			}
			options |= F_INTERVAL;
			break;

		case 'l':
			preload = atoi(optarg);
			if (preload < 0) {
				fprintf(stderr, "ping: bad preload value.\n");
				exit(1);
			}
			break;

		case 'n':
			options |= F_NUMERIC;
			break;

		case 'p': // fill buffer with user pattern
			options |= F_PINGFILLED;
			fill((char *) datap, optarg);
			break;

		case 'q':
			options |= F_QUIET;
			break;

		case 'R':
			options |= F_RROUTE;
			break;

		case 'r':
			options |= F_SO_DONTROUTE;
			break;

		case 's': /* size of packet to send */
			datalen = atoi(optarg);
			if (datalen > MAXPACKET) {
				fprintf(stderr, "ping: packet size too large.\n");
				exit(1);
			}
			if (datalen <= 0) {
				fprintf(stderr, "ping: illegal packet size.\n");
				exit(1);
			}
			break;

		case 'v':
			options |= F_VERBOSE;
			break;

		default:
			usage();
	}

	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usage();
	}

	target = *argv;

	bzero((char *) &whereto, sizeof(struct sockaddr));

	to = (struct sockaddr_in *) &whereto;
	to->sin_family = AF_INET;
	to->sin_addr.s_addr = inet_addr(target);

	if (to->sin_addr.s_addr != (u_int)-1) {
		hostname = target;
	} else {
		hp = gethostbyname(target);
		if ( ! hp) {
			fprintf(stderr, "ping: unknown host %s\n", target);
			exit(1);
		}
		to->sin_family = hp->h_addrtype;
		bcopy(hp->h_addr, (caddr_t)&to->sin_addr, hp->h_length);
		strncpy(hnamebuf, hp->h_name, sizeof(hnamebuf) - 1);
		hostname = hnamebuf;
	}

	if ((options & F_FLOOD) && (options & F_INTERVAL)) {
		fprintf(stderr, "ping: -f and -i incompatible options.\n");
		exit(1);
	}

	// If there's space for the time, we can time the transfer
	if (datalen >= sizeof(struct timeval)) {
		timing = 1;
	}

	// If an explicit pattern wasn't set, use a default fill
	if ( ! (options & F_PINGFILLED)) {
		u_int8_t *ptr = OUTPACK_PAYLOAD + (timing ? sizeof(struct timeval) : 0);
		for (int i = 8; i < datalen; ++i) {
			ptr[i] = i;
		}
	}

	ident = getpid() & 0xFFFF;

	if ( ! (proto = getprotobyname("icmp"))) {
		fprintf(stderr, "ping: unknown protocol icmp.\n");
		exit(1);
	}
	if ((sock = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0) {
		perror("ping: socket");
		exit(1);
	}

	hold = 1;
	if (options & F_SO_DEBUG) {
		setsockopt(sock, SOL_SOCKET, SO_DEBUG, (char *) &hold, sizeof(hold));
	}
	if (options & F_SO_DONTROUTE) {
		setsockopt(sock, SOL_SOCKET, SO_DONTROUTE, (char *) &hold, sizeof(hold));
	}

	// record route option
	if (options & F_RROUTE) {
		rspace[IPOPT_OPTVAL] = IPOPT_RR;
		rspace[IPOPT_OLEN] = sizeof(rspace)-1;
		rspace[IPOPT_OFFSET] = IPOPT_MINOFF;
		if (setsockopt(sock, IPPROTO_IP, IP_OPTIONS, rspace, sizeof(rspace)) < 0) {
			perror("ping: record route");
			exit(1);
		}
	}

	// When pinging the broadcast address, you can get a lot of answers.
	// Doing something so evil is useful if you are trying to stress the ethernet,
	// or just want to fill the arp cache to get some stuff for /etc/ethers.

	hold = 48 * 1024;
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &hold, sizeof(hold));

	if (to->sin_family == AF_INET) {
		printf("PING %s (%s): %d data bytes\n", hostname,
				inet_ntoa(*(struct in_addr *) &to->sin_addr.s_addr), datalen);
	} else {
		printf("PING %s: %d data bytes\n", hostname, datalen);
	}

	signal(SIGINT, finish);
	signal(SIGALRM, catcher);

	while (preload--) { // fire off them quickies
		pinger();
	}
	if ((options & F_FLOOD) == 0) {
		catcher(); // start things going
	}
	for (;;) {
		struct sockaddr_in from;
		unsigned int fromlen;

		if (options & F_FLOOD) {
			pinger();
			timeout.tv_sec = 0;
			timeout.tv_usec = 10000;
			fdmask = 1 << sock;
			if (select(sock + 1, (fd_set *) &fdmask, (fd_set *) NULL, (fd_set *) NULL, &timeout) < 1) {
				continue;
			}
		}

		fromlen = sizeof(from);
		packlen = recvfrom(sock, inpack, sizeof(inpack), 0, (struct sockaddr *) &from, &fromlen);

		if (packlen < 0) {
			if (errno != EINTR) {
				perror("ping: recvfrom");
			}
			continue;
		}

		pr_pack(packlen, &from);

		if (npackets && nreceived >= npackets) {
			break;
		}
	}
	finish();
	/* NOTREACHED */
}

/**
 *  catcher --
 *      This routine causes another PING to be transmitted,
 *      and then schedules another SIGALRM for 1 second from now.
 *
 *  bug --
 *      Our sense of time will slowly skew
 *      (i.e., packets will not be launched exactly at 1-second intervals).
 *      This does not affect the quality of the delay and loss statistics.
 */
void catcher() {
	int waittime;

	pinger();
	signal(SIGALRM, catcher);
	if ( ! npackets || ntransmitted < npackets) {
		alarm((u_int) interval);
	} else {
		if (nreceived) {
			waittime = 2 * tmax / 1000;
			if ( ! waittime) {
				waittime = 1;
			}
		} else {
			waittime = MAXWAIT;
		}
		signal(SIGALRM, finish);
		alarm((u_int) waittime);
	}
}

/**
 *  pinger --
 *      Compose and transmit an ICMP ECHO REQUEST packet.
 *      The IP packet will be added on by the kernel.
 *      The ID field is our UNIX process ID,
 *      and the sequence number is an ascending integer.
 *      The first 8 bytes of the data portion are used
 *      to hold a UNIX "timeval" struct in VAX byte-order,
 *      to compute the round-trip time.
 */
void pinger() {

	struct icmp *icp = (struct icmp *) outpack;
	icp->icmp_type = ICMP_ECHO;
	icp->icmp_code = 0;
	icp->icmp_cksum = 0;
	icp->icmp_seq = ntransmitted++;
	icp->icmp_id = ident; // ID

	CLR(icp->icmp_seq % mx_dup_ck);

	if (timing) {
		gettimeofday((struct timeval *) &outpack[8], (struct timezone *) NULL);
	}
	int cc = datalen + 8; // skips ICMP portion

	// compute ICMP checksum here
	icp->icmp_cksum = in_cksum((u_short *) icp, cc);

	int i = sendto(sock, (char *) outpack, cc, 0, &whereto, sizeof(struct sockaddr));

	if (i < 0 || i != cc) {
		if (i < 0) {
			perror("ping: sendto");
		}
		printf("ping: wrote %s %d chars, ret=%d\n", hostname, cc, i);
	}
	if ( ! (options & F_QUIET) && (options & F_FLOOD)) {
		write(STDOUT_FILENO, &DOT, 1);
	}
}

/**
 *  pr_pack --
 *      Print out the packet, if it came from us.
 *      This logic is necessary because ALL readers of the ICMP socket
 *      get a copy of ALL ICMP packets which arrive ('tis only fair).
 *      This permits multiple copies of this program to be run
 *      without having intermingled output (or statistics!).
 */
void pr_pack(int packlen, struct sockaddr_in *from) {
	struct timeval tv;

	gettimeofday(&tv, (struct timezone *) NULL);

	/* Check the IP header */

	struct ip *ip = INPACK_IP;
	int hlen = ip->ip_hl << 2;

	if (hlen < (int) sizeof(struct ip)) {
		if (options & F_VERBOSE) {
			fprintf(stderr, "ping: packet too short (%d bytes) from %s\n",
					packlen, inet_ntoa(from->sin_addr));
		}
		return;
	}
	if (hlen > packlen) {
		if (options & F_VERBOSE) {
			fprintf(stderr, "ping: partial packet (%d/%d bytes) from %s\n",
					hlen, packlen, inet_ntoa(from->sin_addr));
		}
		return;
	}

	ipoptlen = hlen - sizeof(struct ip);
	packlen -= hlen;
	struct icmp *icp = INPACK_ICMP;

	/* ICMP_MINLEN is the size of the icmp header (8 octets) */
	if (packlen < ICMP_MINLEN + datalen) {
		if (options & F_VERBOSE) {
			fprintf(stderr, "ping: packet too short (%d bytes) from %s\n",
					packlen, inet_ntoa(from->sin_addr));
		}
		return;
	}

	// Now the ICMP part

	if (icp->icmp_type == ICMP_ECHOREPLY) {
		int dupflag;
		long triptime = 0;

		if (icp->icmp_id != ident) {
			return; // 'Twas not our ECHO
		}

		++nreceived;

		if (timing) {
			struct timeval *tp = (struct timeval *) icp->icmp_data;
			tvsub(&tv, tp);
			triptime = tv.tv_sec * 10000 + (tv.tv_usec / 100);
			tsum += triptime;
			if (triptime < tmin) {
				tmin = triptime;
			}
			if (triptime > tmax) {
				tmax = triptime;
			}
		}
		if (TST(icp->icmp_seq % mx_dup_ck)) {
			++nrepeats;
			--nreceived;
			dupflag = 1;
		} else {
			SET(icp->icmp_seq % mx_dup_ck);
			dupflag = 0;
		}
		if (options & F_QUIET) {
			return;
		}

		if (options & F_FLOOD) {
			write(STDOUT_FILENO, &BSPACE, 1);
		} else {
			// print the ECHOREPLY

			printf("%d bytes from %s: icmp_seq=%u", packlen,
					inet_ntoa(*(struct in_addr *) &from->sin_addr.s_addr), icp->icmp_seq);

			printf(" ttl=%d", ip->ip_ttl);

			u_char *cp = INPACK_PAYLOAD;
			u_char *dp = OUTPACK_PAYLOAD;
			int len = datalen;

			if (timing) {
				printf(" time=%ld.%ld ms", triptime / 10, triptime % 10);
				cp  += sizeof(struct timeval);
				dp  += sizeof(struct timeval);
				len -= sizeof(struct timeval);
			}

			if (dupflag) {
				printf(" (DUP!)");
			}

			// check the data
			for (int i = 0; i < len; i++) {
				if (cp[i] != dp[i]) {
					printf("\n" "wrong data byte #%d should be 0x%x but was 0x%x", i, dp[i], cp[i]);
					for (i = 0; i < len; i++) {
						if ((i % 32) == 8) {
							printf("\n\t");
						}
						printf("%x ", cp[i]);
					}
					break;
				}
			}
		}
	} else {
		// We've got something other than an ECHOREPLY
		if ( ! (options & F_VERBOSE)) {
			return;
		}
		printf("%d bytes from %s: ", packlen, pr_addr(from->sin_addr.s_addr));
		pr_icmph(icp);
	}

	if ( ! (options & F_FLOOD)) {
		putchar('\n');
		fflush(stdout);
	}
}

/**
 * in_cksum --
 *     Checksum routine for Internet Protocol family headers (C Version)
 */
int in_cksum(u_short *addr, int len) {
	int nleft = len;
	u_short *w = addr;
	int sum = 0;
	u_short answer = 0;

	// Our algorithm is simple, using a 32 bit accumulator (sum),
	// we add sequential 16 bit words to it, and at the end,
	// fold back all the carry bits from the top 16 bits into the lower 16 bits.

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}

	// mop up an odd byte, if necessary

	if (nleft == 1) {
		*(u_char *) (&answer) = *(u_char *) w;
		sum += answer;
	}

	// add back carry outs from top 16 bits to low 16 bits

	sum = (sum >> 16) + (sum & 0xffff); // add hi 16 to low 16
	sum += (sum >> 16);                 // add carry
	answer = ~sum;                      // truncate to 16 bits
	return (answer);
}

/**
 *  tvsub --
 *      Subtract 2 timeval structs:  out = out - in.
 *      Out is assumed to be >= in.
 */
void tvsub(struct timeval *out, struct timeval *in) {
	if ((out->tv_usec -= in->tv_usec) < 0) {
		--out->tv_sec;
		out->tv_usec += 1000000;
	}
	out->tv_sec -= in->tv_sec;
}

/**
 *  finish -- Print out statistics, and give up.
 */
static void finish() {
	signal(SIGINT, SIG_IGN);

	putchar('\n');
	fflush(stdout);

	printf("--- %s ping statistics ---\n", hostname);
	printf("%ld packets transmitted, ", ntransmitted);
	printf("%ld packets received, ", nreceived);

	if (nrepeats) {
		printf("+%ld duplicates, ", nrepeats);
	}
	if (ntransmitted) {
		if (nreceived > ntransmitted) {
			printf("-- somebody's printing up packets!");
		} else {
	 		printf("%d%% packet loss", (int) (((ntransmitted - nreceived) * 100) / ntransmitted));
		}
	}
	putchar('\n');
	if (nreceived && timing) {
		printf("round-trip min/avg/max = %ld.%ld/%lu.%ld/%ld.%ld ms\n",
				tmin / 10, tmin % 10, (tsum / (nreceived + nrepeats)) / 10,
				(tsum / (nreceived + nrepeats)) % 10, tmax / 10, tmax % 10);
	}

	exit(0);
}

/*
 * pr_icmph --
 *      Print a descriptive string about an ICMP header.
 */
void pr_icmph(struct icmp *icp) {
	switch (icp->icmp_type) {

	case ICMP_ECHOREPLY:
		printf("Echo Reply\n");
		/* XXX ID + Seq + Data */
		break;

	case ICMP_DEST_UNREACH:
		switch (icp->icmp_code) {
			case ICMP_NET_UNREACH:
				printf("Destination Net Unreachable\n");
				break;
			case ICMP_HOST_UNREACH:
				printf("Destination Host Unreachable\n");
				break;
			case ICMP_PROT_UNREACH:
				printf("Destination Protocol Unreachable\n");
				break;
			case ICMP_PORT_UNREACH:
				printf("Destination Port Unreachable\n");
				break;
			case ICMP_FRAG_NEEDED:
				printf("frag needed and DF set\n");
				break;
			case ICMP_SR_FAILED:
				printf("Source Route Failed\n");
				break;
			default:
				printf("Dest Unreachable, Bad Code: %d\n", icp->icmp_code);
				break;
		}
		/* Print returned IP header information */
		pr_retip((struct ip *) icp->icmp_data);
		break;

	case ICMP_SOURCE_QUENCH:
		printf("Source Quench\n");
		pr_retip((struct ip *) icp->icmp_data);
		break;

	case ICMP_REDIRECT:
		switch (icp->icmp_code) {
			case ICMP_REDIR_NET:
				printf("Redirect Network");
				break;
			case ICMP_REDIR_HOST:
				printf("Redirect Host");
				break;
			case ICMP_REDIR_NETTOS:
				printf("Redirect Type of Service and Network");
				break;
			case ICMP_REDIR_HOSTTOS:
				printf("Redirect Type of Service and Host");
				break;
			default:
				printf("Redirect, Bad Code: %d", icp->icmp_code);
				break;
		}
		printf("(New addr: %s)\n", inet_ntoa(icp->icmp_gwaddr));
		pr_retip((struct ip *) icp->icmp_data);
		break;

	case ICMP_ECHO:
		printf("Echo Request\n");
		/* XXX ID + Seq + Data */
		break;

	case ICMP_TIME_EXCEEDED:
		switch (icp->icmp_code) {
			case ICMP_EXC_TTL:
				printf("Time to live exceeded\n");
				break;
			case ICMP_EXC_FRAGTIME:
				printf("Frag reassembly time exceeded\n");
				break;
			default:
				printf("Time exceeded, Bad Code: %d\n", icp->icmp_code);
				break;
		}
		pr_retip((struct ip *) icp->icmp_data);
		break;

	case ICMP_PARAMETERPROB:
		printf("Parameter problem: IP address = %s\n", inet_ntoa(icp->icmp_gwaddr));
		pr_retip((struct ip *) icp->icmp_data);
		break;

	case ICMP_TIMESTAMP:
		printf("Timestamp\n");
		/* XXX ID + Seq + 3 timestamps */
		break;

	case ICMP_TIMESTAMPREPLY:
		printf("Timestamp Reply\n");
		/* XXX ID + Seq + 3 timestamps */
		break;

	case ICMP_INFO_REQUEST:
		printf("Information Request\n");
		/* XXX ID + Seq */
		break;

	case ICMP_INFO_REPLY:
		printf("Information Reply\n");
		/* XXX ID + Seq */
		break;

	case ICMP_MASKREQ:
		printf("Address Mask Request\n");
		break;

	case ICMP_MASKREPLY:
		printf("Address Mask Reply\n");
		break;

	default:
		printf("Bad ICMP type: %d\n", icp->icmp_type);
	}
}

/**
 *  pr_iph -- Print an IP header with options.
 */
void pr_iph(struct ip *ip) {

	int hlen = ip->ip_hl << 2;
	u_char *cp = (u_char *) ip + 20; /* point to options */

	printf("Vr HL TOS  Len   ID Flg  off TTL Pro  cks      Src      Dst Data\n");
	printf(" %1x  %1x  %02x %04x %04x", ip->ip_v, ip->ip_hl, ip->ip_tos, ip->ip_len, ip->ip_id);
	printf("   %1x %04x", ((ip->ip_off) & 0xe000) >> 13, (ip->ip_off) & 0x1fff);
	printf("  %02x  %02x %04x", ip->ip_ttl, ip->ip_p, ip->ip_sum);
	printf(" %s ", inet_ntoa(ip->ip_src));
	printf(" %s ", inet_ntoa(ip->ip_dst));
	// dump any option bytes
	while (hlen-- > 20) {
		printf("%02x", *cp++);
	}
	putchar('\n');
}

/**
 *  pr_addr -- Return an ascii host address as a dotted quad and optionally with a hostname.
 */
char* pr_addr(u_long l) {

	if (l == 0) {
		return "0.0.0.0";
	}
	struct hostent *hp;
	static char buf[80];
	struct in_addr addr;
	addr.s_addr = l;
	if ((options & F_NUMERIC)) {
		hp = gethostbyaddr((char *) &l, 4, AF_INET);
		if (hp) {
			sprintf(buf, "%s (%s)", hp->h_name, inet_ntoa(addr));
			return buf;
		}
	}
	return inet_ntoa(addr);
}

/**
 *  pr_retip --
 *      Dump some info on a returned (via ICMP) IP packet.
 */
void pr_retip(struct ip *ip) {
	pr_iph(ip);

	int hlen = ip->ip_hl << 2;
	u_char *cp = (u_char *) ip + hlen;

	if (ip->ip_p == 6) {
		printf("TCP: from port %u, to port %u (decimal)\n",
			  (*cp * 256 + *(cp + 1)), (*(cp + 2) * 256 + *(cp + 3)));
	} else {
		if (ip->ip_p == 17) {
			printf("UDP: from port %u, to port %u (decimal)\n",
				  (*cp * 256 + *(cp + 1)), (*(cp + 2) * 256 + *(cp + 3)));
		}
	}
}

void fill(char *bp, char *pat) {

	for (char* cp = pat; *cp; cp++) {
		if ( ! isxdigit(*cp)) {
			fprintf(stderr,
					"ping: patterns must be specified as hex digits.\n");
			exit(1);
		}
	}

	int p[16];
	int ii = sscanf(pat, "%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x",
			&p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &p[6], &p[7],
			&p[8], &p[9], &p[10], &p[11], &p[12], &p[13], &p[14], &p[15]);

	if (ii > 0) {
		for (int kk = 0; kk <= MAXPACKET - (8 + ii); kk += ii) {
			for (int jj = 0; jj < ii; ++jj) {
				bp[jj + kk] = p[jj];
			}
		}
	}
	if ( ! (options & F_QUIET)) {
		printf("PATTERN: 0x");
		for (int jj = 0; jj < ii; ++jj) {
			printf("%02x", bp[jj] & 0xFF);
		}
		printf("\n");
	}
}

void usage() {
	fprintf(stderr,
	    "usage: ping [-Rdfnqrv] [-c count] [-i wait] [-l preload]" "\n\t"
		"[-p pattern] [-s packetsize] [-t ttl] [-I interface address] host\n" );
	exit(1);
}

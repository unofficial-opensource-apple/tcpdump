/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /cvs/root/tcpdump/tcpdump/print-ether.c,v 1.1.1.3 2003/03/17 18:42:16 rbraun Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcpdump-stdinc.h>

#include <stdio.h>
#include <pcap.h>

#include "interface.h"
#include "addrtoname.h"
#include "ethertype.h"

#include "ether.h"

const u_char *snapend;

static inline void
ether_hdr_print(register const u_char *bp, u_int length)
{
	register const struct ether_header *ep;

	ep = (const struct ether_header *)bp;
	if (qflag)
		(void)printf("%s %s %d: ",
			     etheraddr_string(ESRC(ep)),
			     etheraddr_string(EDST(ep)),
			     length);
	else
		(void)printf("%s %s %s %d: ",
			     etheraddr_string(ESRC(ep)),
			     etheraddr_string(EDST(ep)),
			     etherproto_string(ep->ether_type),
			     length);
}

void
ether_print(const u_char *p, u_int length, u_int caplen)
{
	struct ether_header *ep;
	u_short ether_type;
	u_short extracted_ethertype;

	if (caplen < ETHER_HDRLEN) {
		printf("[|ether]");
		return;
	}

	if (eflag)
		ether_hdr_print(p, length);

	length -= ETHER_HDRLEN;
	caplen -= ETHER_HDRLEN;
	ep = (struct ether_header *)p;
	p += ETHER_HDRLEN;

	ether_type = ntohs(ep->ether_type);

	/*
	 * Is it (gag) an 802.3 encapsulation?
	 */
	extracted_ethertype = 0;
	if (ether_type <= ETHERMTU) {
		/* Try to print the LLC-layer header & higher layers */
		if (llc_print(p, length, caplen, ESRC(ep), EDST(ep),
		    &extracted_ethertype) == 0) {
			/* ether_type not known, print raw packet */
			if (!eflag)
				ether_hdr_print((u_char *)ep, length + ETHER_HDRLEN);
			if (extracted_ethertype) {
				printf("(LLC %s) ",
			       etherproto_string(htons(extracted_ethertype)));
			}
			if (!xflag && !qflag)
				default_print(p, caplen);
		}
	} else if (ether_encap_print(ether_type, p, length, caplen,
	    &extracted_ethertype) == 0) {
		/* ether_type not known, print raw packet */
		if (!eflag)
			ether_hdr_print((u_char *)ep, length + ETHER_HDRLEN);
		if (!xflag && !qflag)
			default_print(p, caplen);
	}
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the ether header of the packet, 'h->ts' is the timestamp,
 * 'h->length' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
ether_if_print(const struct pcap_pkthdr *h, const u_char *p)
{
	ether_print(p, h->len, h->caplen);

	return (ETHER_HDRLEN);
}

/*
 * Prints the packet encapsulated in an Ethernet data segment
 * (or an equivalent encapsulation), given the Ethernet type code.
 *
 * Returns non-zero if it can do so, zero if the ethertype is unknown.
 *
 * The Ethernet type code is passed through a pointer; if it was
 * ETHERTYPE_8021Q, it gets updated to be the Ethernet type of
 * the 802.1Q payload, for the benefit of lower layers that might
 * want to know what it is.
 */

int
ether_encap_print(u_short ethertype, const u_char *p,
    u_int length, u_int caplen, u_short *extracted_ethertype)
{
 recurse:
	*extracted_ethertype = ethertype;

	switch (ethertype) {

	case ETHERTYPE_IP:
		ip_print(p, length);
		return (1);

#ifdef INET6
	case ETHERTYPE_IPV6:
		ip6_print(p, length);
		return (1);
#endif /*INET6*/

	case ETHERTYPE_ARP:
	case ETHERTYPE_REVARP:
		arp_print(p, length, caplen);
		return (1);

	case ETHERTYPE_DN:
		decnet_print(p, length, caplen);
		return (1);

	case ETHERTYPE_ATALK:
		if (vflag)
			fputs("et1 ", stdout);
		atalk_print(p, length);
		return (1);

	case ETHERTYPE_AARP:
		aarp_print(p, length);
		return (1);

	case ETHERTYPE_IPX:
		printf("(NOV-ETHII) ");
		ipx_print(p, length);
		return (1);

	case ETHERTYPE_8021Q:
		printf("802.1Q vlan#%d P%d%s ",
		       ntohs(*(u_int16_t *)p) & 0xfff,
		       ntohs(*(u_int16_t *)p) >> 13,
		       (ntohs(*(u_int16_t *)p) & 0x1000) ? " CFI" : "");
		ethertype = ntohs(*(u_int16_t *)(p + 2));
		p += 4;
		length -= 4;
		caplen -= 4;
		if (ethertype > ETHERMTU)
			goto recurse;

		*extracted_ethertype = 0;

		if (llc_print(p, length, caplen, p - 18, p - 12,
		    extracted_ethertype) == 0) {
			/* ether_type not known, print raw packet */
			if (!eflag)
				ether_hdr_print(p - 18, length + 4);
			if (*extracted_ethertype) {
				printf("(LLC %s) ",
			       etherproto_string(htons(*extracted_ethertype)));
			}
			if (!xflag && !qflag)
				default_print(p - 18, caplen + 4);
		}
		return (1);

	case ETHERTYPE_PPPOED:
	case ETHERTYPE_PPPOES:
		pppoe_print(p, length);
		return (1);

	case ETHERTYPE_PPP:
		printf("ppp");
		if (length) {
			printf(": ");
			ppp_print(p, length);
		}
		return (1);

        case ETHERTYPE_LOOPBACK:
                printf("loopback");
                return (1);

	case ETHERTYPE_MPLS:
	case ETHERTYPE_MPLS_MULTI:
		mpls_print(p, length);
		return (1);

	case ETHERTYPE_LAT:
	case ETHERTYPE_SCA:
	case ETHERTYPE_MOPRC:
	case ETHERTYPE_MOPDL:
		/* default_print for now */
	default:
		return (0);
	}
}

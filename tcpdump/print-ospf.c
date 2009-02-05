/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996, 1997
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
 *
 * OSPF support contributed by Jeffrey Honig (jch@mitchell.cit.cornell.edu)
 */

#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /cvs/root/tcpdump/tcpdump/print-ospf.c,v 1.1.1.3 2003/03/17 18:42:18 rbraun Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcpdump-stdinc.h>

#include <stdio.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"

#include "ospf.h"

#include "ip.h"

static struct tok ospf_option_values[] = {
	{ OSPF_OPTION_T,	"TOS" },
	{ OSPF_OPTION_E,	"External" },
	{ OSPF_OPTION_MC,	"Multicast" },
	{ OSPF_OPTION_NP,	"NSSA" },
	{ OSPF_OPTION_EA,	"Advertise External" },
	{ OSPF_OPTION_DC,	"Demand Circuit" },
	{ OSPF_OPTION_O,	"Opaque" },
	{ 0,			NULL }
};

static struct tok ospf_authtype_values[] = {
	{ OSPF_AUTH_NONE,	"none" },
	{ OSPF_AUTH_NONE,	"simple" },
	{ OSPF_AUTH_MD5,	"MD5" },
	{ 0,			NULL }
};

static struct tok ospf_rla_flag_values[] = {
	{ RLA_FLAG_B,		"ABR" },
	{ RLA_FLAG_E,		"ASBR" },
	{ RLA_FLAG_W1,		"Virtual" },
	{ RLA_FLAG_W2,		"W2" },
	{ 0,			NULL }
};

static struct tok type2str[] = {
	{ OSPF_TYPE_UMD,	"UMD" },
	{ OSPF_TYPE_HELLO,	"Hello" },
	{ OSPF_TYPE_DD,		"Database Description" },
	{ OSPF_TYPE_LS_REQ,	"LS-Request" },
	{ OSPF_TYPE_LS_UPDATE,	"LS-Update" },
	{ OSPF_TYPE_LS_ACK,	"LS-Ack" },
	{ 0,			NULL }
};

static struct tok lsa_values[] = {
	{ LS_TYPE_ROUTER,       "Router" },
	{ LS_TYPE_NETWORK,      "Network" },
	{ LS_TYPE_SUM_IP,       "Summary" },
	{ LS_TYPE_SUM_ABR,      "ASBR Summary" },
	{ LS_TYPE_ASE,          "External" },
	{ LS_TYPE_GROUP,        "Multicast Group" },
	{ LS_TYPE_NSSA,         "NSSA" },
	{ LS_TYPE_OPAQUE_LL,    "Link Local Opaque" },
	{ LS_TYPE_OPAQUE_AL,    "Area Local Opaque" },
	{ LS_TYPE_OPAQUE_DW,    "Domain Wide Opaque" },
	{ 0,			NULL }
};

static struct tok ospf_dd_flag_values[] = {
	{ OSPF_DB_INIT,	        "Init" },
	{ OSPF_DB_MORE,	        "More" },
	{ OSPF_DB_MASTER,	"Master" },
	{ 0,			NULL }
};

static char tstr[] = " [|ospf]";

#ifdef WIN32
#define inline __inline
#endif /* WIN32 */

static int ospf_print_lshdr(const struct lsa_hdr *);
static int ospf_print_lsa(const struct lsa *);
static int ospf_decode_v2(const struct ospfhdr *, const u_char *);

static int
ospf_print_lshdr(register const struct lsa_hdr *lshp) {

	TCHECK(lshp->ls_type);
	TCHECK(lshp->ls_options);

        printf("\n\t  %s LSA (%d), LSA-ID: %s, Advertising Router: %s, seq 0x%08x, age %us",
               tok2str(lsa_values,"unknown",lshp->ls_type),
               lshp->ls_type,
               ipaddr_string(&lshp->ls_stateid),
               ipaddr_string(&lshp->ls_router),
               EXTRACT_32BITS(&lshp->ls_seq),
               EXTRACT_16BITS(&lshp->ls_age));
        printf("\n\t    Options: %s", bittok2str(ospf_option_values,"none",lshp->ls_options));

return (0);
trunc:
	return (1);
}

/*
 * Print a single link state advertisement.  If truncated return 1, else 0.
 */
static int
ospf_print_lsa(register const struct lsa *lsap)
{
	register const u_char *ls_end;
	register const struct rlalink *rlp;
	register const struct tos_metric *tosp;
	register const struct in_addr *ap;
	register const struct aslametric *almp;
	register const struct mcla *mcp;
	register const u_int32_t *lp;
	register int j, k;

        printf("\n\t  %s LSA (%d), LSA-ID: %s, Advertising Router: %s, seq 0x%08x, age %us",
               tok2str(lsa_values,"unknown",lsap->ls_hdr.ls_type),
               lsap->ls_hdr.ls_type,
               ipaddr_string(&lsap->ls_hdr.ls_stateid),
               ipaddr_string(&lsap->ls_hdr.ls_router),
               EXTRACT_32BITS(&lsap->ls_hdr.ls_seq),
               EXTRACT_16BITS(&lsap->ls_hdr.ls_age));
        printf("\n\t    Options: %s", bittok2str(ospf_option_values,"none",lsap->ls_hdr.ls_options));

	TCHECK(lsap->ls_hdr.ls_length);
	ls_end = (u_char *)lsap + EXTRACT_16BITS(&lsap->ls_hdr.ls_length);
	switch (lsap->ls_hdr.ls_type) {

	case LS_TYPE_ROUTER:
		TCHECK(lsap->lsa_un.un_rla.rla_flags);
                printf("\n\t    Router LSA Options: %s", bittok2str(ospf_rla_flag_values,"unknown (%u)",lsap->lsa_un.un_rla.rla_flags));

		TCHECK(lsap->lsa_un.un_rla.rla_count);
		j = EXTRACT_16BITS(&lsap->lsa_un.un_rla.rla_count);
		TCHECK(lsap->lsa_un.un_rla.rla_link);
		rlp = lsap->lsa_un.un_rla.rla_link;
		while (j--) {
			TCHECK(*rlp);
			switch (rlp->link_type) {

			case RLA_TYPE_VIRTUAL:
				printf("\n\t      Virtual Link: Neighbor-Router-ID: %s, Interface-IP: %s",
				    ipaddr_string(&rlp->link_id),
				    ipaddr_string(&rlp->link_data)); 
                                break;

			case RLA_TYPE_ROUTER:
				printf("\n\t      Neighbor-Router-ID: %s, Interface-IP: %s",
				    ipaddr_string(&rlp->link_id),
				    ipaddr_string(&rlp->link_data));
				break;

			case RLA_TYPE_TRANSIT:
				printf("\n\t      Neighbor-Network-ID: %s, Interface-IP: %s",
				    ipaddr_string(&rlp->link_id),
				    ipaddr_string(&rlp->link_data));
				break;

			case RLA_TYPE_STUB:
				printf("\n\t      Stub-Network: %s, mask: %s",
				    ipaddr_string(&rlp->link_id),
				    ipaddr_string(&rlp->link_data));
				break;

			default:
				printf("\n\t      unknown Router Links Type (%u)",
				    rlp->link_type);
				return (0);
			}
			printf(", tos 0, metric: %d", EXTRACT_16BITS(&rlp->link_tos0metric));
			tosp = (struct tos_metric *)
			    ((sizeof rlp->link_tos0metric) + (u_char *) rlp);
			for (k = 0; k < (int) rlp->link_toscount; ++k, ++tosp) {
				TCHECK(*tosp);
				printf(", tos %d, metric: %d",
				    tosp->tos_type,
				    EXTRACT_16BITS(&tosp->tos_metric));
			}
			rlp = (struct rlalink *)((u_char *)(rlp + 1) +
			    ((rlp->link_toscount) * sizeof(*tosp)));
		}
		break;

	case LS_TYPE_NETWORK:
		TCHECK(lsap->lsa_un.un_nla.nla_mask);
		printf("\n\t    mask %s rtrs",
		    ipaddr_string(&lsap->lsa_un.un_nla.nla_mask));
		ap = lsap->lsa_un.un_nla.nla_router;
		while ((u_char *)ap < ls_end) {
			TCHECK(*ap);
			printf(" %s", ipaddr_string(ap));
			++ap;
		}
		break;

	case LS_TYPE_SUM_IP:
		TCHECK(lsap->lsa_un.un_nla.nla_mask);
		printf("\n\t    mask %s",
		    ipaddr_string(&lsap->lsa_un.un_sla.sla_mask));
		TCHECK(lsap->lsa_un.un_sla.sla_tosmetric);
		lp = lsap->lsa_un.un_sla.sla_tosmetric;
                /* suppress tos if its not supported */
                if(!((lsap->ls_hdr.ls_options)&OSPF_OPTION_T)) {
                    printf(", metric: %u", EXTRACT_32BITS(lp)&SLA_MASK_METRIC);
                    break;
                }
		while ((u_char *)lp < ls_end) {
			register u_int32_t ul;

			TCHECK(*lp);
			ul = EXTRACT_32BITS(lp);
			printf(", tos %d metric %d",
			    (ul & SLA_MASK_TOS) >> SLA_SHIFT_TOS,
			    ul & SLA_MASK_METRIC);
			++lp;
		}
		break;

	case LS_TYPE_SUM_ABR:
		TCHECK(lsap->lsa_un.un_sla.sla_tosmetric);
		lp = lsap->lsa_un.un_sla.sla_tosmetric;
                /* suppress tos if its not supported */
                if(!((lsap->ls_hdr.ls_options)&OSPF_OPTION_T)) {
                    printf(", metric: %u", EXTRACT_32BITS(lp)&SLA_MASK_METRIC);
                    break;
                }
		while ((u_char *)lp < ls_end) {
			register u_int32_t ul;

			TCHECK(*lp);
			ul = EXTRACT_32BITS(lp);
			printf(", tos %d metric %d",
			    (ul & SLA_MASK_TOS) >> SLA_SHIFT_TOS,
			    ul & SLA_MASK_METRIC);
			++lp;
		}
		break;

	case LS_TYPE_ASE:
		TCHECK(lsap->lsa_un.un_nla.nla_mask);
		printf("\n\t    mask %s",
		    ipaddr_string(&lsap->lsa_un.un_asla.asla_mask));

		TCHECK(lsap->lsa_un.un_sla.sla_tosmetric);
		almp = lsap->lsa_un.un_asla.asla_metric;
		while ((u_char *)almp < ls_end) {
			register u_int32_t ul;

			TCHECK(almp->asla_tosmetric);
			ul = EXTRACT_32BITS(&almp->asla_tosmetric);
			printf(", type %d, tos %d metric:",
			    (ul & ASLA_FLAG_EXTERNAL) ? 2 : 1,
			    (ul & ASLA_MASK_TOS) >> ASLA_SHIFT_TOS);
                        if ((ul & ASLA_MASK_METRIC)==0xffffff)
                            printf(" infinite");
                        else
                            printf(" %d", (ul & ASLA_MASK_METRIC));

			TCHECK(almp->asla_forward);
			if (almp->asla_forward.s_addr) {
				printf(", forward %s",
				    ipaddr_string(&almp->asla_forward));
			}
			TCHECK(almp->asla_tag);
			if (almp->asla_tag.s_addr) {
				printf(", tag %s",
				    ipaddr_string(&almp->asla_tag));
			}
			++almp;
		}
		break;

	case LS_TYPE_GROUP:
		/* Multicast extensions as of 23 July 1991 */
		mcp = lsap->lsa_un.un_mcla;
		while ((u_char *)mcp < ls_end) {
			TCHECK(mcp->mcla_vid);
			switch (EXTRACT_32BITS(&mcp->mcla_vtype)) {

			case MCLA_VERTEX_ROUTER:
				printf("\n\t    Router Router-ID %s",
				    ipaddr_string(&mcp->mcla_vid));
				break;

			case MCLA_VERTEX_NETWORK:
				printf("\n\t    Network Designated Router %s",
				    ipaddr_string(&mcp->mcla_vid));
				break;

			default:
				printf("\n\t    unknown VertexType (%u)",
				    EXTRACT_32BITS(&mcp->mcla_vtype));
				break;
			}
		++mcp;
		}
	}

	return (0);
trunc:
	return (1);
}

static int
ospf_decode_v2(register const struct ospfhdr *op,
    register const u_char *dataend)
{
	register const struct in_addr *ap;
	register const struct lsr *lsrp;
	register const struct lsa_hdr *lshp;
	register const struct lsa *lsap;
	register int lsa_count;

	switch (op->ospf_type) {

	case OSPF_TYPE_UMD:
		/*
		 * Rob Coltun's special monitoring packets;
		 * do nothing
		 */
		break;

	case OSPF_TYPE_HELLO:
                TCHECK(op->ospf_hello.hello_deadint);
                printf("\n\t  Hello Timer: %us, Dead Timer %us, mask: %s, Priority: %u",
                       EXTRACT_16BITS(&op->ospf_hello.hello_helloint),
                       EXTRACT_32BITS(&op->ospf_hello.hello_deadint),
                       ipaddr_string(&op->ospf_hello.hello_mask),
                       op->ospf_hello.hello_priority);

                printf("\n\t  Options: %s",
                       bittok2str(ospf_option_values,"none",op->ospf_hello.hello_options));

		TCHECK(op->ospf_hello.hello_dr);
		if (op->ospf_hello.hello_dr.s_addr != 0)
			printf("\n\t  Designated Router %s",
			    ipaddr_string(&op->ospf_hello.hello_dr));

		TCHECK(op->ospf_hello.hello_bdr);
		if (op->ospf_hello.hello_bdr.s_addr != 0)
			printf(", Backup Designated Router %s",
			    ipaddr_string(&op->ospf_hello.hello_bdr));

                ap = op->ospf_hello.hello_neighbor;
                if ((u_char *)ap < dataend)
                        printf("\n\t  Neighbor List:");
                while ((u_char *)ap < dataend) {
                        TCHECK(*ap);
                        printf("\n\t    %s", ipaddr_string(ap));
                        ++ap;
                }
		break;	/* HELLO */

	case OSPF_TYPE_DD:
		TCHECK(op->ospf_db.db_options);
                printf("\n\t  Options: %s",
                       bittok2str(ospf_option_values,"none",op->ospf_db.db_options));
		TCHECK(op->ospf_db.db_flags);
                printf("\n\t  DD Flags: %s",
                       bittok2str(ospf_dd_flag_values,"none",op->ospf_db.db_flags));

		if (vflag) {
			/* Print all the LS adv's */
			lshp = op->ospf_db.db_lshdr;
			while (!ospf_print_lshdr(lshp)) {
				++lshp;
			}
		}
		break;

	case OSPF_TYPE_LS_REQ:
                lsrp = op->ospf_lsr;
                while ((u_char *)lsrp < dataend) {
                        TCHECK(*lsrp);

                        printf("\n\t  %s LSA (%d), LSA-ID: %s, Advertising Router: %s",
                               tok2str(lsa_values,"unknown",lsrp->ls_type),
                               lsrp->ls_type,
                               ipaddr_string(&lsrp->ls_stateid),
                               ipaddr_string(&lsrp->ls_router));
                        ++lsrp;
                }
		break;

	case OSPF_TYPE_LS_UPDATE:
                lsap = op->ospf_lsu.lsu_lsa;
                TCHECK(op->ospf_lsu.lsu_count);
                lsa_count = EXTRACT_32BITS(&op->ospf_lsu.lsu_count);
                printf(", %d LSA%s",lsa_count, lsa_count > 1 ? "s" : "");
                while (lsa_count--) {
                        if (ospf_print_lsa(lsap))
                                goto trunc;
                        lsap = (struct lsa *)((u_char *)lsap +
                                              EXTRACT_16BITS(&lsap->ls_hdr.ls_length));
                }
		break;

	case OSPF_TYPE_LS_ACK:
                lshp = op->ospf_lsa.lsa_lshdr;
                while (!ospf_print_lshdr(lshp)) {
                    ++lshp;
                }
                break;

	default:
		printf("v2 type (%d)", op->ospf_type);
		break;
	}
	return (0);
trunc:
	return (1);
}

void
ospf_print(register const u_char *bp, register u_int length,
    register const u_char *bp2)
{
	register const struct ospfhdr *op;
	register const struct ip *ip;
	register const u_char *dataend;
	register const char *cp;

	op = (struct ospfhdr *)bp;
	ip = (struct ip *)bp2;

        /* XXX Before we do anything else, strip off the MD5 trailer */
        TCHECK(op->ospf_authtype);
        if (EXTRACT_16BITS(&op->ospf_authtype) == OSPF_AUTH_MD5) {
                length -= OSPF_AUTH_MD5_LEN;
                snapend -= OSPF_AUTH_MD5_LEN;
        }

	/* If the type is valid translate it, or just print the type */
	/* value.  If it's not valid, say so and return */
	TCHECK(op->ospf_type);
	cp = tok2str(type2str, "unknown LS-Type %d", op->ospf_type);
	printf("OSPFv%d %s length: %d", op->ospf_version, cp, length);
	if (*cp == 'u')
		return;

        if(!vflag) /* non verbose - so lets bail out here */
                return;

	TCHECK(op->ospf_len);
	if (length != EXTRACT_16BITS(&op->ospf_len)) {
		printf(" [len %d]", EXTRACT_16BITS(&op->ospf_len));
		return;
	}
	dataend = bp + length;

	TCHECK(op->ospf_routerid);
        printf("\n\tRouter-ID: %s", ipaddr_string(&op->ospf_routerid));

	TCHECK(op->ospf_areaid);
	if (op->ospf_areaid.s_addr != 0)
		printf(", Area %s", ipaddr_string(&op->ospf_areaid));
	else
		printf(", Backbone Area");

	if (vflag) {
		/* Print authentication data (should we really do this?) */
		TCHECK2(op->ospf_authdata[0], sizeof(op->ospf_authdata));

                printf(", Authentication Type: %s (%u)",
                       tok2str(ospf_authtype_values,"unknown",EXTRACT_16BITS(&op->ospf_authtype)),
                       EXTRACT_16BITS(&op->ospf_authtype));

		switch (EXTRACT_16BITS(&op->ospf_authtype)) {

		case OSPF_AUTH_NONE:
			break;

		case OSPF_AUTH_SIMPLE:
			(void)fn_printn(op->ospf_authdata,
			    sizeof(op->ospf_authdata), NULL);
			printf("\"");
			break;

		case OSPF_AUTH_MD5:
                        printf("\n\tKey-ID: %u, Auth-Length: %u, Crypto Sequence Number: 0x%08x",
                               *((op->ospf_authdata)+2),
                               *((op->ospf_authdata)+3),
                               EXTRACT_32BITS((op->ospf_authdata)+4));
			break;

		default:
			return;
		}
	}
	/* Do rest according to version.	 */
	switch (op->ospf_version) {

	case 2:
		/* ospf version 2 */
		if (ospf_decode_v2(op, dataend))
			goto trunc;
		break;

	default:
		printf(" ospf [version %d]", op->ospf_version);
		break;
	}			/* end switch on version */

	return;
trunc:
	fputs(tstr, stdout);
}

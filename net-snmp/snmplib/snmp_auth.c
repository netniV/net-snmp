/*
 * snmp_auth.c
 *
 * Community name parse/build routines.
 */
/**********************************************************************
    Copyright 1988, 1989, 1991, 1992 by Carnegie Mellon University

			 All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/

#include <config.h>

#ifdef KINETICS
#include "gw.h"
#include "fp4/cmdmacro.h"
#endif

#include <stdio.h>
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <sys/types.h>
#if TIME_WITH_SYS_TIME
# ifdef WIN32
#  include <sys/timeb.h>
# else
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#if HAVE_WINSOCK_H
#include <winsock.h>
#endif

#ifdef vms
#include <in.h>
#endif

#include "asn1.h"
#include "snmp.h"
#include "snmp_api.h"
#include "snmp_impl.h"
#include "mib.h"
#include "md5.h"
#include "system.h"
#include "tools.h"
#include "snmp_debug.h"
#include "scapi.h"

/*
 * Globals.
 */

/*******************************************************************-o-******
 * snmp_comstr_parse
 *
 * Parameters:
 *	*data		(I)   Message.
 *	*length		(I/O) Bytes left in message.
 *	*psid		(O)   Community string.
 *	*slen		(O)   Length of community string.
 *	*version	(O)   Message version.
 *      
 * Returns:
 *	Pointer to the remainder of data.
 *
 *
 * Parse the header of a community string-based message such as that found
 * in SNMPv1 and SNMPv2c.
 */
u_char *
snmp_comstr_parse(u_char *data,
		  size_t *length,
		  u_char *psid,
		  size_t *slen,
		  int *version)
{
    u_char   	type;
    long	ver;


    /* Message is an ASN.1 SEQUENCE.
     */
    data = asn_parse_sequence(data, length, &type,
                        (ASN_SEQUENCE | ASN_CONSTRUCTOR), "auth message");
    if (data == NULL){
        return NULL;
    }

    /* First field is the version.
     */
    DEBUGDUMPHEADER("dump_recv", "Parsing SNMP version\n");
    data = asn_parse_int(data, length, &type, &ver, sizeof(ver));
    DEBUGINDENTLESS();
    *version = ver;
    if (data == NULL){
        ERROR_MSG("bad parse of version");
        return NULL;
    }

    /* second field is the community string for SNMPv1 & SNMPv2c */
    DEBUGDUMPHEADER("dump_recv", "Parsing community string\n");
    data = asn_parse_string(data, length, &type, psid, slen);
    DEBUGINDENTLESS();
    if (data == NULL){
        ERROR_MSG("bad parse of community");
        return NULL;
    }
    psid[*slen] = '\0';
    return (u_char *)data;

}  /* end snmp_comstr_parse() */




/*******************************************************************-o-******
 * snmp_comstr_build
 *
 * Parameters:
 *	*data
 *	*length
 *	*psid
 *	*slen
 *	*version
 *	 messagelen
 *      
 * Returns:
 *	Pointer into 'data' after built section.
 *
 *
 * Build the header of a community string-based message such as that found
 * in SNMPv1 and SNMPv2c.
 *
 * NOTE:	The length of the message will have to be inserted later,
 *		if not known.
 *
 * NOTE:	Version is an 'int'.  (CMU had it as a long, but was passing
 *		in a *int.  Grrr.)  Assign version to verfix and pass in
 *		that to asn_build_int instead which expects a long.  -- WH
 */
u_char *
snmp_comstr_build(	u_char	*data,
			size_t	*length,
			u_char	*psid,
			size_t	*slen,
			int	*version,
			size_t	messagelen)
{
    long	 verfix	 = *version;
    u_char	*h1	 = data;
    u_char	*h1e;
    size_t	 hlength = *length;


    /* Build the the message wrapper (note length will be inserted later).
     */
    data = asn_build_sequence(data, length, (u_char)(ASN_SEQUENCE | ASN_CONSTRUCTOR), 0);
    if (data == NULL){
        ERROR_MSG("buildheader");
        return NULL;
    }
    h1e = data;


    /* Store the version field.
     */
    data = asn_build_int(data, length,
            (u_char)(ASN_UNIVERSAL | ASN_PRIMITIVE | ASN_INTEGER),
            &verfix, sizeof(verfix));
    if (data == NULL){
        ERROR_MSG("buildint");
        return NULL;
    }


    /* Store the community string.
     */
    data = asn_build_string(data, length,
            (u_char)(ASN_UNIVERSAL | ASN_PRIMITIVE | ASN_OCTET_STR),
            psid, *(u_char *)slen);
    if (data == NULL){
        ERROR_MSG("buildstring");
        return NULL;
    }


    /* Insert length.
     */
    asn_build_sequence(h1, &hlength, (u_char)(ASN_SEQUENCE | ASN_CONSTRUCTOR),
                       data-h1e + messagelen);


    return data;

}  /* end snmp_comstr_build() */

#ifdef USE_INTERNAL_MD5

static void
md5Digest(	u_char	*start,
		size_t	 length,
		u_char	*digest)
{
    MDstruct	 MD;

    int		 i, j;
    u_char	*cp;
#if WORDS_BIGENDIAN
    u_char	*buf;
    u_char	 buffer[SNMP_MAX_LEN];
#endif


#if 0
    int count, sum;

    sum = 0;
    for(count = 0; count < length; count++)
	sum += start[count];
    printf("sum %d (%d)\n", sum, length);
#endif


#if WORDS_BIGENDIAN
    /* Do the computation in an array.
     */
    cp = buf = buffer;
    memmove(buf, start, length);
#else
    /* Do the computation in place.
     */
    cp = start;
#endif


    MDbegin(&MD);
    while(length >= 64){
	MDupdate(&MD, cp, 64 * 8);
	cp += 64;
	length -= 64;
    }
    MDupdate(&MD, cp, length * 8);
    /* MDprint(&MD); */

   for (i=0;i<4;i++)
     for (j=0;j<32;j=j+8)
	 *digest++ = (MD.buffer[i]>>j) & 0xFF;

}  /* end md5Digest() */

#endif /* USE_INTERNAL_MD5 */

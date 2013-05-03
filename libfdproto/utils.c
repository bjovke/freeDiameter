/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@freediameter.net>							 *
*													 *
* Copyright (c) 2013, WIDE Project and NICT								 *
* All rights reserved.											 *
* 													 *
* Redistribution and use of this software in source and binary forms, with or without modification, are  *
* permitted provided that the following conditions are met:						 *
* 													 *
* * Redistributions of source code must retain the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer.										 *
*    													 *
* * Redistributions in binary form must reproduce the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer in the documentation and/or other						 *
*   materials provided with the distribution.								 *
* 													 *
* * Neither the name of the WIDE Project or NICT nor the 						 *
*   names of its contributors may be used to endorse or 						 *
*   promote products derived from this software without 						 *
*   specific prior written permission of WIDE Project and 						 *
*   NICT.												 *
* 													 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A *
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 	 *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 	 *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR *
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF   *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.								 *
*********************************************************************************************************/

#include "fdproto-internal.h"

DECLARE_FD_DUMP_PROTOTYPE(fd_sa_dump_node, sSA * sa, int flags)
{
	char addrbuf[INET6_ADDRSTRLEN];
	size_t o = 0;
	if (!offset)
		offset = &o;
	
	if (sa) {
		int rc = getnameinfo(sa, sSAlen( sa ), addrbuf, sizeof(addrbuf), NULL, 0, flags);
		if (rc) {
			CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "%s", gai_strerror(rc)), return NULL);
		} else {
			CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "%s", &addrbuf[0]), return NULL);
		}
	} else {
		CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "(NULL / ANY)"), return NULL);
	}
	
	return *buf;
}

DECLARE_FD_DUMP_PROTOTYPE(fd_sa_dump_node_serv, sSA * sa, int flags) 
{
	char addrbuf[INET6_ADDRSTRLEN];
	char servbuf[32];
	size_t o = 0;
	if (!offset)
		offset = &o;
	
	if (sa) {
		int rc = getnameinfo(sa, sSAlen( sa ), addrbuf, sizeof(addrbuf), servbuf, sizeof(servbuf), flags);
		if (rc) {
			CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "%s", gai_strerror(rc)), return NULL);
		} else {
			CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "%s", &addrbuf[0]), return NULL);
		}
	} else {
		CHECK_MALLOC_DO( fd_dump_extend( FD_DUMP_STD_PARAMS, "(NULL / ANY)"), return NULL);
	}
	return *buf;
}

/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@nict.go.jp>							 *
*													 *
* Copyright (c) 2010, WIDE Project and NICT								 *
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

#include "tests.h"

#include <cnxctx.h>

#ifndef TEST_PORT
#define TEST_PORT	3868
#endif /* TEST_PORT */

#ifndef NB_STREAMS
#define NB_STREAMS	10
#endif /* NB_STREAMS */



/* Main test routine */
int main(int argc, char *argv[])
{
#ifdef DISABLE_SCTP
	/* In this case, we don't perform this simple test */
	PASSTEST();
#endif
	int sock, srvsock, clisock;
	char buf1[]="abcdef";
	char *buf2;
	size_t sz;
	struct fd_list eps = FD_LIST_INITIALIZER(eps);
	uint32_t status = 0;
	uint16_t str;
	int ev;
	
	/* Initialize the server addresses */
	{
		struct addrinfo hints, *ai, *aip;
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags  = AI_NUMERICSERV;
		hints.ai_family = AF_INET;
		CHECK( 0, getaddrinfo("localhost", _stringize(TEST_PORT), &hints, &ai) );
		aip = ai;
		while (aip) {
			CHECK( 0, fd_ep_add_merge( &eps, aip->ai_addr, aip->ai_addrlen, EP_FL_DISC | EP_ACCEPTALL ));
			aip = aip->ai_next;
		};
		freeaddrinfo(ai);
	}
	
	/* First, initialize the daemon modules */
	INIT_FD();
	
	/* Restrain the # of streams */
	fd_g_config->cnf_sctp_str = NB_STREAMS;
	
	/* Create the server socket */
	CHECK( 0, fd_sctp_create_bind_server( &sock, AF_INET6, &eps, TEST_PORT ));
	
	/* Accept incoming clients */
	CHECK( 0, fd_sctp_listen( sock ));
	
	/* Now, create the client socket */
	CHECK( 0, fd_sctp_client( &clisock, 0, TEST_PORT, &eps ));
	
	/* Accept this connection */
	srvsock = accept(sock, NULL, NULL);
	
	/* Send a first message */
	CHECK( 0, fd_sctp_sendstr(srvsock, 1, (uint8_t *)buf1, sizeof(buf1), &status) );
	CHECK( 0, status);
	
	/* Receive this message */
	CHECK( 0, fd_sctp_recvmeta(clisock, &str, (uint8_t **)&buf2, &sz, &ev, &status) );
	CHECK( FDEVP_CNX_MSG_RECV, ev);
	CHECK( 0, status);
	CHECK( 1, str);
	CHECK( sizeof(buf1), sz );
	CHECK( 0, memcmp(buf1, buf2, sz) );
	free(buf2); buf2 = NULL;
	
	/* Send in the other direction */
	CHECK( 0, fd_sctp_sendstr(clisock, 2, (uint8_t *)buf1, sizeof(buf1), &status) );
	CHECK( 0, status);
	
	/* Receive this message */
	CHECK( 0, fd_sctp_recvmeta(srvsock, &str, (uint8_t **)&buf2, &sz, &ev, &status) );
	CHECK( FDEVP_CNX_MSG_RECV, ev);
	CHECK( 0, status);
	CHECK( 2, str);
	CHECK( sizeof(buf1), sz );
	CHECK( 0, memcmp(buf1, buf2, sz) );
	free(buf2); buf2 = NULL;
	
	/* That's all for the tests yet */
	PASSTEST();
} 
	

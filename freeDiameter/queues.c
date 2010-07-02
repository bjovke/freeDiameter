/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@nict.go.jp>							 *
*													 *
* Copyright (c) 2009, WIDE Project and NICT								 *
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

#include "fD.h"

/* The global message queues */
struct fifo * fd_g_incoming = NULL;
struct fifo * fd_g_outgoing = NULL;
struct fifo * fd_g_local = NULL;

/* Initialize the message queues. */
int fd_queues_init(void)
{
	TRACE_ENTRY();
	CHECK_FCT( fd_fifo_new ( &fd_g_incoming ) );
	CHECK_FCT( fd_fifo_new ( &fd_g_outgoing ) );
	CHECK_FCT( fd_fifo_new ( &fd_g_local ) );
	return 0;
}

/* Destroy a queue after emptying it (and dumping the content) */
int fd_queues_fini(struct fifo ** queue)
{
	struct msg * msg;
	int ret = 0;
	
	TRACE_ENTRY("%p", queue);
	
	/* Note : the threads that post into this queue should already been stopped before this !!! */

	/* Empty all contents */
	while (1) {
		/* Check if there is a message in the queue */
		ret = fd_fifo_tryget(*queue, &msg);
		if (ret == EWOULDBLOCK)
			break;
		CHECK_FCT(ret);
		
		/* We got one! */
		fd_log_debug("The following message is lost because the daemon is stopping:\n");
		fd_msg_dump_walk(NONE, msg);
		fd_msg_free(msg);
	}
	
	/* Now, delete the empty queue */
	CHECK_FCT( fd_fifo_del ( queue ) );
	
	return 0;
}

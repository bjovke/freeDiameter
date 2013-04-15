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

#include "fdcore-internal.h"

/* Alloc a new hbh for requests, bufferize the message and send on the connection, save in sentreq if provided */
static int do_send(struct msg ** msg, uint32_t flags, struct cnxctx * cnx, uint32_t * hbh, struct sr_list * srl)
{
	struct msg_hdr * hdr;
	int msg_is_a_req;
	uint8_t * buf;
	size_t sz;
	int ret;
	uint32_t bkp_hbh = 0;
	struct timespec senton;
	struct msg * cpy_for_logs_only;
	
	TRACE_ENTRY("%p %x %p %p %p", msg, flags, cnx, hbh, srl);
	
	/* Retrieve the message header */
	CHECK_FCT( fd_msg_hdr(*msg, &hdr) );
	
	msg_is_a_req = (hdr->msg_flags & CMD_FLAG_REQUEST);
	if (msg_is_a_req) {
		CHECK_PARAMS(hbh && srl);
		/* Alloc the hop-by-hop id and increment the value for next message */
		bkp_hbh = hdr->msg_hbhid;
		hdr->msg_hbhid = *hbh;
		*hbh = hdr->msg_hbhid + 1;
	}
	
	/* Create the message buffer */
	CHECK_FCT(fd_msg_bufferize( *msg, &buf, &sz ));
	pthread_cleanup_push( free, buf );
	
	cpy_for_logs_only = *msg;
	
	/* Save a request before sending so that there is no race condition with the answer */
	if (msg_is_a_req) {
		CHECK_FCT_DO( ret = fd_p_sr_store(srl, msg, &hdr->msg_hbhid, bkp_hbh), goto out );
	}
	
	CHECK_SYS_DO( clock_gettime(CLOCK_REALTIME, &senton), /* ... */ );
	CHECK_FCT_DO( fd_msg_ts_set_sent(cpy_for_logs_only, &senton), /* ... */ );
	
	/* Log the message */
	fd_msg_log( FD_MSG_LOG_SENT, cpy_for_logs_only, "Sent to '%s'", fd_cnx_getid(cnx));
	
	{
		struct timespec rcvon, delay;
		
		(void) fd_msg_ts_get_recv(cpy_for_logs_only, &rcvon);
		if (rcvon.tv_sec != 0 || rcvon.tv_nsec != 0) {
			TS_DIFFERENCE( &delay, &rcvon, &senton);
			fd_msg_log( FD_MSG_LOG_TIMING, cpy_for_logs_only, "Forwarded in %ld.%6.6ld sec", (long)delay.tv_sec, delay.tv_nsec/1000);
		} else { /* We log the answer time only for answers generated locally */
			if (!msg_is_a_req) {
				/* get the matching request */
				struct msg * req;
				struct timespec reqrcvon;
				(void) fd_msg_answ_getq(cpy_for_logs_only, &req);
				(void) fd_msg_ts_get_recv(req, &reqrcvon);
				TS_DIFFERENCE( &delay, &reqrcvon, &senton);
				fd_msg_log( FD_MSG_LOG_TIMING, cpy_for_logs_only, "Answered in %ld.%6.6ld sec", (long)delay.tv_sec, delay.tv_nsec/1000);
			}
		}
	}
	
	/* Send the message */
	CHECK_FCT_DO( ret = fd_cnx_send(cnx, buf, sz, flags), );
out:
	;	
	pthread_cleanup_pop(1);
	
	if (ret)
		return ret;
	
	/* Free remaining messages (i.e. answers) */
	if (*msg) {
		CHECK_FCT( fd_msg_free(*msg) );
		*msg = NULL;
	}
	
	return 0;
}

static void cleanup_requeue(void * arg)
{
	struct msg *msg = arg;
	CHECK_FCT_DO(fd_fifo_post(fd_g_outgoing, &msg),
		{
			fd_msg_log( FD_MSG_LOG_DROPPED, msg, "An error occurred while attempting to requeue this message during cancellation of the sending function");
			CHECK_FCT_DO(fd_msg_free(msg), /* What can we do more? */);
		} );
}

/* The code of the "out" thread */
static void * out_thr(void * arg)
{
	struct fd_peer * peer = arg;
	ASSERT( CHECK_PEER(peer) );
	
	/* Set the thread name */
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "OUT/%s", peer->p_hdr.info.pi_diamid);
		fd_log_threadname ( buf );
	}
	
	/* Loop until cancelation */
	while (1) {
		struct msg * msg;
		int ret;
		
		/* Retrieve next message to send */
		CHECK_FCT_DO( fd_fifo_get(peer->p_tosend, &msg), goto error );
		
		/* Now if we are cancelled, we requeue this message */
		pthread_cleanup_push(cleanup_requeue, msg);
		
		/* Send the message, log any error */
		CHECK_FCT_DO( ret = do_send(&msg, 0, peer->p_cnxctx, &peer->p_hbh, &peer->p_sr),
			{
				if (msg) {
					fd_msg_log( FD_MSG_LOG_DROPPED, msg, "Internal error: Problem while sending (%s)", strerror(ret) );
					fd_msg_free(msg);
				}
			} );
			
		/* Loop */
		pthread_cleanup_pop(0);
	}
	
error:
	/* It is not really a connection error, but the effect is the same, we are not able to send anymore message */
	CHECK_FCT_DO( fd_event_send(peer->p_events, FDEVP_CNX_ERROR, 0, NULL), /* What do we do if it fails? */ );
	return NULL;
}

/* Wrapper to sending a message either by out thread (peer in OPEN state) or directly; cnx or peer must be provided. Flags are valid only for direct sending, not through thread (unused) */
int fd_out_send(struct msg ** msg, struct cnxctx * cnx, struct fd_peer * peer, uint32_t flags)
{
	struct msg_hdr * hdr;
	
	TRACE_ENTRY("%p %p %p %x", msg, cnx, peer, flags);
	CHECK_PARAMS( msg && *msg && (cnx || (peer && peer->p_cnxctx)));
	
	if (peer) {
		CHECK_FCT( fd_msg_hdr(*msg, &hdr) );
		if (!(hdr->msg_flags & CMD_FLAG_REQUEST)) {
			/* Update the count of pending answers to send */
			CHECK_POSIX( pthread_mutex_lock(&peer->p_state_mtx) );
			peer->p_reqin_count--;
			CHECK_POSIX( pthread_mutex_unlock(&peer->p_state_mtx) );			
		}
	}
	
	if (fd_peer_getstate(peer) == STATE_OPEN) {
		/* Normal case: just queue for the out thread to pick it up */
		CHECK_FCT( fd_fifo_post(peer->p_tosend, msg) );
		
	} else {
		int ret;
		uint32_t *hbh = NULL;
		
		/* In other cases, the thread is not running, so we handle the sending directly */
		if (peer)
			hbh = &peer->p_hbh;

		if (!cnx)
			cnx = peer->p_cnxctx;

		/* Do send the message */
		CHECK_FCT_DO( ret = do_send(msg, flags, cnx, hbh, peer ? &peer->p_sr : NULL),
			{
				if (msg) {
					fd_msg_log( FD_MSG_LOG_DROPPED, *msg, "Internal error: Problem while sending (%s)", strerror(ret) );
					fd_msg_free(*msg);
					*msg = NULL;
				}
			} );
	}
	
	return 0;
}

/* Start the "out" thread that picks messages in p_tosend and send them on p_cnxctx */
int fd_out_start(struct fd_peer * peer)
{
	TRACE_ENTRY("%p", peer);
	CHECK_PARAMS( CHECK_PEER(peer) && (peer->p_outthr == (pthread_t)NULL) );
	
	CHECK_POSIX( pthread_create(&peer->p_outthr, NULL, out_thr, peer) );
	
	return 0;
}

/* Stop that thread */
int fd_out_stop(struct fd_peer * peer)
{
	TRACE_ENTRY("%p", peer);
	CHECK_PARAMS( CHECK_PEER(peer) );
	
	CHECK_FCT( fd_thr_term(&peer->p_outthr) );
	
	return 0;
}
		

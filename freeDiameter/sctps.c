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

/* This file contains code for TLS over multi-stream SCTP wrapper implementation (GnuTLS does not support this) */
/* See http://aaa.koganei.wide.ad.jp/blogs/index.php/waaad/2008/08/18/tls-over-sctp for history */

#include "fD.h"
#include "cnxctx.h"

#include <netinet/sctp.h>
#include <sys/uio.h>

/*

Architecture of this wrapper:
 - we have several fifo queues (1 per stream pairs).
 GnuTLS is configured to use custom push / pull functions:
 - the pull function retrieves the data from the fifo queue corresponding to a stream #.
 - the push function sends the data on a certain stream.
 We also have a demux thread that reads the socket and store received data in the appropriate fifo
 
 We have one gnutls_session per stream pair, and as many streams that read the gnutls records and save incoming data to the target queue.
 
This complexity is required because we cannot read a socket for a given stream only; we can only get the next message and find its stream.
*/



/*************************************************************/
/*                      threads                              */
/*************************************************************/

/* Demux received data and store in the appropriate fifo */
static void * demuxer(void * arg)
{
	struct cnxctx * conn = arg;
	uint8_t * buf;
	size_t    bufsz;
	int	  event;
	uint16_t  strid;
	
	TRACE_ENTRY("%p", arg);
	CHECK_PARAMS_DO(conn && (conn->cc_socket > 0), goto out);
	
	/* Set the thread name */
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "Demuxer (%d)", conn->cc_socket);
		fd_log_threadname ( buf );
	}
	
	ASSERT( conn->cc_proto == IPPROTO_SCTP );
	ASSERT( Target_Queue(conn) );
	ASSERT( conn->cc_sctps_data.array );
	
	do {
		CHECK_FCT_DO( fd_sctp_recvmeta(conn->cc_socket, &strid, &buf, &bufsz, &event), goto error );
		switch (event) {
			case FDEVP_CNX_MSG_RECV:
				/* Demux this message in the appropriate fifo, another thread will pull, gnutls process, and send in target queue */
				if (strid < conn->cc_sctp_para.pairs) {
					CHECK_FCT_DO(fd_event_send(conn->cc_sctps_data.array[strid].raw_recv, event, bufsz, buf), goto error );
				} else {
					TRACE_DEBUG(INFO, "Received packet (%d bytes) on out-of-range stream #%s from %s, discarded.", bufsz, strid, conn->cc_remid);
					free(buf);
				}
				break;
				
			case FDEVP_CNX_EP_CHANGE:
				/* Send this event to the target queue */
				CHECK_FCT_DO( fd_event_send( Target_Queue(conn), event, bufsz, buf), goto error );
				break;
			
			case FDEVP_CNX_ERROR:
			default:
				goto error;
		}
		
	} while (conn->cc_loop);
	
out:
	TRACE_DEBUG(FULL, "Thread terminated");	
	return NULL;
error:
	if (!conn->cc_closing) {
		CHECK_FCT_DO( fd_event_send( Target_Queue(conn), FDEVP_CNX_ERROR, 0, NULL), /* continue or destroy everything? */);
	}
	
	/* Since the demux thread terminates, we must trig an error for all decipher threads. We do this by destroying all demuxed FIFO queues */
	for (strid = 0; strid < conn->cc_sctp_para.pairs; strid++) {
		fd_event_destroy( &conn->cc_sctps_data.array[strid].raw_recv, free );
	}
	
	goto out;
}

/* Decrypt the data received in this stream pair and store it in the target queue */
static void * decipher(void * arg)
{
	struct sctps_ctx * ctx = arg;
	struct cnxctx 	 *cnx;
	
	TRACE_ENTRY("%p", arg);
	CHECK_PARAMS_DO(ctx && ctx->raw_recv && ctx->parent, goto error);
	cnx = ctx->parent;
	ASSERT( Target_Queue(cnx) );
	
	/* Set the thread name */
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "Decipher (%hu@%d)", ctx->strid, cnx->cc_socket);
		fd_log_threadname ( buf );
	}
	
	CHECK_FCT_DO(fd_tls_rcvthr_core(cnx, ctx->strid ? ctx->session : cnx->cc_tls_para.session), /* continue */);
error:
	if (!cnx->cc_closing) {
		CHECK_FCT_DO( fd_event_send( Target_Queue(cnx), FDEVP_CNX_ERROR, 0, NULL), /* continue or destroy everything? */);
	}
	TRACE_DEBUG(FULL, "Thread terminated");	
	return NULL;
}

/*************************************************************/
/*                     push / pull                           */
/*************************************************************/

/* Send data over the connection, called by gnutls */
static ssize_t sctps_push(gnutls_transport_ptr_t tr, const void * data, size_t len)
{
	struct sctps_ctx * ctx = (struct sctps_ctx *) tr;
	
	TRACE_ENTRY("%p %p %zd", tr, data, len);
	CHECK_PARAMS_DO( tr && data, { errno = EINVAL; return -1; } );
	
	CHECK_FCT_DO( fd_sctp_sendstr(ctx->parent->cc_socket, ctx->strid, (uint8_t *)data, len), /* errno is already set */ return -1 );
	
	return len;
}

/* Retrieve data received on a stream and already demultiplexed */
static ssize_t sctps_pull(gnutls_transport_ptr_t tr, void * buf, size_t len)
{
	struct sctps_ctx * ctx = (struct sctps_ctx *) tr;
	size_t pulled = 0;
	int emptied;
	
	TRACE_ENTRY("%p %p %zd", tr, buf, len);
	CHECK_PARAMS_DO( tr && buf, { errno = EINVAL; return -1; } );
	
	/* If we don't have data available now, pull new message from the fifo -- this is blocking */
	if (!ctx->partial.buf) {
		int ev;
		CHECK_FCT_DO( errno = fd_event_get(ctx->raw_recv, &ev, &ctx->partial.bufsz, (void *)&ctx->partial.buf), return -1 );
		ASSERT( ev == FDEVP_CNX_MSG_RECV );
	}
		
	pulled = ctx->partial.bufsz - ctx->partial.offset;
	if (pulled <= len) {
		emptied = 1;
	} else {
		/* limit to the capacity of destination buffer */
		emptied = 0;
		pulled = len;
	}

	/* Store the data in the destination buffer */
	memcpy(buf, ctx->partial.buf + ctx->partial.offset, pulled);

	/* Free the buffer if we read all its content, and reset the partial structure */
	if (emptied) {
		free(ctx->partial.buf);
		memset(&ctx->partial, 0, sizeof(ctx->partial));
	} else {
		ctx->partial.offset += pulled;
	}

	/* We are done */
	return pulled;
}

/* Set the parameters of a session to use the appropriate fifo and stream information */
static void set_sess_transport(gnutls_session_t session, struct sctps_ctx *ctx)
{
	/* Set the transport pointer passed to push & pull callbacks */
	gnutls_transport_set_ptr( session, (gnutls_transport_ptr_t) ctx );
	
	/* Reset the low water value, since we don't use sockets */
	gnutls_transport_set_lowat( session, 0 );
	
	/* Set the push and pull callbacks */
	gnutls_transport_set_pull_function(session, sctps_pull);
	gnutls_transport_set_push_function(session, sctps_push);

	return;
}

/*************************************************************/
/*               Session resuming support                    */
/*************************************************************/

struct sr_store {
	struct fd_list	 list;	/* list of sr_data, ordered by key.size then key.data */
	pthread_rwlock_t lock;
	struct cnxctx   *parent;
	/* Add another list to chain in a global list to implement a garbage collector on sessions */
};

/* Saved master session data for resuming sessions */
struct sr_data {
	struct fd_list	chain;
	gnutls_datum_t	key;
	gnutls_datum_t 	data;
};

/* The level at which we debug session resuming */
#define SR_LEVEL (FULL + 1)

/* Initialize the store area for a connection */
static int store_init(struct cnxctx * conn)
{
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS( conn && !conn->cc_sctps_data.sess_store );
	
	CHECK_MALLOC( conn->cc_sctps_data.sess_store = malloc(sizeof(struct sr_store)) );
	memset(conn->cc_sctps_data.sess_store, 0, sizeof(struct sr_store));
	
	fd_list_init(&conn->cc_sctps_data.sess_store->list, NULL);
	CHECK_POSIX( pthread_rwlock_init(&conn->cc_sctps_data.sess_store->lock, NULL) );
	conn->cc_sctps_data.sess_store->parent = conn;
	
	return 0;
}

/* Destroy the store area for a connection, and all its content */
static void store_destroy(struct cnxctx * conn)
{
	/* Del all list entries */
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS_DO( conn, return );
	
	if (!conn->cc_sctps_data.sess_store)
		return;
	
	CHECK_POSIX_DO( pthread_rwlock_destroy(&conn->cc_sctps_data.sess_store->lock), /* continue */ );
	
	while (!FD_IS_LIST_EMPTY(&conn->cc_sctps_data.sess_store->list)) {
		struct sr_data * sr = (struct sr_data *) conn->cc_sctps_data.sess_store->list.next;
		fd_list_unlink( &sr->chain );
		free(sr->key.data);
		free(sr->data.data);
		free(sr);
	}
	
	free(conn->cc_sctps_data.sess_store);
	conn->cc_sctps_data.sess_store = NULL;
	return;
}

/* Search the position (or next if not found) of a key in a store */
static struct fd_list * find_or_next(struct sr_store * sto, gnutls_datum_t key, int * match)
{
	struct fd_list * ret;
	*match = 0;
	
	for (ret = sto->list.next; ret != &sto->list; ret = ret->next) {
		int cmp = 0;
		struct sr_data * sr = (struct sr_data *)ret;
		
		if ( key.size < sr->key.size )
			break;
		
		if ( key.size > sr->key.size )
			continue;
		
		/* Key sizes are equal */
		cmp = memcmp( key.data, sr->key.data, key.size );
		
		if (cmp > 0)
			continue;
		
		if (cmp == 0)
			*match = 1;
		
		break;
	}
	
	return ret;
}

/* Callbacks for the TLS server side of the connection, called during gnutls_handshake */
static int sr_store (void *dbf, gnutls_datum_t key, gnutls_datum_t data)
{
	struct sr_store * sto = (struct sr_store *)dbf;
	struct fd_list * li;
	struct sr_data * sr;
	int match = 0;
	int ret = 0;
	
	CHECK_PARAMS_DO( sto && key.data && data.data, return -1 );
	
	CHECK_POSIX_DO( pthread_rwlock_wrlock(&sto->lock), return -1 );
	TRACE_DEBUG_BUFFER(SR_LEVEL, "Session store [key ", key.data, key.size, "]");
	
	li = find_or_next(sto, key, &match);
	if (match) {
		sr = (struct sr_data *)li;
		
		/* Check the data is the same */
		if ((data.size != sr->data.size) || memcmp(data.data, sr->data.data, data.size)) {
			TRACE_DEBUG(SR_LEVEL, "GnuTLS tried to store a session with same key and different data!");
			ret = -1;
		} else {
			TRACE_DEBUG(SR_LEVEL, "GnuTLS tried to store a session with same key and same data, skipped.");
		}
		goto out;
	}
	
	/* Create a new entry */
	CHECK_MALLOC_DO( sr = malloc(sizeof(struct sr_data)), { ret = -1; goto out; } );
	memset(sr, 0, sizeof(struct sr_data));

	fd_list_init(&sr->chain, sr);

	CHECK_MALLOC_DO( sr->key.data = malloc(key.size), { ret = -1; goto out; } );
	sr->key.size = key.size;
	memcpy(sr->key.data, key.data, key.size);

	CHECK_MALLOC_DO( sr->data.data = malloc(data.size), { ret = -1; goto out; } );
	sr->data.size = data.size;
	memcpy(sr->data.data, data.data, data.size);
	
	/* Save this new entry in the list, we are done */
	fd_list_insert_before(li, &sr->chain);

out:	
	CHECK_POSIX_DO( pthread_rwlock_unlock(&sto->lock), return -1 );
	return ret;
}

static int sr_remove (void *dbf, gnutls_datum_t key)
{
	struct sr_store * sto = (struct sr_store *)dbf;
	struct fd_list * li;
	struct sr_data * sr;
	int match = 0;
	int ret = 0;
	
	CHECK_PARAMS_DO( sto && key.data, return -1 );
	
	CHECK_POSIX_DO( pthread_rwlock_wrlock(&sto->lock), return -1 );
	TRACE_DEBUG_BUFFER(SR_LEVEL, "Session delete [key ", key.data, key.size, "]");
	
	li = find_or_next(sto, key, &match);
	if (match) {
		sr = (struct sr_data *)li;
		
		/* Destroy this data */
		fd_list_unlink(li);
		free(sr->key.data);
		free(sr->data.data);
		free(sr);
	} else {
		/* It was not found */
		ret = -1;
	}
	
	CHECK_POSIX_DO( pthread_rwlock_unlock(&sto->lock), return -1 );
	return ret;
}

static gnutls_datum_t sr_fetch (void *dbf, gnutls_datum_t key)
{
	struct sr_store * sto = (struct sr_store *)dbf;
	struct fd_list * li;
	struct sr_data * sr;
	int match = 0;
	gnutls_datum_t res = { NULL, 0 };
	gnutls_datum_t error = { NULL, 0 };

	CHECK_PARAMS_DO( sto && key.data, return error );

	CHECK_POSIX_DO( pthread_rwlock_rdlock(&sto->lock), return error );
	TRACE_DEBUG_BUFFER(SR_LEVEL, "Session fetch [key ", key.data, key.size, "]");
	
	li = find_or_next(sto, key, &match);
	if (match) {
		sr = (struct sr_data *)li;
		CHECK_MALLOC_DO(res.data = gnutls_malloc(sr->data.size), goto out );
		res.size = sr->data.size;
		memcpy(res.data, sr->data.data, res.size);
	}
out:	
	TRACE_DEBUG(SR_LEVEL, "Fetched (%p, %d) from store %p", res.data, res.size, sto);
	CHECK_POSIX_DO( pthread_rwlock_unlock(&sto->lock), return error);
	return res;
}

/* Set the session pointer in a session object */
static void set_resume_callbacks(gnutls_session_t session, struct cnxctx * conn)
{
	TRACE_ENTRY("%p", conn);
	
	gnutls_db_set_retrieve_function(session, sr_fetch);
	gnutls_db_set_remove_function  (session, sr_remove);
	gnutls_db_set_store_function   (session, sr_store);
	gnutls_db_set_ptr              (session, conn->cc_sctps_data.sess_store);
	
	return;
}

/* The handshake is made in parallel in several threads to speed up */
static void * handshake_resume_th(void * arg)
{
	struct sctps_ctx * ctx = (struct sctps_ctx *) arg;
	int resumed;
	
	TRACE_ENTRY("%p", arg);
	
	/* Set the thread name */
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "Handshake resume (%hu@%d)", ctx->strid, ctx->parent->cc_socket);
		fd_log_threadname ( buf );
	}
	
	TRACE_DEBUG(FULL, "Starting TLS resumed handshake on stream %hu", ctx->strid);
	CHECK_GNUTLS_DO( gnutls_handshake( ctx->session ), return NULL);
			
	resumed = gnutls_session_is_resumed(ctx->session);
	if (!resumed) {
		/* Check the credentials here also */
		CHECK_FCT_DO( fd_tls_verify_credentials(ctx->session, ctx->parent, 0), return NULL );
	}
	if (TRACE_BOOL(FULL)) {
		if (resumed) {
			fd_log_debug("Session was resumed successfully on stream %hu (conn: '%s')\n", ctx->strid, fd_cnx_getid(ctx->parent));
		} else {
			fd_log_debug("Session was NOT resumed on stream %hu  (full handshake + verif) (conn: '%s')\n", ctx->strid, fd_cnx_getid(ctx->parent));
		}
	}
			
	/* Finished, OK */
	return arg;
}


/*************************************************************/
/*                     Exported functions                    */
/*************************************************************/

/* Initialize the wrapper for the connection */
int fd_sctps_init(struct cnxctx * conn)
{
	uint16_t i;
	
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS( conn && (conn->cc_sctp_para.pairs > 1) && (!conn->cc_sctps_data.array) );
	
	/* First, alloc the array and initialize the non-TLS data */
	CHECK_MALLOC( conn->cc_sctps_data.array = calloc(conn->cc_sctp_para.pairs, sizeof(struct sctps_ctx))  );
	for (i = 0; i < conn->cc_sctp_para.pairs; i++) {
		conn->cc_sctps_data.array[i].parent = conn;
		conn->cc_sctps_data.array[i].strid  = i;
		CHECK_FCT( fd_fifo_new(&conn->cc_sctps_data.array[i].raw_recv) );
	}
	
	/* Set push/pull functions in the master session, using fifo in array[0] */
	set_sess_transport(conn->cc_tls_para.session, &conn->cc_sctps_data.array[0]);
	
	/* For server side, we also initialize the resuming capabilities */
	if (conn->cc_tls_para.mode == GNUTLS_SERVER) {
		
		/* Prepare the store for sessions data */
		CHECK_FCT( store_init(conn) );
		
		/* Set the callbacks for resuming in the master session */
		set_resume_callbacks(conn->cc_tls_para.session, conn);
	}

	/* Start the demux thread */
	CHECK_POSIX( pthread_create( &conn->cc_rcvthr, NULL, demuxer, conn ) );
	
	return 0;
}

/* Handshake other streams, after full handshake on the master session */
int fd_sctps_handshake_others(struct cnxctx * conn, char * priority, void * alt_creds)
{
	uint16_t i;
	int errors = 0;
	gnutls_datum_t 	master_data;
	
	TRACE_ENTRY("%p %p", conn, priority);
	CHECK_PARAMS( conn && (conn->cc_sctp_para.pairs > 1) && conn->cc_sctps_data.array );

	/* Server side: we set all the parameters, the resume callback will take care of resuming the session */
	/* Client side: we duplicate the parameters of the master session, then set the transport pointer */
	
	/* For client side, retrieve the master session parameters */
	if (conn->cc_tls_para.mode == GNUTLS_CLIENT) {
		CHECK_GNUTLS_DO( gnutls_session_get_data2(conn->cc_tls_para.session, &master_data), return ENOMEM );
		/* For debug: */
		if (TRACE_BOOL(SR_LEVEL)) {
			uint8_t  id[256];
			size_t	 ids = sizeof(id);
			CHECK_GNUTLS_DO( gnutls_session_get_id(conn->cc_tls_para.session, id, &ids), /* continue */ );
			TRACE_DEBUG_BUFFER(SR_LEVEL, "Master session id: [", id, ids, "]");
		}
	}
	
	/* Initialize the session objects and start the handshake in a separate thread */
	for (i = 1; i < conn->cc_sctp_para.pairs; i++) {
		/* Set credentials and priority */
		CHECK_FCT( fd_tls_prepare(&conn->cc_sctps_data.array[i].session, conn->cc_tls_para.mode, priority, alt_creds) );
		
		/* For the client, copy data from master session; for the server, set session resuming pointers */
		if (conn->cc_tls_para.mode == GNUTLS_CLIENT) {
			CHECK_GNUTLS_DO( gnutls_session_set_data(conn->cc_sctps_data.array[i].session, master_data.data, master_data.size), return ENOMEM );
		} else {
			set_resume_callbacks(conn->cc_sctps_data.array[i].session, conn);
		}
		
		/* Set transport parameters */
		set_sess_transport(conn->cc_sctps_data.array[i].session, &conn->cc_sctps_data.array[i]);
		
		/* Start the handshake thread */
		CHECK_POSIX( pthread_create( &conn->cc_sctps_data.array[i].thr, NULL, handshake_resume_th, &conn->cc_sctps_data.array[i] ) );
	}
	
	/* We can now release the memory of master session data if any */
	if (conn->cc_tls_para.mode == GNUTLS_CLIENT) {
		gnutls_free(master_data.data);
	}
	
	/* Now wait for all handshakes to finish */
	for (i = 1; i < conn->cc_sctp_para.pairs; i++) {
		void * ret;
		CHECK_POSIX( pthread_join(conn->cc_sctps_data.array[i].thr, &ret) );
		if (ret == NULL) {
			errors++; /* Handshake failed on this stream */
		}
	}
	
	return errors ? ENOTCONN : 0;
}

/* Receive messages from all stream pairs */
int fd_sctps_startthreads(struct cnxctx * conn)
{
	uint16_t i;
	
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS( conn && conn->cc_sctps_data.array );
	
	for (i = 0; i < conn->cc_sctp_para.pairs; i++) {
		
		/* Start the decipher thread */
		CHECK_POSIX( pthread_create( &conn->cc_sctps_data.array[i].thr, NULL, decipher, &conn->cc_sctps_data.array[i] ) );
	}
	return 0;
}

/* Initiate a "bye" on all stream pairs */
void fd_sctps_bye(struct cnxctx * conn)
{
	uint16_t i;
	
	CHECK_PARAMS_DO( conn && conn->cc_sctps_data.array, return );
	
	/* End all TLS sessions, in series (not as efficient as paralel, but simpler) */
	for (i = 1; i < conn->cc_sctp_para.pairs; i++) {
		CHECK_GNUTLS_DO( gnutls_bye(conn->cc_sctps_data.array[i].session, GNUTLS_SHUT_WR), /* Continue */ );
	}
}

/* After "bye" was sent on all streams, read from sessions until an error is received */
void fd_sctps_waitthreadsterm(struct cnxctx * conn)
{
	uint16_t i;
	
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS_DO( conn && conn->cc_sctps_data.array, return );
	
	for (i = 0; i < conn->cc_sctp_para.pairs; i++) {
		if (conn->cc_sctps_data.array[i].thr != (pthread_t)NULL) {
			CHECK_POSIX_DO( pthread_join(conn->cc_sctps_data.array[i].thr, NULL), /* continue */ );
			conn->cc_sctps_data.array[i].thr = (pthread_t)NULL;
		}
	}
	return;
}

/* Free gnutls resources of all sessions */
void fd_sctps_gnutls_deinit_others(struct cnxctx * conn)
{
	uint16_t i;
	
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS_DO( conn && conn->cc_sctps_data.array, return );
	
	for (i = 1; i < conn->cc_sctp_para.pairs; i++) {
		gnutls_deinit(conn->cc_sctps_data.array[i].session);
	}
}


/* Stop all receiver threads */
void fd_sctps_stopthreads(struct cnxctx * conn)
{
	uint16_t i;
	
	TRACE_ENTRY("%p", conn);
	CHECK_PARAMS_DO( conn && conn->cc_sctps_data.array, return );
	
	for (i = 0; i < conn->cc_sctp_para.pairs; i++) {
		CHECK_FCT_DO( fd_thr_term(&conn->cc_sctps_data.array[i].thr), /* continue */ );
	}
	return;
}

/* Destroy a wrapper context */
void fd_sctps_destroy(struct cnxctx * conn)
{
	uint16_t i;
	
	CHECK_PARAMS_DO( conn && conn->cc_sctps_data.array, return );
	
	/* Terminate all receiving threads in case we did not do it yet */
	fd_sctps_stopthreads(conn);
	
	/* Now, stop the demux thread */
	CHECK_FCT_DO( fd_thr_term(&conn->cc_rcvthr), /* continue */ );
	
	/* Free remaining data in the array */
	for (i = 0; i < conn->cc_sctp_para.pairs; i++) {
		if (conn->cc_sctps_data.array[i].raw_recv)
			fd_event_destroy( &conn->cc_sctps_data.array[i].raw_recv, free );
		free(conn->cc_sctps_data.array[i].partial.buf);
		/* gnutls_session was already deinit */
	}
	
	/* Free the array itself now */
	free(conn->cc_sctps_data.array);
	conn->cc_sctps_data.array = NULL;
	
	/* Delete the store of sessions */
	store_destroy(conn);
	
	return ;
}

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

/* This file contains the server (listening) part of the daemon */

struct fd_list		FD_SERVERS = FD_LIST_INITIALIZER(FD_SERVERS);	/* The list of all server sockets */
/* We don't need to protect this list, it is only accessed from the main thread. */

/* Server (listening socket) information */
struct server {
	struct fd_list	chain;		/* link in the FD_SERVERS list */

	int 		socket;		/* server socket, or <= 0 */
	
	int 		proto;		/* IPPROTO_TCP or IPPROTO_SCTP */
	int 		secur;		/* TLS is started immediatly after connection ? */
	
	pthread_t	serv_thr;	/* The thread listening for new connections */
	int		serv_status;	/* 0 : not created; 1 : running; 2 : terminated */
	
	pthread_mutex_t	clients_mtx;	/* Mutex to protect the list of clients connected to the thread */
	struct fd_list	clients;	/* The list of clients connecting to this server, which information is not yet known */
	
	char *		serv_name;	/* A string to identify this server */
};

/* Client (connected remote endpoint, not received CER yet) information */
struct client {
	struct fd_list	 chain;	/* link in the server's list of clients */
	
	struct cnxctx	*conn;	/* Parameters of the connection; sends its events to the ev fifo bellow */
	
	struct timespec	 ts;	/* Delay for receiving CER: INCNX_TIMEOUT */
	struct fifo	*ev;	/* Events of the connection -- allowed: TIMEOUT, ERROR (cnx, tls), MSG_RCV (CER, other=>error) */
	
	pthread_t	 cli_thr; /* connection state machine (simplified PSM) */
};

/* Parameter for the thread handling the new connected client, to avoid bloking the server thread */
struct cli_fast {
	struct server * serv;
	int		sock;
	sSS		ss;
	socklen_t	sslen;
};


/* This thread is called when a new client had just connected */
static void * handle_client_fast(void * arg)
{
	struct cli_fast * cf = arg;
	struct client * c = NULL;
	
	/* Name the current thread */
	ASSERT(arg);
	{
		char addr[128];
		int offset = snprintf(addr, sizeof(addr), "Srv %d/Cli %d : ", cf->serv->socket, cf->sock);
		int rc = getnameinfo((sSA *)&cf->ss, sizeof(sSS), addr + offset, sizeof(addr) - offset, NULL, 0, 0);
		if (rc)
			memcpy(addr + offset, gai_strerror(rc), sizeof(addr) - offset);
		
		if (TRACE_BOOL(INFO)) {
			fd_log_debug( "New connection %s, sock %d, from '%s'\n", cf->serv->serv_name, cf->sock, addr + offset);
		}
	
		fd_log_threadname ( addr );
	}
	
	/* Create a client structure */
	CHECK_MALLOC_DO( c = malloc(sizeof(struct client)), goto early_error );
	memset(c, 0, sizeof(struct client));
	fd_list_init(&c->chain, c);
	c->cli_thr = pthread_self();
	
	/* Create the connection context */
	CHECK_MALLOC_DO( c->conn = fd_cnx_init(cf->sock, cf->serv->proto), goto early_error );
	
	/* In case we are a secure server, handshake now */
	if (cf->serv->secur) {
		
		TODO("Continue");
	}
	
	/* Save the client in the list */
	CHECK_POSIX_DO( pthread_mutex_lock( &cf->serv->clients_mtx ), goto early_error );
	fd_list_insert_before(&cf->serv->clients, &c->chain);
	CHECK_POSIX_DO( pthread_mutex_unlock( &cf->serv->clients_mtx ), goto error );
	
	
	
	
early_error:
	TRACE_DEBUG(INFO, "Thread is detaching to die");
	pthread_detach(pthread_self());
	shutdown(cf->sock, SHUT_RDWR);
	free(cf);
	free(c);
error:	
	TRACE_DEBUG(INFO, "Thread is terminating");
	CHECK_FCT_DO(fd_event_send(fd_g_config->cnf_main_ev, FDEV_TERMINATE, NULL), );
	return NULL;
}

/* The thread for the server */
static void * serv_th(void * arg)
{
	struct server *sv = (struct server *)arg;
	struct cli_fast cf;
	
	CHECK_PARAMS_DO(sv, goto error);
	fd_log_threadname ( sv->serv_name );
	sv->serv_status = 1;
	
	memset(&cf, 0, sizeof(struct cli_fast));
	cf.serv = sv;
	
	
	/* Accept incoming connections */
	CHECK_SYS_DO(  listen(sv->socket, 5), goto error );
	
	do {
		struct cli_fast * ncf;
		pthread_t	  thr;
		
		/* Re-init socket size */
		cf.sslen = sizeof(sSS);
		
		/* Wait for a new client */
		CHECK_SYS_DO( cf.sock = accept(sv->socket, (sSA *)&cf.ss, &cf.sslen), goto error );
		
		TRACE_DEBUG(FULL, "New connection accepted");
		
		/* Create the copy for the client thread */
		CHECK_MALLOC_DO( ncf = malloc(sizeof(struct cli_fast)), goto error );
		memcpy(ncf, &cf, sizeof(struct cli_fast));
		
		/* Create the thread to handle the new incoming connection */
		CHECK_POSIX_DO( pthread_create( &thr /* we don't use it, but NULL is not standard */, NULL, handle_client_fast, ncf), goto error );
		
	} while (1);
	
error:	
	if (sv)
		sv->serv_status = 2;
	/* Send error signal to the daemon */
	TRACE_DEBUG(INFO, "An error occurred in server module! Thread is terminating...");
	CHECK_FCT_DO(fd_event_send(fd_g_config->cnf_main_ev, FDEV_TERMINATE, NULL), );

	return NULL;
}


/* Create a new server structure */
static struct server * new_serv( int proto, int secur, int socket )
{
	char buf[32];
	char * sn = NULL;
	struct server * new;
	
	/* Create the server debug name */
	buf[sizeof(buf) - 1] = '\0';
	snprintf(buf, sizeof(buf) - 1, "Serv %d (%s%s)", socket, IPPROTO_NAME( proto ),	secur ? "s" : "");
	CHECK_MALLOC_DO( sn = strdup(buf), return NULL );
	
	/* New server structure */
	CHECK_MALLOC_DO( new = malloc(sizeof(struct server)), return NULL );
	
	memset(new, 0, sizeof(struct server));
	fd_list_init(&new->chain, new);
	new->socket = socket;
	new->proto = proto;
	new->secur = secur;
	CHECK_POSIX_DO( pthread_mutex_init(&new->clients_mtx, NULL), return NULL );
	fd_list_init(&new->clients, new);
	
	new->serv_name = sn;
	
	return new;
}

/* Dump all servers information */
void fd_servers_dump()
{
	struct fd_list * li;
	
	fd_log_debug("Dumping servers list :\n");
	for (li = FD_SERVERS.next; li != &FD_SERVERS; li = li->next) {
		struct server * sv = (struct server *)li;
		fd_log_debug("  Serv '%s': %s(%d), %s, %s, %s\n", 
				sv->serv_name, 
				(sv->socket > 0) ? "Open" : "Closed", sv->socket,
				IPPROTO_NAME( sv->proto ),
				sv->secur ? "Secur" : "NotSecur",
				(sv->serv_status == 0) ? "Thread not created" :
				((sv->serv_status == 1) ? "Thread running" :
				((sv->serv_status == 2) ? "Thread terminated" :
							  "Thread status unknown")));
		/* Dump the endpoints ? */
		/* Dump the client list ? */
	}
}

/* Start all the servers */
int fd_servers_start()
{
	int  socket;
	struct server * sv;
	
	/* SCTP */
	if (!fd_g_config->cnf_flags.no_sctp) {
#ifdef DISABLE_SCTP
		ASSERT(0);
#else /* DISABLE_SCTP */
		
		/* Create the server on default port */
		CHECK_FCT( fd_sctp_create_bind_server( &socket, fd_g_config->cnf_port ) );
		CHECK_MALLOC( sv = new_serv(IPPROTO_SCTP, 0, socket) );
		
		
		
		
#endif /* DISABLE_SCTP */
	}
	
	/* TCP */
	if (!fd_g_config->cnf_flags.no_tcp) {
		
		
	}
	
	return 0;
}

/* Terminate all the servers */
void fd_servers_stop()
{
	
}

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

/* Create and send a message, and receive it */

/* Note that we use both sessions and the argument to answer callback to pass the same value.
 * This is just for the purpose of checking everything went OK.
 */

#include "app_test.h"

static struct session_handler * atst_cli_reg = NULL;

struct atst_mess_info {
	int32_t		randval;	/* a random value to store in Test-AVP */
	struct timespec ts;		/* Time of sending the message */
} ;

/* Cb called when an answer is received */
static void atst_cb_ans(void * data, struct msg ** msg)
{
	struct atst_mess_info * mi = NULL;
	struct timespec ts;
	struct session * sess;
	struct avp * avp;
	struct avp_hdr * hdr;
	
	CHECK_SYS_DO( clock_gettime(CLOCK_REALTIME, &ts), return );

	/* Search the session, retrieve its data */
	{
		int new;
		CHECK_FCT_DO( fd_msg_sess_get(fd_g_config->cnf_dict, *msg, &sess, &new), return );
		ASSERT( new == 0 );
		
		CHECK_FCT_DO( fd_sess_state_retrieve( atst_cli_reg, sess, &mi ), return );
		TRACE_DEBUG( INFO, "%p %p", mi, data);
		ASSERT( (void *)mi == data );
	}
	
	/* Now log content of the answer */
	fprintf(stderr, "RECV ");
	
	/* Value of Test-AVP */
	CHECK_FCT_DO( fd_msg_search_avp ( *msg, atst_avp, &avp), return );
	if (avp) {
		CHECK_FCT_DO( fd_msg_avp_hdr( avp, &hdr ), return );
		fprintf(stderr, "%x (%s) ", hdr->avp_value->i32, (hdr->avp_value->i32 == mi->randval) ? "Ok" : "PROBLEM");
	} else {
		fprintf(stderr, "no_Test-AVP ");
	}
	
	/* Value of Result Code */
	CHECK_FCT_DO( fd_msg_search_avp ( *msg, atst_res_code, &avp), return );
	if (avp) {
		CHECK_FCT_DO( fd_msg_avp_hdr( avp, &hdr ), return );
		fprintf(stderr, "Status: %d ", hdr->avp_value->i32);
	} else {
		fprintf(stderr, "no_Result-Code ");
	}
	
	/* Value of Origin-Host */
	CHECK_FCT_DO( fd_msg_search_avp ( *msg, atst_origin_host, &avp), return );
	if (avp) {
		CHECK_FCT_DO( fd_msg_avp_hdr( avp, &hdr ), return );
		fprintf(stderr, "From '%.*s' ", hdr->avp_value->os.len, hdr->avp_value->os.data);
	} else {
		fprintf(stderr, "no_Origin-Host ");
	}
	
	/* Value of Origin-Realm */
	CHECK_FCT_DO( fd_msg_search_avp ( *msg, atst_origin_realm, &avp), return );
	if (avp) {
		CHECK_FCT_DO( fd_msg_avp_hdr( avp, &hdr ), return );
		fprintf(stderr, "('%.*s') ", hdr->avp_value->os.len, hdr->avp_value->os.data);
	} else {
		fprintf(stderr, "no_Origin-Realm ");
	}
	
	/* Now compute how long it took */
	if (ts.tv_nsec > mi->ts.tv_nsec) {
		fprintf(stderr, "in %d.%06ld sec", 
				(int)(ts.tv_sec - mi->ts.tv_sec),
				(long)(ts.tv_nsec - mi->ts.tv_nsec) / 1000);
	} else {
		fprintf(stderr, "in %d.%06ld sec", 
				(int)(ts.tv_sec + 1 - mi->ts.tv_sec),
				(long)(1000000000 + ts.tv_nsec - mi->ts.tv_nsec) / 1000);
	}
	
	fprintf(stderr, "\n");
	fflush(stderr);
	
	/* Free the message */
	CHECK_FCT_DO(fd_msg_free(*msg), return);
	*msg = NULL;
	
	free(mi);
	
	return;
}

/* Create a test message */
static void atst_cli_test_message(void)
{
	struct msg * req = NULL;
	struct avp * avp;
	union avp_value val;
	struct atst_mess_info * mi = NULL;
	struct session *sess = NULL;
	
	TRACE_DEBUG(FULL, "Creating a new message for sending.");
	
	/* Create the request from template */
	CHECK_FCT_DO( fd_msg_new( atst_cmd_r, MSGFL_ALLOC_ETEID, &req ), return );
	
	/* Create a new session */
	CHECK_FCT_DO( fd_sess_new( &sess, fd_g_config->cnf_diamid, "app_test", 8 ), return );
	
	/* Create the random value to store with the session */
	mi = malloc(sizeof(struct atst_mess_info));
	if (mi == NULL) {
		fd_log_debug("malloc failed: %s", strerror(errno));
		return;
	}
	
	mi->randval = (int32_t)random();
	
	/* Now set all AVPs values */
	
	/* Session-Id */
	{
		char * sid;
		CHECK_FCT_DO( fd_sess_getsid ( sess, &sid ), return );
		CHECK_FCT_DO( fd_msg_avp_new ( atst_sess_id, 0, &avp ), return );
		val.os.data = sid;
		val.os.len  = strlen(sid);
		CHECK_FCT_DO( fd_msg_avp_setvalue( avp, &val ), return );
		CHECK_FCT_DO( fd_msg_avp_add( req, MSG_BRW_FIRST_CHILD, avp ), return );
		
	}
	
	/* Set the Destination-Realm AVP */
	{
		CHECK_FCT_DO( fd_msg_avp_new ( atst_dest_realm, 0, &avp ), return  );
		val.os.data = (unsigned char *)(atst_conf->dest_realm);
		val.os.len  = strlen(atst_conf->dest_realm);
		CHECK_FCT_DO( fd_msg_avp_setvalue( avp, &val ), return  );
		CHECK_FCT_DO( fd_msg_avp_add( req, MSG_BRW_LAST_CHILD, avp ), return  );
	}
	
	/* Set the Destination-Host AVP if needed*/
	if (atst_conf->dest_host) {
		CHECK_FCT_DO( fd_msg_avp_new ( atst_dest_host, 0, &avp ), return  );
		val.os.data = (unsigned char *)(atst_conf->dest_host);
		val.os.len  = strlen(atst_conf->dest_host);
		CHECK_FCT_DO( fd_msg_avp_setvalue( avp, &val ), return  );
		CHECK_FCT_DO( fd_msg_avp_add( req, MSG_BRW_LAST_CHILD, avp ), return  );
	}
	
	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO( fd_msg_add_origin ( req, 0 ), return  );
	
	
	/* Set the Test-AVP AVP */
	{
		CHECK_FCT_DO( fd_msg_avp_new ( atst_avp, 0, &avp ), return  );
		val.i32 = mi->randval;
		CHECK_FCT_DO( fd_msg_avp_setvalue( avp, &val ), return  );
		CHECK_FCT_DO( fd_msg_avp_add( req, MSG_BRW_LAST_CHILD, avp ), return  );
	}
	
	CHECK_SYS_DO( clock_gettime(CLOCK_REALTIME, &mi->ts), return );
	
	/* Store this value in the session */
	CHECK_FCT_DO( fd_sess_state_store ( atst_cli_reg, sess, &mi ), return ); 
	
	/* Log sending the message */
	fprintf(stderr, "SEND %x to '%s' (%s)\n", mi->randval, atst_conf->dest_realm, atst_conf->dest_host?:"-" );
	fflush(stderr);
	
	/* Send the request */
	CHECK_FCT_DO( fd_msg_send( &req, atst_cb_ans, mi ), return );
	
	return;
}

int atst_cli_init(void)
{
	CHECK_FCT( fd_sess_handler_create(&atst_cli_reg, free) );
	
	CHECK_FCT( atst_sig_init(atst_cli_test_message) );
	
	return 0;
}

void atst_cli_fini(void)
{
	(void) fd_sess_handler_destroy(&atst_cli_reg);
	
	atst_sig_fini();
	
	return;
};

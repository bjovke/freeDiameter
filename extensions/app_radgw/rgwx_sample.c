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

/* Sample radius/diameter gateway plugin, for developers to see the structure of a plugin. */

#include "rgw_common.h"

/* The state of this extension */
struct rgwp_config {
	/* In a real extension, we would store the parsed configuration file, and the states of the extension */
	int state;
};

/* The function called at plugin initialization */
static struct rgwp_config * sample_conf_parse ( char * conf_file )
{
	struct rgwp_config * ret = NULL;
	
	TRACE_ENTRY("%p", conf_file);
	
	CHECK_MALLOC_DO( ret = malloc(sizeof(struct rgwp_config)), return NULL );
	
	ret->state = 1;
	
	return ret;
}

/* This function is called when the plugin is unloaded, to cleanup all the states */
static void sample_conf_free(struct rgwp_config * cs)
{
	TRACE_ENTRY("%p", cs);
	CHECK_PARAMS_DO( cs, );
	free(cs);
	return;
}

/* This function is called on incoming RADIUS messages. It should handle (some) RADIUS data and store into the Diameter message. */
static int sample_rad_req( struct rgwp_config * cs, struct session * session, struct radius_msg * rad_req, struct radius_msg ** rad_ans, struct msg ** diam_fw, struct rgw_client * cli )
{
	TRACE_ENTRY("%p %p %p %p %p %p", cs, session, rad_req, rad_ans, diam_fw, cli);
	CHECK_PARAMS(cs);
	TRACE_DEBUG(INFO, "RADIUS/Diameter Sample plugin received a new RADIUS message.");
	return 0;
}

/* This function is called when a Diameter answer is coming back. It should remove the AVPs and add the attributes in the RADIUS message. */
static int sample_diam_ans( struct rgwp_config * cs, struct session * session, struct msg ** diam_ans, struct radius_msg ** rad_fw, struct rgw_client * cli )
{
	TRACE_ENTRY("%p %p %p %p %p", cs, session, diam_ans, rad_fw, cli);
	CHECK_PARAMS(cs);
	TRACE_DEBUG(INFO, "RADIUS/Diameter Sample plugin received a new Diameter answer.");
	return 0;
}


/* Finally, we declare the structure that will be loaded by main RADIUS/Diameter gateway extension */
struct rgw_api rgwp_descriptor = {
	sample_conf_parse,
	sample_conf_free,
	sample_rad_req,
	sample_diam_ans
};	

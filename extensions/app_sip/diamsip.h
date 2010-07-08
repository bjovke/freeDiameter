/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Alexandre Westfahl <awestfahl@freediameter.net>						 *
*													 *
* Copyright (c) 2010, Alexandre Westfahl, Teraoka Laboratory (Keio University), and the WIDE Project. 	 *		
*													 *
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
* * Neither the name of the Teraoka Laboratory nor the 							 *
*   names of its contributors may be used to endorse or 						 *
*   promote products derived from this software without 						 *
*   specific prior written permission of Teraoka Laboratory 						 *
*   													 *
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
#include <freeDiameter/extension.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <gcrypt.h>
#include <string.h>
#include <mysql.h>
#include "md5.h"


#define NONCE_SIZE 16
#define DIGEST_LEN 16


/* Mode for the extension */
#define MODE_DSSERVER	0x1
#define	MODE_SL	0x2


/* The module configuration */
struct as_conf {
	int		mode;		/* default MODE_DSSERVER | MODE_SL */
	enum {ASMYSQL} datasource; 
	char * mysql_login;
	char * mysql_password;
	char * mysql_database;
	char * mysql_server;
	uint16_t  mysql_port;
	
};
extern struct as_conf * as_conf;

/* Parse the configuration file */
int as_conf_handle(char * conffile);


extern MYSQL *conn;



void calc_md5(char *buffer, char * data);
void clear_digest(uint8_t * digest, char * readable_digest, int digestlength);
struct avp_hdr * walk_digest(struct avp *avp, int avp_code);
int start_mysql_connection();
void request_mysql(char *query);
void close_mysql_connection();

void DigestCalcHA1(char * pszAlg,char * pszUserName,char * pszRealm,char * pszPassword,char * pszNonce,char * pszCNonce,HASHHEX SessionKey);
void DigestCalcResponse(HASHHEX HA1,char * pszNonce,char * pszNonceCount,char * pszCNonce,char * pszQop,char * pszMethod,char * pszDigestUri,HASHHEX HEntity,HASHHEX Response);
void DigestCalcResponseAuth(HASHHEX HA1,char * pszNonce,char * pszNonceCount,char * pszCNonce,char * pszQop,char * pszMethod,char * pszDigestUri,HASHHEX HEntity,HASHHEX Response);

int fd_avp_search_avp ( struct avp * groupedavp, struct dict_object * what, struct avp ** avp );

int ds_entry();
void fd_ext_fini(void);
int diamsip_default_cb( struct msg ** msg, struct avp * avp, struct session * sess, enum disp_action * act);
int diamsip_MAR_cb( struct msg ** msg, struct avp * avp, struct session * sess, enum disp_action * act);
int diamsip_RTA_cb( struct msg ** msg, struct avp * avp, struct session * sess, enum disp_action * act);
#define SQL_GETPASSWORD "SELECT `password` FROM ds_users WHERE `username` ='%s'"
#define SQL_GETPASSWORD_LEN 52

#define SQL_GETSIPURI "SELECT `sip_server_uri` FROM ds_users WHERE `username` ='%s'"
#define SQL_GETSIPURI_LEN 60

#define SQL_SETSIPURI "UPDATE ds_users SET `sip_server_uri`='%s', `flag`=1 WHERE `username` ='%s'"
#define SQL_SETSIPURI_LEN 74

#define SQL_GETSIPAOR "SELECT `sip_aor` FROM `ds_sip_aor`, `ds_users` WHERE `ds_sip_aor`.`id_user` = `ds_users`.`id_user` AND `ds_users`.`username` = '%s'"
#define SQL_GETSIPAOR_LEN 131

#define SQL_CLEARFLAG "UPDATE ds_users SET `flag`=0 WHERE `username` ='%s'"
#define SQL_CLEARFLAG_LEN 74

extern struct session_handler * ds_sess_hdl;



struct ds_nonce
{
	char *nonce;
};

//Storage for some usefull AVPs
struct diamsip_dict{
	struct dict_object * Auth_Session_State;
	struct dict_object * Auth_Application_Id;
	struct dict_object * User_Name;
	struct dict_object * SIP_Auth_Data_Item;
	struct dict_object * SIP_Authorization;
	struct dict_object * SIP_Authenticate;
	struct dict_object * SIP_Number_Auth_Items;	
	struct dict_object * SIP_Authentication_Scheme;
	struct dict_object * SIP_Authentication_Info;	
	struct dict_object * SIP_Server_URI;
	struct dict_object * SIP_Method;
	struct dict_object * SIP_AOR;
	struct dict_object * Digest_URI;		
	struct dict_object * Digest_Nonce;
	struct dict_object * Digest_Nonce_Count;
	struct dict_object * Digest_CNonce;		
	struct dict_object * Digest_Realm;		
	struct dict_object * Digest_Response;	
	struct dict_object * Digest_Response_Auth;	
	struct dict_object * Digest_Username;	
	struct dict_object * Digest_Method;
	struct dict_object * Digest_QOP;	
	struct dict_object * Digest_Algorithm;
	struct dict_object * Digest_HA1;
};

extern  struct diamsip_dict  sip_dict;

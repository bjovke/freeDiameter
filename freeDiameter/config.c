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

#include "fD.h"
#include <sys/stat.h>

/* Configuration management */

#ifndef GNUTLS_DEFAULT_PRIORITY
# define GNUTLS_DEFAULT_PRIORITY "NORMAL"
#endif /* GNUTLS_DEFAULT_PRIORITY */
#ifndef GNUTLS_DEFAULT_DHBITS
# define GNUTLS_DEFAULT_DHBITS 1024
#endif /* GNUTLS_DEFAULT_DHBITS */

/* Initialize the fd_g_config structure to default values */
int fd_conf_init()
{
	TRACE_ENTRY();
	
	fd_g_config->cnf_eyec = EYEC_CONFIG;
	
	fd_g_config->cnf_timer_tc = 30;
	fd_g_config->cnf_timer_tw = 30;
	
	fd_g_config->cnf_port     = 3868;
	fd_g_config->cnf_port_tls = 3869;
	fd_g_config->cnf_sctp_str = 30;
	fd_g_config->cnf_dispthr  = 4;
	fd_list_init(&fd_g_config->cnf_endpoints, NULL);
	fd_list_init(&fd_g_config->cnf_apps, NULL);
	#ifdef DISABLE_SCTP
	fd_g_config->cnf_flags.no_sctp = 1;
	#endif /* DISABLE_SCTP */
	
	fd_g_config->cnf_orstateid = (uint32_t) time(NULL);
	
	CHECK_FCT( fd_dict_init(&fd_g_config->cnf_dict) );
	CHECK_FCT( fd_fifo_new(&fd_g_config->cnf_main_ev) );
	
	/* TLS parameters */
	CHECK_GNUTLS_DO( gnutls_certificate_allocate_credentials (&fd_g_config->cnf_sec_data.credentials), return ENOMEM );
	CHECK_GNUTLS_DO( gnutls_dh_params_init (&fd_g_config->cnf_sec_data.dh_cache), return ENOMEM );

	return 0;
}

void fd_conf_dump()
{
	if (!TRACE_BOOL(INFO))
		return;
	
	fd_log_debug("-- Configuration :\n");
	fd_log_debug("  Debug trace level ...... : %+d\n", fd_g_debug_lvl);
	fd_log_debug("  Configuration file ..... : %s\n", fd_g_config->cnf_file);
	fd_log_debug("  Diameter Identity ...... : %s (l:%Zi)\n", fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
	fd_log_debug("  Diameter Realm ......... : %s (l:%Zi)\n", fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
	fd_log_debug("  Tc Timer ............... : %u\n", fd_g_config->cnf_timer_tc);
	fd_log_debug("  Tw Timer ............... : %u\n", fd_g_config->cnf_timer_tw);
	fd_log_debug("  Local port ............. : %hu\n", fd_g_config->cnf_port);
	fd_log_debug("  Local secure port ...... : %hu\n", fd_g_config->cnf_port_tls);
	fd_log_debug("  Number of SCTP streams . : %hu\n", fd_g_config->cnf_sctp_str);
	fd_log_debug("  Number of server threads : %hu\n", fd_g_config->cnf_dispthr);
	if (FD_IS_LIST_EMPTY(&fd_g_config->cnf_endpoints)) {
		fd_log_debug("  Local endpoints ........ : Default (use all available)\n");
	} else {
		fd_log_debug("  Local endpoints ........ : \n");
		fd_ep_dump( 29, &fd_g_config->cnf_endpoints );
	}
	if (FD_IS_LIST_EMPTY(&fd_g_config->cnf_apps)) {
		fd_log_debug("  Local applications ..... : (none)\n");
	} else {
		struct fd_list * li = fd_g_config->cnf_apps.next;
		fd_log_debug("  Local applications ..... : ");
		while (li != &fd_g_config->cnf_apps) {
			struct fd_app * app = (struct fd_app *)li;
			if (li != fd_g_config->cnf_apps.next) fd_log_debug("                             ");
			fd_log_debug("App: %u\t%s%s\tVnd: %u\n", 
					app->appid,
					app->flags.auth ? "Au" : "--",
					app->flags.acct ? "Ac" : "--",
					app->vndid);
			li = li->next;
		}
	}
	
	fd_log_debug("  Flags : - IP ........... : %s\n", fd_g_config->cnf_flags.no_ip4 ? "DISABLED" : "Enabled");
	fd_log_debug("          - IPv6 ......... : %s\n", fd_g_config->cnf_flags.no_ip6 ? "DISABLED" : "Enabled");
	fd_log_debug("          - Relay app .... : %s\n", fd_g_config->cnf_flags.no_fwd ? "DISABLED" : "Enabled");
	fd_log_debug("          - TCP .......... : %s\n", fd_g_config->cnf_flags.no_tcp ? "DISABLED" : "Enabled");
	#ifdef DISABLE_SCTP
	fd_log_debug("          - SCTP ......... : DISABLED (at compilation)\n");
	#else /* DISABLE_SCTP */
	fd_log_debug("          - SCTP ......... : %s\n", fd_g_config->cnf_flags.no_sctp ? "DISABLED" : "Enabled");
	#endif /* DISABLE_SCTP */
	fd_log_debug("          - Pref. proto .. : %s\n", fd_g_config->cnf_flags.pr_tcp ? "TCP" : "SCTP");
	fd_log_debug("          - TLS method ... : %s\n", fd_g_config->cnf_flags.tls_alg ? "INBAND" : "Separate port");
	
	fd_log_debug("  TLS :   - Certificate .. : %s\n", fd_g_config->cnf_sec_data.cert_file ?: "(NONE)");
	fd_log_debug("          - Private key .. : %s\n", fd_g_config->cnf_sec_data.key_file ?: "(NONE)");
	fd_log_debug("          - CA (trust) ... : %s (%d certs)\n", fd_g_config->cnf_sec_data.ca_file ?: "(none)", fd_g_config->cnf_sec_data.ca_file_nr);
	fd_log_debug("          - CRL .......... : %s\n", fd_g_config->cnf_sec_data.crl_file ?: "(none)");
	fd_log_debug("          - Priority ..... : %s\n", fd_g_config->cnf_sec_data.prio_string ?: "(default: '" GNUTLS_DEFAULT_PRIORITY "')");
	if (fd_g_config->cnf_sec_data.dh_file)
		fd_log_debug("          - DH file ...... : %s\n", fd_g_config->cnf_sec_data.dh_file);
	else
		fd_log_debug("          - DH bits ...... : %d\n", fd_g_config->cnf_sec_data.dh_bits ?: GNUTLS_DEFAULT_DHBITS);
	
	fd_log_debug("  Origin-State-Id ........ : %u\n", fd_g_config->cnf_orstateid);
}

/* Parse the configuration file (using the yacc parser) */
int fd_conf_parse()
{
	extern FILE * fddin;
	
	/* Attempt to find the configuration file */
	if (!fd_g_config->cnf_file)
		fd_g_config->cnf_file = FD_DEFAULT_CONF_FILENAME;
	
	fddin = fopen(fd_g_config->cnf_file, "r");
	if ((fddin == NULL) && (*fd_g_config->cnf_file != '/')) {
		/* We got a relative path, attempt to add the default directory prefix */
		char * bkp = fd_g_config->cnf_file;
		CHECK_MALLOC( fd_g_config->cnf_file = malloc(strlen(bkp) + strlen(DEFAULT_CONF_PATH) + 2) ); /* we will not free it, but not important */
		sprintf( fd_g_config->cnf_file, DEFAULT_CONF_PATH "/%s", bkp );
		fddin = fopen(fd_g_config->cnf_file, "r");
	}
	if (fddin == NULL) {
		int ret = errno;
		fprintf(stderr, "Unable to open configuration file %s for reading: %s\n", fd_g_config->cnf_file, strerror(ret));
		return ret;
	}
	
	/* call yacc parser */
	TRACE_DEBUG (FULL, "Parsing configuration file: %s", fd_g_config->cnf_file);
	CHECK_FCT(  fddparse(fd_g_config)  );
	
	/* close the file */
	fclose(fddin);
	
	/* Check that TLS private key was given */
	if (! fd_g_config->cnf_sec_data.key_file) {
		fprintf(stderr, "Missing private key configuration for TLS. Please provide the TLS_cred configuration directive.\n");
		return EINVAL;
	}
	
	/* Resolve hostname if not provided */
	if (fd_g_config->cnf_diamid == NULL) {
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 1024
#endif /* HOST_NAME_MAX */
		char buf[HOST_NAME_MAX + 1];
		struct addrinfo hints, *info;
		int ret;
		
		/* local host name */
		CHECK_SYS(gethostname(buf, sizeof(buf)));
		
		/* get FQDN */
		memset(&hints, 0, sizeof hints);
		hints.ai_flags = AI_CANONNAME;

		ret = getaddrinfo(buf, NULL, &hints, &info);
		if (ret != 0) {
			fprintf(stderr, "Error resolving local FQDN :\n"
					" '%s' : %s\n"
					"Please provide Identity in configuration file.\n",
					buf, gai_strerror(ret));
			return EINVAL;
		}
		CHECK_MALLOC( fd_g_config->cnf_diamid = strdup(info->ai_canonname) );
		freeaddrinfo(info);
	}
	
	/* cache the length of the diameter id for the session module */
	fd_g_config->cnf_diamid_len = strlen(fd_g_config->cnf_diamid);
	
	/* Handle the realm part */
	if (fd_g_config->cnf_diamrlm == NULL) {
		char * start = NULL;
		
		/* Check the diameter identity is a fqdn */
		start = strchr(fd_g_config->cnf_diamid, '.');
		if ((start == NULL) || (start[1] == '\0')) {
			fprintf(stderr, "Unable to extract realm from the Identity '%s'.\n"
					"Please fix your Identity setting or provide Realm.\n",
					fd_g_config->cnf_diamid);
			return EINVAL;
		}		
		
		CHECK_MALLOC( fd_g_config->cnf_diamrlm = strdup( start + 1 )  ); 
	}
	fd_g_config->cnf_diamrlm_len = strlen(fd_g_config->cnf_diamrlm);
	
	/* Validate some flags */
	if (fd_g_config->cnf_flags.no_ip4 && fd_g_config->cnf_flags.no_ip6) {
		fprintf(stderr, "IP and IPv6 cannot be disabled at the same time.\n");
		return EINVAL;
	}
	if (fd_g_config->cnf_flags.no_tcp && fd_g_config->cnf_flags.no_sctp) {
		fprintf(stderr, "TCP and SCTP cannot be disabled at the same time.\n");
		return EINVAL;
	}
	
	/* Validate local endpoints */
	if ((!FD_IS_LIST_EMPTY(&fd_g_config->cnf_endpoints)) && (fd_g_config->cnf_flags.no_ip4 || fd_g_config->cnf_flags.no_ip6)) {
		struct fd_list * li;
		for ( li = fd_g_config->cnf_endpoints.next; li != &fd_g_config->cnf_endpoints; li = li->next) {
			struct fd_endpoint * ep = (struct fd_endpoint *)li;
			if ( (fd_g_config->cnf_flags.no_ip4 && (ep->sa.sa_family == AF_INET))
			   ||(fd_g_config->cnf_flags.no_ip6 && (ep->sa.sa_family == AF_INET6)) ) {
				li = li->prev;
				fd_list_unlink(&ep->chain);
				if (TRACE_BOOL(INFO)) {
					fd_log_debug("Info: Removing local address conflicting with the flags no_IP / no_IP6 : ");
					sSA_DUMP_NODE( &ep->sa, NI_NUMERICHOST );
					fd_log_debug("\n");
				}
				free(ep);
			}
		}
	}
	
	/* Configure TLS default parameters */
	if (! fd_g_config->cnf_sec_data.prio_string) {
		const char * err_pos = NULL;
		CHECK_GNUTLS_DO( gnutls_priority_init( 
					&fd_g_config->cnf_sec_data.prio_cache,
					GNUTLS_DEFAULT_PRIORITY,
					&err_pos),
				 { TRACE_DEBUG(INFO, "Error in priority string at position : %s", err_pos); return EINVAL; } );
	}
	
	/* Verify that our certificate is valid -- otherwise remote peers will reject it */
	{
		int ret = 0, i;
		
		gnutls_datum_t certfile;
		size_t alloc = 0;
		
		gnutls_x509_crt_t * certs = NULL;
		unsigned int cert_max = 0;
		
		gnutls_x509_crt_t * CA_list;
		int CA_list_length;
		
		gnutls_x509_crl_t * CRL_list;
		int CRL_list_length;
		
		unsigned int verify;
		time_t now;
		
		memset(&certfile, 0, sizeof(certfile));
		
		/* Read the certificate file */
		FILE *stream = fopen (fd_g_config->cnf_sec_data.cert_file, "rb");
		if (!stream) {
			int err = errno;
			TRACE_DEBUG(INFO, "An error occurred while opening '%s': %s\n", fd_g_config->cnf_sec_data.cert_file, strerror(err));
			return err; 
		}
		do {
			uint8_t * realloced = NULL;
			size_t read = 0;
			
			if (alloc < certfile.size + BUFSIZ + 1) {
				alloc += alloc / 2 + BUFSIZ + 1;
				CHECK_MALLOC_DO( realloced = realloc(certfile.data, alloc),
					{
						free(certfile.data);
						return ENOMEM;
					} )
				certfile.data = realloced;
			}
			
			read = fread( certfile.data + certfile.size, 1, alloc - certfile.size - 1, stream );
			certfile.size += read;
			
			if (ferror(stream)) {
				int err = errno;
				TRACE_DEBUG(INFO, "An error occurred while reading '%s': %s\n", fd_g_config->cnf_sec_data.cert_file, strerror(err));
				return err; 
			}
		} while (!feof(stream));
		certfile.data[certfile.size] = '\0';
		fclose(stream);
		
		/* Import the certificate(s) */
		GNUTLS_TRACE( ret = gnutls_x509_crt_list_import(NULL, &cert_max, &certfile, GNUTLS_X509_FMT_PEM, GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED) );
		if (ret != GNUTLS_E_SHORT_MEMORY_BUFFER) {
			CHECK_GNUTLS_DO(ret, return EINVAL);
		}
		
		CHECK_MALLOC( certs = calloc(cert_max, sizeof(gnutls_x509_crt_t)) );
		CHECK_GNUTLS_DO( gnutls_x509_crt_list_import(certs, &cert_max, &certfile, GNUTLS_X509_FMT_PEM, 0),
			{
				TRACE_DEBUG(INFO, "Failed to import the data from file '%s'", fd_g_config->cnf_sec_data.cert_file);
				free(certfile.data);
				return EINVAL;
			} );
		free(certfile.data);
		
		ASSERT(cert_max >= 1);
		
		/* Now, verify the list against the local CA and CRL */
		GNUTLS_TRACE( gnutls_certificate_get_x509_cas (fd_g_config->cnf_sec_data.credentials, &CA_list, (unsigned int *) &CA_list_length) );
		GNUTLS_TRACE( gnutls_certificate_get_x509_crls (fd_g_config->cnf_sec_data.credentials, &CRL_list, (unsigned int *) &CRL_list_length) );
		CHECK_GNUTLS_DO( gnutls_x509_crt_list_verify(certs, cert_max, CA_list, CA_list_length, CRL_list, CRL_list_length, 0, &verify),
			{
				TRACE_DEBUG(INFO, "Failed to verify the local certificate '%s' against local credentials. Please check your certificate is valid.", fd_g_config->cnf_sec_data.cert_file);
				return EINVAL;
			} );
		if (verify) {
			fd_log_debug("TLS: Local certificate chain '%s' is invalid :\n", fd_g_config->cnf_sec_data.cert_file);
			if (verify & GNUTLS_CERT_INVALID)
				fd_log_debug(" - The certificate is not trusted (unknown CA? expired?)\n");
			if (verify & GNUTLS_CERT_REVOKED)
				fd_log_debug(" - The certificate has been revoked.\n");
			if (verify & GNUTLS_CERT_SIGNER_NOT_FOUND)
				fd_log_debug(" - The certificate hasn't got a known issuer.\n");
			if (verify & GNUTLS_CERT_SIGNER_NOT_CA)
				fd_log_debug(" - The certificate signer is not a CA, or uses version 1, or 3 without basic constraints.\n");
			if (verify & GNUTLS_CERT_INSECURE_ALGORITHM)
				fd_log_debug(" - The certificate signature uses a weak algorithm.\n");
			return EINVAL;
		}
	
		/* Check the local Identity is valid with the certificate */
		if (!gnutls_x509_crt_check_hostname (certs[0], fd_g_config->cnf_diamid)) {
			fd_log_debug("TLS: Local certificate '%s' is invalid :\n", fd_g_config->cnf_sec_data.cert_file);
			fd_log_debug(" - The certificate hostname does not match '%s'\n", fd_g_config->cnf_diamid);
			return EINVAL;
		}
		
		/* Check validity of all the certificates in the chain */
		now = time(NULL);
		for (i = 0; i < cert_max; i++)
		{
			time_t deadline;

			GNUTLS_TRACE( deadline = gnutls_x509_crt_get_expiration_time(certs[i]) );
			if ((deadline != (time_t)-1) && (deadline < now)) {
				fd_log_debug("TLS: Local certificate chain '%s' is invalid :\n", fd_g_config->cnf_sec_data.cert_file);
				fd_log_debug(" - The certificate %d in the chain is expired\n", i);
				return EINVAL;
			}

			GNUTLS_TRACE( deadline = gnutls_x509_crt_get_activation_time(certs[i]) );
			if ((deadline != (time_t)-1) && (deadline > now)) {
				fd_log_debug("TLS: Local certificate chain '%s' is invalid :\n", fd_g_config->cnf_sec_data.cert_file);
				fd_log_debug(" - The certificate %d in the chain is not yet activated\n", i);
				return EINVAL;
			}
		}
		
		/* Everything checked OK, free the certificate list */
		for (i = 0; i < cert_max; i++)
		{
			GNUTLS_TRACE( gnutls_x509_crt_deinit (certs[i]) );
		}
		free(certs);
	}
	
	
	/* gnutls_certificate_set_verify_limits -- so far the default values are fine... */
	
	/* DH */
	if (fd_g_config->cnf_sec_data.dh_file) {
		gnutls_datum_t dhparams = { NULL, 0 };
		size_t alloc = 0;
		FILE *stream = fopen (fd_g_config->cnf_sec_data.dh_file, "rb");
		if (!stream) {
			int err = errno;
			TRACE_DEBUG(INFO, "An error occurred while opening '%s': %s\n", fd_g_config->cnf_sec_data.dh_file, strerror(err));
			return err; 
		}
		do {
			uint8_t * realloced = NULL;
			size_t read = 0;
			
			if (alloc < dhparams.size + BUFSIZ + 1) {
				alloc += alloc / 2 + BUFSIZ + 1;
				CHECK_MALLOC_DO( realloced = realloc(dhparams.data, alloc),
					{
						free(dhparams.data);
						return ENOMEM;
					} )
				dhparams.data = realloced;
			}
			
			read = fread( dhparams.data + dhparams.size, 1, alloc - dhparams.size - 1, stream );
			dhparams.size += read;
			
			if (ferror(stream)) {
				int err = errno;
				TRACE_DEBUG(INFO, "An error occurred while reading '%s': %s\n", fd_g_config->cnf_sec_data.dh_file, strerror(err));
				return err; 
			}
		} while (!feof(stream));
		dhparams.data[dhparams.size] = '\0';
		fclose(stream);
		CHECK_GNUTLS_DO( gnutls_dh_params_import_pkcs3( 
					fd_g_config->cnf_sec_data.dh_cache,
					&dhparams,
					GNUTLS_X509_FMT_PEM),
					 { TRACE_DEBUG(INFO, "Error in DH bits value : %d", fd_g_config->cnf_sec_data.dh_bits ?: GNUTLS_DEFAULT_DHBITS); return EINVAL; } );
		free(dhparams.data);
		
	} else {
		TRACE_DEBUG(INFO, "Generating fresh Diffie-Hellman parameters of size %d (this takes some time)... ", fd_g_config->cnf_sec_data.dh_bits ?: GNUTLS_DEFAULT_DHBITS);
		CHECK_GNUTLS_DO( gnutls_dh_params_generate2( 
					fd_g_config->cnf_sec_data.dh_cache,
					fd_g_config->cnf_sec_data.dh_bits ?: GNUTLS_DEFAULT_DHBITS),
					 { TRACE_DEBUG(INFO, "Error in DH bits value : %d", fd_g_config->cnf_sec_data.dh_bits ?: GNUTLS_DEFAULT_DHBITS); return EINVAL; } );
	}			
	
	return 0;
}


/* Destroy contents of fd_g_config structure */
int fd_conf_deinit()
{
	TRACE_ENTRY();
	
	/* Free the TLS parameters */
	gnutls_priority_deinit(fd_g_config->cnf_sec_data.prio_cache);
	gnutls_dh_params_deinit(fd_g_config->cnf_sec_data.dh_cache);
	gnutls_certificate_free_credentials(fd_g_config->cnf_sec_data.credentials);
	
	free(fd_g_config->cnf_sec_data.cert_file); fd_g_config->cnf_sec_data.cert_file = NULL;
	free(fd_g_config->cnf_sec_data.key_file); fd_g_config->cnf_sec_data.key_file = NULL;
	free(fd_g_config->cnf_sec_data.ca_file); fd_g_config->cnf_sec_data.ca_file = NULL;
	free(fd_g_config->cnf_sec_data.crl_file); fd_g_config->cnf_sec_data.crl_file = NULL;
	free(fd_g_config->cnf_sec_data.prio_string); fd_g_config->cnf_sec_data.prio_string = NULL;
	free(fd_g_config->cnf_sec_data.dh_file); fd_g_config->cnf_sec_data.dh_file = NULL;
	
	/* Destroy dictionary */
	CHECK_FCT_DO( fd_dict_fini(&fd_g_config->cnf_dict), );
	
	/* Destroy the main event queue */
	CHECK_FCT_DO( fd_fifo_del(&fd_g_config->cnf_main_ev), );
	
	/* Destroy the local endpoints and applications */
	CHECK_FCT_DO(fd_ep_filter(&fd_g_config->cnf_endpoints, 0 ), );
	CHECK_FCT_DO(fd_app_empty(&fd_g_config->cnf_apps ), );
	
	/* Destroy the local identity */	
	free(fd_g_config->cnf_diamid); fd_g_config->cnf_diamid = NULL;
	free(fd_g_config->cnf_diamrlm); fd_g_config->cnf_diamrlm = NULL;
	
	return 0;
}



/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@nict.go.jp>							 *
*													 *
* Copyright (c) 2011, WIDE Project and NICT								 *
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

#define TEST_STR (os0_t)"This is my test string (with extra unused data)"

/* The following string contains UTF-8 encoded characters (Chinese characters) */
#define TEST_IDN_UTF8  "freeDiameter.中国"
#define TEST_IDN_CONV  "freeDiameter.xn--fiqs8s"

/* Main test routine */
int main(int argc, char *argv[])
{
	/* First, initialize the daemon modules */
	INIT_FD();
	
	/* Check the hash function */
	{
		uint8_t buf[30];
		
		uint32_t hash = fd_os_hash(TEST_STR, CONSTSTRLEN(TEST_STR)); /* reference value */
		
		/* Check that a hash of a substring / surstring is different */
		CHECK( 1, hash != fd_os_hash(TEST_STR, CONSTSTRLEN(TEST_STR) - 1) ? 1 : 0 );
		CHECK( 1, hash != fd_os_hash(TEST_STR, CONSTSTRLEN(TEST_STR) + 1) ? 1 : 0 );
		
		/* Check alignment of the string is not important */
		memcpy(buf + 4, TEST_STR, CONSTSTRLEN(TEST_STR));
		CHECK( hash, fd_os_hash(buf + 4, CONSTSTRLEN(TEST_STR)) );
		
		memcpy(buf + 3, TEST_STR, CONSTSTRLEN(TEST_STR));
		CHECK( hash, fd_os_hash(buf + 3, CONSTSTRLEN(TEST_STR)) );
		
		memcpy(buf + 2, TEST_STR, CONSTSTRLEN(TEST_STR));
		CHECK( hash, fd_os_hash(buf + 2, CONSTSTRLEN(TEST_STR)) );
		
		memcpy(buf + 1, TEST_STR, CONSTSTRLEN(TEST_STR));
		CHECK( hash, fd_os_hash(buf + 1, CONSTSTRLEN(TEST_STR)) );
	}
	
	/* Check the Diameter Identity functions */
	{
		char * res;
		size_t len;
		
		/* A valid ASCII domain name */
		res = TEST_IDN_CONV;
		CHECK( 0, fd_os_validate_DiameterIdentity(&res, &len, 1) );
		CHECK( 0, strcasecmp(res, TEST_IDN_CONV) ); /* the function does not change a valid DN */
		CHECK( 0, fd_os_validate_DiameterIdentity(&res, &len, 0) );
		CHECK( 0, strcasecmp(res, TEST_IDN_CONV) );
		CHECK( CONSTSTRLEN(TEST_IDN_CONV), len );
		free(res);
		
		/* Now, an invalid string */
		res = TEST_IDN_UTF8;
		
		#ifdef DIAMID_IDNA_IGNORE
		
		/* The UTF-8 chars are considered valid */
		CHECK( 1, fd_os_is_valid_DiameterIdentity((os0_t)TEST_IDN_UTF8, CONSTSTRLEN(TEST_IDN_UTF8) );
		
		/* The string should be passed unmodified */
		CHECK( 0, fd_os_validate_DiameterIdentity(&res, &len, 1) );
		CHECK( 0, strcasecmp(res, TEST_IDN_UTF8) );
		CHECK( 0, fd_os_cmp(res, len, TEST_IDN_UTF8, CONSTSTRLEN(TEST_IDN_UTF8)) );
		CHECK( 0, fd_os_almostcasecmp(res, len, TEST_IDN_UTF8, CONSTSTRLEN(TEST_IDN_UTF8)) );
		CHECK( 0, fd_os_validate_DiameterIdentity(&res, &len, 0) );
		CHECK( 0, strcasecmp(res, TEST_IDN_UTF8) );
		CHECK( CONSTSTRLEN(TEST_IDN_UTF8), len );
		free(res);
		
		#else /* DIAMID_IDNA_IGNORE */
		
		/* The UTF-8 chars are recognized as invalid DiameterIdentity */
		CHECK( 0, fd_os_is_valid_DiameterIdentity((os0_t)TEST_IDN_UTF8, CONSTSTRLEN(TEST_IDN_UTF8) ));
		
		# ifdef DIAMID_IDNA_REJECT
		
		/* The string must be rejected */
		CHECK( EINVAL, fd_os_validate_DiameterIdentity(&res, &len, 1) );
		
		# else /* DIAMID_IDNA_REJECT */
		
		/* The string should be transformed into TEST_IDN_CONV */
		CHECK( 0, fd_os_validate_DiameterIdentity(&res, &len, 1) );
		CHECK( 0, strcasecmp(res, TEST_IDN_CONV) );
		CHECK( CONSTSTRLEN(TEST_IDN_CONV), len );
		free(res);
		
		# endif /* DIAMID_IDNA_REJECT */
		#endif /* DIAMID_IDNA_IGNORE */

	}
	
	/* That's all for the tests yet */
	PASSTEST();
} 
	

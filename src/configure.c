/*
  Copyright (c) 2012, Matthias Schiffer <mschiffer@universe-factory.net>
  Partly based on QuickTun Copyright (c) 2010, Ivo Smits <Ivo@UCIS.nl>.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "fastd.h"
#include "peer.h"

#include <config.h>

#include <arpa/inet.h>
#include <stdarg.h>


extern fastd_method fastd_method_null;

#ifdef WITH_METHOD_ECFXP
extern fastd_method fastd_method_ec25519_fhmqvc_xsalsa20_poly1305;
#endif


static void default_config(fastd_config *conf) {
	conf->loglevel = LOG_DEBUG;

	conf->peer_stale_time = 300;
	conf->peer_stale_time_temp = 30;
	conf->eth_addr_stale_time = 300;

	conf->ifname = NULL;

	memset(&conf->bind_addr_in, 0, sizeof(struct sockaddr_in));
	conf->bind_addr_in.sin_family = AF_UNSPEC;
	conf->bind_addr_in.sin_port = htons(1337);
	conf->bind_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);

	memset(&conf->bind_addr_in6, 0, sizeof(struct sockaddr_in6));
	conf->bind_addr_in6.sin6_family = AF_UNSPEC;
	conf->bind_addr_in6.sin6_port = htons(1337);
	conf->bind_addr_in6.sin6_addr = in6addr_any;

	conf->mtu = 1500;
	conf->protocol = PROTOCOL_ETHERNET;
	conf->method = &fastd_method_null;
	conf->peers = NULL;
}

static bool config_match(const char *opt, ...) {
	va_list ap;
	bool match = false;
	const char *str;
	
	va_start(ap, opt);

	while((str = va_arg(ap, const char*)) != NULL) {
		if (strcmp(opt, str) == 0) {
			match = true;
			break;
		}
	}

	va_end(ap);

	return match;
}

#define IF_OPTION(args...) if(config_match(argv[i], args, NULL) && (++i))
#define IF_OPTION_ARG(args...) if(config_match(argv[i], args, NULL) && ({ \
				arg = argv[i+1];			\
				i+=2;					\
				if (i > argc)				\
					exit_error(ctx, "config error: option `%s' needs an argument", argv[i-2]); \
				true;					\
			}))
#define IGNORE_OPTION (i++)

void fastd_configure(fastd_context *ctx, fastd_config *conf, int argc, char *const argv[]) {
	default_config(conf);

	fastd_peer_config **current_peer = &conf->peers;

	int i = 1;
	const char *arg;
	long l;
	char *charptr;
	char *endptr;
	char *addrstr;

	bool v4_peers = false, v6_peers = false;


	conf->n_floating = 0;


	while (i < argc) {
		IF_OPTION_ARG("-i", "--interface") {
			conf->ifname = arg;
			continue;
		}

		IF_OPTION_ARG("-b", "--bind") {
			if (arg[0] == '[') {
				charptr = strchr(arg, ']');
				if (!charptr || (charptr[1] != ':' && charptr[1] != '\0'))
					exit_error(ctx, "invalid bind address `%s'", arg);

				addrstr = strndup(arg+1, charptr-arg-1);
			
				if (charptr[1] == ':')
					charptr++;
				else
					charptr = NULL;
			}
			else {
				charptr = strchr(arg, ':');
				if (charptr) {
					addrstr = strndup(arg, charptr-arg);
				}
				else {
					addrstr = strdup(arg);
				}
			}

			if (charptr) {
				l = strtol(charptr+1, &endptr, 10);
				if (*endptr || l > 65535)
					exit_error(ctx, "invalid bind port `%s'", charptr+1);
			}

			if (strcmp(addrstr, "any") == 0) {
				conf->bind_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
				conf->bind_addr_in.sin_port = htons(l);

				conf->bind_addr_in6.sin6_addr = in6addr_any;
				conf->bind_addr_in6.sin6_port = htons(l);
			}
			else if (arg[0] == '[') {
				conf->bind_addr_in6.sin6_family = AF_INET6;
				if (inet_pton(AF_INET6, addrstr, &conf->bind_addr_in6.sin6_addr) != 1)
					exit_error(ctx, "invalid bind address `%s'", addrstr);
				conf->bind_addr_in6.sin6_port = htons(l);
			}
			else {
				conf->bind_addr_in.sin_family = AF_INET;
				if (inet_pton(AF_INET, addrstr, &conf->bind_addr_in.sin_addr) != 1)
					exit_error(ctx, "invalid bind address `%s'", addrstr);
				conf->bind_addr_in.sin_port = htons(l);
			}

			free(addrstr);

			continue;
		}

		IF_OPTION_ARG("-M", "--mtu") {
			conf->mtu = strtol(arg, &endptr, 10);
			if (*endptr || conf->mtu < 576)
				exit_error(ctx, "invalid mtu `%s'", arg);
			continue;
		}

		IF_OPTION_ARG("-P", "--protocol") {
			if (!strcmp(arg, "ethernet"))
				conf->protocol = PROTOCOL_ETHERNET;
			else if (!strcmp(arg, "ip"))
				conf->protocol = PROTOCOL_IP;
			else
				exit_error(ctx, "invalid protocol `%s'", arg);
			continue;
		}


		IF_OPTION_ARG("-m", "--method") {
			if (!strcmp(arg, "null"))
				conf->method = &fastd_method_null;
#ifdef WITH_METHOD_ECFXP
			if (!strcmp(arg, "ecfxp"))
				conf->method = &fastd_method_ec25519_fhmqvc_xsalsa20_poly1305;
#endif
			else
				exit_error(ctx, "invalid method `%s'", arg);
			continue;
		}

		IF_OPTION_ARG("-p", "--peer") {
			*current_peer = malloc(sizeof(fastd_peer_config));
			(*current_peer)->next = NULL;

			memset(&(*current_peer)->address, 0, sizeof(fastd_peer_address));
			if (strcmp(arg, "float") == 0) {
				(*current_peer)->address.sa.sa_family = AF_UNSPEC;
				conf->n_floating++;
				continue;
			}

			if (arg[0] == '[') {
				charptr = strchr(arg, ']');
				if (!charptr || (charptr[1] != ':' && charptr[1] != '\0'))
					exit_error(ctx, "invalid peer address `%s'", arg);

				addrstr = strndup(arg+1, charptr-arg-1);

				if (charptr[1] == ':')
					charptr++;
				else
					charptr = NULL;
			}
			else {
				charptr = strchr(arg, ':');
				if (charptr)
					addrstr = strndup(arg, charptr-arg);
				else
					addrstr = strdup(arg);
			}

			if (charptr) {
				l = strtol(charptr+1, &endptr, 10);
				if (*endptr || l > 65535)
					exit_error(ctx, "invalid peer port `%s'", charptr+1);
			}
			else {
				l = 1337; /* default port */
			}

			if (arg[0] == '[') {
				v6_peers = true;
				(*current_peer)->address.in6.sin6_family = AF_INET6;
				if (inet_pton(AF_INET6, addrstr, &(*current_peer)->address.in6.sin6_addr) != 1)
					exit_error(ctx, "invalid peer address `%s'", addrstr);
				(*current_peer)->address.in6.sin6_port = htons(l);
			}
			else {
				v4_peers = true;
				(*current_peer)->address.in.sin_family = AF_INET;
				if (inet_pton(AF_INET, addrstr, &(*current_peer)->address.in.sin_addr) != 1)
					exit_error(ctx, "invalid peer address `%s'", addrstr);
				(*current_peer)->address.in.sin_port = htons(l);
			}

			free(addrstr);

			current_peer = &(*current_peer)->next;

			continue;
		}

		exit_error(ctx, "config error: unknown option `%s'", argv[i]);
	}

	if (conf->n_floating && conf->bind_addr_in.sin_family == AF_UNSPEC
	    && conf->bind_addr_in6.sin6_family == AF_UNSPEC) {
		conf->bind_addr_in.sin_family = AF_INET;
		conf->bind_addr_in6.sin6_family = AF_INET6;
	}
	else if (v4_peers) {
		conf->bind_addr_in.sin_family = AF_INET;
	}
	else if (v6_peers) {
		conf->bind_addr_in6.sin6_family = AF_INET6;
	}

	bool ok = true;
	if (conf->protocol == PROTOCOL_IP && (!conf->peers || conf->peers->next)) {
		pr_error(ctx, "for protocol `ip' exactly one peer must be configured");
		ok = false;
	}

	if (ok)
		ok = conf->method->check_config(ctx, conf);

	if (!ok)
		exit_error(ctx, "config error");
}

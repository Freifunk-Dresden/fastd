/*
  Copyright (c) 2012-2013, Matthias Schiffer <mschiffer@universe-factory.net>
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


#include "ghash_pclmulqdq.h"


static inline v2di shl(v2di v, int a) {
	v2di tmph, tmpl;
	tmph.v = v.v << ((v2di){{a, a}}).v;
	tmpl.v = v.v >> ((v2di){{64-a, 64-a}}).v;

	return (v2di){{tmph.e[0], tmph.e[1]|tmpl.e[0]}};
}

static inline v2di shr(v2di v, int a) {
	v2di tmph, tmpl;
	tmph.v = v.v >> ((v2di){{a, a}}).v;
	tmpl.v = v.v << ((v2di){{64-a, 64-a}}).v;

	return (v2di){{tmph.e[0]|tmpl.e[1], tmph.e[1]}};
}

static const v2di BYTESWAP_SHUFFLE = { .v16 = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}};

#define BYTESWAP(v) ({ (v).v16 = __builtin_ia32_pshufb128((v).v16, BYTESWAP_SHUFFLE.v16); })

fastd_mac_state_t* fastd_ghash_pclmulqdq_init_state(fastd_context_t *ctx UNUSED, const fastd_mac_context_t *mctx UNUSED, const uint8_t *key) {
	fastd_mac_state_t *state = malloc(sizeof(fastd_mac_state_t));

	memcpy(&state->H, key, sizeof(v2di));
	BYTESWAP(state->H);

	return state;
}

static inline v2di gmul(v2di v, v2di h) {
	/* multiply */
	v2di z0, z1, z2;
	z0.vll = __builtin_ia32_pclmulqdq128(v.vll, h.vll, 0x11);
	z2.vll = __builtin_ia32_pclmulqdq128(v.vll, h.vll, 0x00);

	v2di tmp = {{v.e[0] ^ v.e[1], h.e[0] ^ h.e[1]}};
	z1.vll = __builtin_ia32_pclmulqdq128(tmp.vll, tmp.vll, 0x01);
	z1.v ^= z0.v ^ z2.v;

	v2di pl = {{z0.e[0] ^ z1.e[1], z0.e[1]}};
	v2di ph = {{z2.e[0], z2.e[1] ^ z1.e[0]}};

	pl = shl(pl, 1);
	pl.e[0] |= ph.e[1] >> 63;

	ph = shl(ph, 1);

	/* reduce */
	uint64_t b = ph.e[0] << 62;
	uint64_t c = ph.e[0] << 57;

	v2di d = {{ph.e[0], ph.e[1] ^ b ^ c}};

	v2di e = shr(d, 1);
	v2di f = shr(d, 2);
	v2di g = shr(d, 7);

	pl.v ^= d.v ^ e.v ^ f.v ^ g.v;

	return pl;
}


bool fastd_ghash_pclmulqdq_hash(fastd_context_t *ctx UNUSED, const fastd_mac_state_t *state, fastd_block128_t *out, const fastd_block128_t *in, size_t n_blocks) {
	const v2di *inv = (const v2di*)in;
	v2di v = {{0, 0}};

	size_t i;
	for (i = 0; i < n_blocks; i++) {
		v2di b = inv[i];
		BYTESWAP(b);
		v.v ^= b.v;
		v = gmul(v, state->H);
	}

	BYTESWAP(v);
	*out = v.block;

	return true;
}

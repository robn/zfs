/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Monocypher 3.1.3 (Poly1305, Chacha, and useful utilities)
 * adapted for OpenZFS by Rob Norris <robn@despairlabs.com>
 */

// Copyright (c) 2017-2019, Loup Vaillant
// All rights reserved.
//
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MONOCYPHER_H
#define MONOCYPHER_H

#include <sys/types.h>

////////////////////////
/// Type definitions ///
////////////////////////

// Poly1305
typedef struct {
    uint32_t r[4];   // constant multiplier (from the secret key)
    uint32_t h[5];   // accumulated hash
    uint8_t  c[16];  // chunk of the message
    uint32_t pad[4]; // random number added at the end (from the secret key)
    size_t   c_idx;  // How many bytes are there in the chunk.
} crypto_poly1305_ctx;

////////////////////////////
/// High level interface ///
////////////////////////////

// Constant time comparisons
// -------------------------

// Return 0 if a and b are equal, -1 otherwise
int crypto_verify16(const uint8_t a[16], const uint8_t b[16]);
int crypto_verify32(const uint8_t a[32], const uint8_t b[32]);
int crypto_verify64(const uint8_t a[64], const uint8_t b[64]);

// Erase sensitive data
// --------------------

// Please erase all copies
void crypto_wipe(void *secret, size_t size);

////////////////////////////
/// Low level primitives ///
////////////////////////////

// For experts only.  You have been warned.

// Chacha20
// --------

// Unauthenticated stream cipher.
// Don't forget to add authentication.
uint32_t crypto_ietf_chacha20_ctr(uint8_t       *cipher_text,
                                  const uint8_t *plain_text,
                                  size_t         text_size,
                                  const uint8_t  key[32],
                                  const uint8_t  nonce[12],
                                  uint32_t       ctr);

// Poly 1305
// ---------

// This is a *one time* authenticator.
// Disclosing the mac reveals the key.
// See crypto_lock() on how to use it properly.

// Direct interface
void crypto_poly1305(uint8_t        mac[16],
                     const uint8_t *message, size_t message_size,
                     const uint8_t  key[32]);

// Incremental interface
void crypto_poly1305_init  (crypto_poly1305_ctx *ctx, const uint8_t key[32]);
void crypto_poly1305_update(crypto_poly1305_ctx *ctx,
                            const uint8_t *message, size_t message_size);
void crypto_poly1305_final (crypto_poly1305_ctx *ctx, uint8_t mac[16]);

#endif // MONOCYPHER_H

#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES128 encryption in CBC-mode of operation and handles 0-padding.
// ECB enables the basic ECB 16-byte block algorithm. Both can be enabled
// simultaneously.

// The #ifndef-guard allows it to be configured before #include'ing or at
// compile time.
#ifndef CBC
#define CBC 1
#endif

#ifndef ECB
#define ECB 1
#endif

void AES128_SetKey(uint8_t *key);

#if defined(ECB) && ECB

void AES128_ECB_encrypt(uint8_t *input, const uint8_t *key, uint8_t *output);
void AES128_ECB_decrypt(uint8_t *input, const uint8_t *key, uint8_t *output);
void AES128_ECB_encrypt_inplace(uint8_t *input);
void AES128_ECB_decrypt_inplace(uint8_t *input);

static inline int AES128_ECB_encrypt_buffer(void *buffer, size_t size) {
	if ((size % 16) != 0) {
		abort();
		return -1;
	}
	for (size_t i = 0; i < size; i += 16) {
		AES128_ECB_encrypt_inplace(buffer + i);
	}
	return 0;
}
static inline int AES128_ECB_decrypt_buffer(void *buffer, size_t size) {
	if ((size % 16) != 0) {
		abort();
		return -1;
	}
	for (size_t i = 0; i < size; i += 16) {
		AES128_ECB_decrypt_inplace(buffer + i);
	}
	return 0;
}

#endif // #if defined(ECB) && ECB

#if defined(CBC) && CBC

void AES128_CBC_encrypt_buffer(uint8_t *output, uint8_t *input, uint32_t length,
							   const uint8_t *key, const uint8_t *iv);
void AES128_CBC_decrypt_buffer(uint8_t *output, uint8_t *input, uint32_t length,
							   const uint8_t *key, const uint8_t *iv);

#endif // #if defined(CBC) && CBC

#endif //_AES_H_

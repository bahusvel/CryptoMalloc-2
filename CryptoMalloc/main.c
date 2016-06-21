//
//  main.c
//  CryptoMalloc
//
//  Created by denis lavrov on 3/05/16.
//  Copyright © 2016 Denis Lavrov. All rights reserved.
//

#define _GNU_SOURCE

#include "aes.h"
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CRYPTO_NOCIPHER 0x01
#define CRYPTO_CLEAR 0x02
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// comment this out to not encrypt STDIO
//#define ENCRYPT_STDIO 1

static pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;

// this is done because on Linux pthreads overrides malloc, and it cannot be
// fetched using dlsym
#ifdef __APPLE__
static void *(*__libc_malloc)(size_t size);
static void *(*__libc_free)(void *ptr);
#else
extern void *__libc_malloc(size_t size);
extern void *__libc_free(void *ptr);
#endif

typedef struct cor_map_node {
	void *key;
	void *cryptoaddr;
	size_t alloc_size;
	unsigned char flags;
	struct cor_map_node *next;
} cor_map_node;

typedef struct cor_map { cor_map_node *first; } cor_map;

static cor_map_node *cor_map_delete(cor_map *map, void *key) {
	cor_map_node *pnp;
	cor_map_node *np;
	for (np = map->first, pnp = map->first; np != NULL;
		 pnp = np, np = np->next) {
		if (np->key == key) {
			if (np == map->first) {
				map->first = np->next;
			} else {
				pnp->next = np->next;
			}
			return np;
		}
	}
	return NULL;
}

static cor_map_node *cor_map_get(cor_map *map, void *key) {
	cor_map_node *np;
	for (np = map->first; np != NULL; np = np->next) {
		if (np->key == key) {
			return np;
		}
	}
	return NULL;
}

static inline void cor_map_set(cor_map *map, cor_map_node *node) {
	node->next = map->first;
	map->first = node;
}

static uint8_t AES_KEY[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae,
							0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
							0x09, 0xcf, 0x4f, 0x3c}; // :)
static char PID_PATH[PATH_MAX];
static int PAGE_SIZE;
static cor_map mem_map = {NULL};
static int fd = -1;
static off64_t crypto_mem_break = 0;
static struct sigaction old_handler;
static pthread_t encryptor_thread;

static void decryptor(int signum, siginfo_t *info, void *context) {
	void *address = info->si_addr;
	if (address == NULL)
		goto segfault;
	cor_map_node *np;
	pthread_mutex_lock(&mymutex);
	for (np = mem_map.first; np != NULL; np = np->next) {
		if (np->key <= address && address <= (np->key + np->alloc_size)) {
			goto decrypt;
		}
	}
	pthread_mutex_unlock(&mymutex);
	goto segfault;
decrypt:
	// printf("Decrypting your ram\n");
	for (size_t i = 0; i < np->alloc_size; i += 16) {
		AES128_ECB_decrypt_inplace(np->cryptoaddr + i);
	}
	// printf("Decrypted!\n");
	mprotect(np->key, np->alloc_size, PROT_READ | PROT_WRITE);
	np->flags |= CRYPTO_CLEAR;
	pthread_mutex_unlock(&mymutex);
	return;
segfault:
	// if stdin and stdout buffers are encrypted this might be bad...
	printf("Real Seg Fault Happened :(\n");
	old_handler.sa_sigaction(signum, info, context);
	return;
}

static void *encryptor(void *ptr) {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGSEGV);
	// this will block sigsegv on this thread, so ensure code from here on is
	// correct
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	while (1) {
		cor_map_node *np;
		pthread_mutex_lock(&mymutex);
		for (np = mem_map.first; np != NULL; np = np->next) {
			if (np->flags & CRYPTO_CLEAR) {
				mprotect(np->key, np->alloc_size, PROT_NONE);
				for (size_t i = 0; i < np->alloc_size; i += 16) {
					AES128_ECB_encrypt_inplace(np->cryptoaddr + i);
				}
				// printf("Encrypted!\n");
				np->flags &= ~CRYPTO_CLEAR;
			}
		}
		pthread_mutex_unlock(&mymutex);
		struct timespec sleep_time = {1, 0}; // seconds, nanoseconds
		while (nanosleep(&sleep_time, &sleep_time))
			continue;
	}
	return NULL;
}

__attribute__((constructor)) static void crypto_malloc_ctor() {
	PAGE_SIZE = getpagesize();
	AES128_SetKey(AES_KEY);

#ifdef __APPLE__
	__libc_malloc = dlsym(RTLD_NEXT, "malloc");
	__libc_free = dlsym(RTLD_NEXT, "free");
#endif

	sprintf(PID_PATH, "/%d.mem", getpid());
	fd = shm_open(PID_PATH, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
	if (fd < 0) {
		perror("Open");
		abort();
	}

	// setting up signal handler
	static struct sigaction sa;
	sa.sa_sigaction = decryptor;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGSEGV);
	sa.sa_flags = SA_SIGINFO | SA_RESTART;

	if (sigaction(SIGSEGV, &sa, &old_handler) < 0) {
		perror("Signal Handler Installation Failed:");
		abort();
	}

	int iret = pthread_create(&encryptor_thread, NULL, encryptor, NULL);
	if (iret) {
		printf("Error - pthread_create() return code: %d\n", iret);
		exit(EXIT_FAILURE);
	}

#ifdef ENCRYPT_STDIO
	// this is a bit evil and is questionable whether it should be used...
	char *stdout_buffer = malloc(BUFSIZ);
	char *stdin_buffer = malloc(BUFSIZ);
	setbuf(stdin, stdin_buffer);
	setbuf(stdout, stdout_buffer);
#endif
}

__attribute__((destructor)) static void crypto_malloc_dtor() {
	close(fd);
	shm_unlink(PID_PATH);
}

void *malloc(size_t size) {
	if (size == 0)
		return NULL;
	// TODO: may need to check if the right segfault handler is set
	size = (size + 4095) & ~0xFFF; // must be page aligned for offset
	pthread_mutex_lock(&mymutex);

	off_t foffset = crypto_mem_break;
	crypto_mem_break += size;

	if (ftruncate64(fd, crypto_mem_break) < 0) {
		perror("ftruncate");
		return NULL;
	}

	void *user_mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
						  MAP_SHARED, fd, foffset);
	void *crypto_mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
							MAP_SHARED, fd, foffset);

	if (user_mem != MAP_FAILED) {
		cor_map_node *head_node = __libc_malloc(sizeof(cor_map_node));
		head_node->key = user_mem;
		head_node->cryptoaddr = crypto_mem;
		head_node->alloc_size = size;
		head_node->flags = CRYPTO_CLEAR;
		cor_map_set(&mem_map, head_node);
		pthread_mutex_unlock(&mymutex);
		return user_mem;
	} else {
		perror("mmap");
		errno = ENOMEM;
		pthread_mutex_unlock(&mymutex);
		return NULL;
	}
}

void free(void *ptr) {
	if (ptr == NULL)
		return;
	pthread_mutex_lock(&mymutex);
	static cor_map_node *previous = NULL;
	if (previous != NULL) {
		if (previous->flags & CRYPTO_CLEAR) {
			// clear out the memory before releasing if it is clear
			memset(previous->key, 0, previous->alloc_size);
		}

		munmap(previous->key, previous->alloc_size);
		munmap(previous->cryptoaddr, previous->alloc_size);
		__libc_free(previous);
		previous = NULL;
	}
	if ((previous = cor_map_delete(&mem_map, ptr)) == NULL) {
		// It really should never go here, but its left as a precaution
		printf("free: Forreign pointer\n");
	}
	pthread_mutex_unlock(&mymutex);
}

// TODO: Use a more sophisticated realloc, for better performance
void *realloc(void *ptr, size_t size) {
	// printf("IT CALLED REALLOC\n");
	if (ptr == NULL)
		return malloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	cor_map_node *node;
	void *new_addr = NULL;
	if ((node = cor_map_get(&mem_map, ptr)) != NULL) {
		new_addr = malloc(size);
		if (new_addr == NULL) {
			printf("MALLOC RETURNED NULL %zu\n", size);
			return NULL;
		}
		memcpy(new_addr, ptr,
			   node->alloc_size < size ? node->alloc_size : size);
		free(ptr);
		return new_addr;
	}
	// It really should never go here, but its left as a precaution
	printf("realloc: Forreign pointer\n");
	return NULL;
}

void *calloc(size_t count, size_t size) {
	// printf("Calloc was called\n");
	if (count == 0 || size == 0)
		return NULL;
	size_t fsize = count * size;
	void *result = malloc(fsize);

	pthread_mutex_lock(&mymutex);
	assert(result != NULL);

	memset(result, 0, fsize);
	pthread_mutex_unlock(&mymutex);

	return result;
}

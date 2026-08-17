#include "nautylus.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

#define NG_CRYPT_HEADER 8u
#define NG_CRYPT_SALT 16u
#define NG_CRYPT_NONCE 24u
#define NG_CRYPT_KEY 32u
#define NG_CRYPT_TAG 16u
#define NG_CRYPT_VERSION "NGCRYPT1"

#ifndef _WIN32
typedef int (*ng_sodium_init_fn)(void);
typedef void (*ng_randombytes_buf_fn)(void*, size_t);
typedef int (*ng_crypto_pwhash_fn)(unsigned char*, unsigned long long, const char*,
                                   unsigned long long, const unsigned char*, unsigned long long,
                                   size_t, int);
typedef int (*ng_encrypt_fn)(unsigned char*, unsigned long long*, const unsigned char*,
                             unsigned long long, const unsigned char*, unsigned long long,
                             const unsigned char*, const unsigned char*, const unsigned char*);
typedef int (*ng_decrypt_fn)(unsigned char*, unsigned long long*, unsigned char*,
                             const unsigned char*, unsigned long long, const unsigned char*,
                             unsigned long long, const unsigned char*, const unsigned char*);

typedef struct {
    void* handle;
    ng_sodium_init_fn sodium_init;
    ng_randombytes_buf_fn randombytes_buf;
    ng_crypto_pwhash_fn pwhash;
    ng_encrypt_fn encrypt;
    ng_decrypt_fn decrypt;
    int attempted;
} ng_sodium_api;

static int ng_load_symbol(void* handle, const char* name, void* destination, size_t size) {
    void* symbol = dlsym(handle, name);
    if (!symbol)
        return 0;
    memcpy(destination, &symbol, size < sizeof(symbol) ? size : sizeof(symbol));
    return 1;
}

static ng_sodium_api* ng_sodium(void) {
    static ng_sodium_api api;
    const char* names[] = {"libsodium.so.23", "libsodium.so", 0};
    size_t i;
    if (api.attempted)
        return api.handle ? &api : 0;
    api.attempted = 1;
    for (i = 0; names[i]; i++) {
        api.handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (api.handle)
            break;
    }
    if (!api.handle ||
        !ng_load_symbol(api.handle, "sodium_init", &api.sodium_init, sizeof(api.sodium_init)) ||
        !ng_load_symbol(api.handle, "randombytes_buf", &api.randombytes_buf,
                        sizeof(api.randombytes_buf)) ||
        !ng_load_symbol(api.handle, "crypto_pwhash", &api.pwhash, sizeof(api.pwhash)) ||
        !ng_load_symbol(api.handle, "crypto_aead_xchacha20poly1305_ietf_encrypt", &api.encrypt,
                        sizeof(api.encrypt)) ||
        !ng_load_symbol(api.handle, "crypto_aead_xchacha20poly1305_ietf_decrypt", &api.decrypt,
                        sizeof(api.decrypt))) {
        if (api.handle)
            dlclose(api.handle);
        api.handle = 0;
        return 0;
    }
    if (api.sodium_init() < 0) {
        dlclose(api.handle);
        api.handle = 0;
        return 0;
    }
    return &api;
}
#endif

static uint64_t ng_crypt_get64(const unsigned char* p) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; i++)
        value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static void ng_crypt_put64(unsigned char* p, uint64_t value) {
    size_t i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)value;
        value >>= 8;
    }
}

static ng_status ng_crypt_read(const char* path, unsigned char** out, size_t* out_size) {
    FILE* file;
    long size;
    unsigned char* data;
    if (!path || !out || !out_size)
        return NG_INVALID_ARGUMENT;
    file = fopen(path, "rb");
    if (!file)
        return NG_IO_ERROR;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NG_IO_ERROR;
    }
    data = (unsigned char*)malloc(size ? (size_t)size : 1);
    if (!data) {
        fclose(file);
        return NG_OOM;
    }
    if (size && fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NG_IO_ERROR;
    }
    fclose(file);
    *out = data;
    *out_size = (size_t)size;
    return NG_OK;
}

static ng_status ng_crypt_write(const char* path, const unsigned char* data, size_t size) {
    FILE* file;
    char* temporary;
    size_t length;
    int closed = 0;
    ng_status status = NG_IO_ERROR;
    if (!path || (!data && size))
        return NG_INVALID_ARGUMENT;
    length = strlen(path);
    temporary = (char*)malloc(length + 5);
    if (!temporary)
        return NG_OOM;
    memcpy(temporary, path, length);
    memcpy(temporary + length, ".tmp", 5);
    file = fopen(temporary, "wb");
    if (file && ng_secure_file(temporary) == NG_OK && fwrite(data, 1, size, file) == size &&
        fclose(file) == 0) {
        closed = 1;
        file = 0;
        if (rename(temporary, path) == 0 && ng_secure_file(path) == NG_OK)
            status = NG_OK;
    }
    if (file && !closed)
        fclose(file);
    remove(temporary);
    free(temporary);
    return status;
}

ng_status ng_encrypt_file(const char* input_path, const char* output_path, const char* password) {
#ifdef _WIN32
    (void)input_path;
    (void)output_path;
    (void)password;
    return NG_INVALID_ARGUMENT;
#else
    ng_sodium_api* api;
    unsigned char* plain = 0;
    unsigned char* cipher = 0;
    unsigned char* output = 0;
    unsigned char salt[NG_CRYPT_SALT], nonce[NG_CRYPT_NONCE], key[NG_CRYPT_KEY];
    unsigned char header[NG_CRYPT_HEADER + NG_CRYPT_SALT + NG_CRYPT_NONCE + 8];
    unsigned long long cipher_size = 0;
    size_t plain_size = 0, total_size;
    ng_status status;
    if (!input_path || !output_path || !password || !*password)
        return NG_INVALID_ARGUMENT;
    api = ng_sodium();
    if (!api)
        return NG_IO_ERROR;
    status = ng_crypt_read(input_path, &plain, &plain_size);
    if (status != NG_OK)
        return status;
    if (plain_size > SIZE_MAX - NG_CRYPT_TAG) {
        free(plain);
        return NG_LIMIT;
    }
    cipher = (unsigned char*)malloc(plain_size + NG_CRYPT_TAG);
    if (!cipher) {
        free(plain);
        return NG_OOM;
    }
    api->randombytes_buf(salt, sizeof(salt));
    api->randombytes_buf(nonce, sizeof(nonce));
    if (api->pwhash(key, sizeof(key), password, (unsigned long long)strlen(password), salt, 2,
                   (size_t)67108864, 2) != 0 ||
        api->encrypt(cipher, &cipher_size, plain, (unsigned long long)plain_size, 0, 0, 0, nonce,
                     key) != 0 || cipher_size > SIZE_MAX - sizeof(header)) {
        memset(key, 0, sizeof(key));
        free(cipher);
        free(plain);
        return NG_IO_ERROR;
    }
    memcpy(header, NG_CRYPT_VERSION, NG_CRYPT_HEADER);
    memcpy(header + NG_CRYPT_HEADER, salt, sizeof(salt));
    memcpy(header + NG_CRYPT_HEADER + sizeof(salt), nonce, sizeof(nonce));
    ng_crypt_put64(header + NG_CRYPT_HEADER + sizeof(salt) + sizeof(nonce), cipher_size);
    total_size = sizeof(header) + (size_t)cipher_size;
    output = (unsigned char*)malloc(total_size);
    if (!output)
        status = NG_OOM;
    else {
        memcpy(output, header, sizeof(header));
        memcpy(output + sizeof(header), cipher, (size_t)cipher_size);
        status = ng_crypt_write(output_path, output, total_size);
    }
    memset(key, 0, sizeof(key));
    free(output);
    free(cipher);
    free(plain);
    return status;
#endif
}

ng_status ng_decrypt_file(const char* input_path, const char* output_path, const char* password) {
#ifdef _WIN32
    (void)input_path;
    (void)output_path;
    (void)password;
    return NG_INVALID_ARGUMENT;
#else
    ng_sodium_api* api;
    unsigned char* input = 0;
    unsigned char* plain = 0;
    unsigned char key[NG_CRYPT_KEY];
    unsigned long long plain_size = 0;
    size_t input_size = 0, cipher_size, header_size = NG_CRYPT_HEADER + NG_CRYPT_SALT + NG_CRYPT_NONCE + 8;
    ng_status status;
    if (!input_path || !output_path || !password || !*password)
        return NG_INVALID_ARGUMENT;
    api = ng_sodium();
    if (!api)
        return NG_IO_ERROR;
    status = ng_crypt_read(input_path, &input, &input_size);
    if (status != NG_OK)
        return status;
    if (input_size < header_size || memcmp(input, NG_CRYPT_VERSION, NG_CRYPT_HEADER) != 0) {
        free(input);
        return NG_CORRUPT;
    }
    if (ng_crypt_get64(input + header_size - 8) > SIZE_MAX)
        cipher_size = 0;
    else
        cipher_size = (size_t)ng_crypt_get64(input + header_size - 8);
    if (cipher_size < NG_CRYPT_TAG || cipher_size != input_size - header_size) {
        free(input);
        return NG_CORRUPT;
    }
    plain = (unsigned char*)malloc(cipher_size - NG_CRYPT_TAG ? cipher_size - NG_CRYPT_TAG : 1);
    if (!plain) {
        free(input);
        return NG_OOM;
    }
    if (api->pwhash(key, sizeof(key), password, (unsigned long long)strlen(password),
                   input + NG_CRYPT_HEADER, 2, (size_t)67108864, 2) != 0 ||
        api->decrypt(plain, &plain_size, 0, input + header_size, (unsigned long long)cipher_size, 0,
                     0, input + NG_CRYPT_HEADER + NG_CRYPT_SALT, key) != 0) {
        memset(key, 0, sizeof(key));
        free(plain);
        free(input);
        return NG_CORRUPT;
    }
    status = ng_crypt_write(output_path, plain, (size_t)plain_size);
    memset(key, 0, sizeof(key));
    free(plain);
    free(input);
    return status;
#endif
}

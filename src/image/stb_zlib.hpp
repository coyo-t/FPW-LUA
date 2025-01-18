//
// Created by Chymic on 18-Jan-25.
//

#ifndef STB_ZLIB_HPP
#define STB_ZLIB_HPP

#include "stb_image.hpp"

// ZLIB client - used by PNG, available for other purposes

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen);

STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen,
																			  int parse_header);

STBIDEF char *stbi_zlib_decode_malloc(const char *buffer, int len, int *outlen);

STBIDEF int stbi_zlib_decode_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

STBIDEF char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);

STBIDEF int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);






#endif //STB_ZLIB_HPP

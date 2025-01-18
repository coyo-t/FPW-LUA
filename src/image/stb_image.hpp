#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif // STBI_NO_STDIO

#define STBI_VERSION 1

enum
{
	STBI_default = 0, // only used for desired_channels

	STBI_grey = 1,
	STBI_grey_alpha = 2,
	STBI_rgb = 3,
	STBI_rgb_alpha = 4
};

#include <stdlib.h>
typedef unsigned char stbi_uc;
typedef unsigned short stbi_us;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBIDEF
#ifdef STB_IMAGE_STATIC
#define STBIDEF static
#else
#define STBIDEF extern
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
// PRIMARY API - works on images of any type
//

//
// load image by filename, open file, or memory buffer
//

typedef struct
{
	int (*read)(void *user, char *data, int size);

	// fill 'data' with 'size' bytes.  return number of bytes actually read
	void (*skip)(void *user, int n); // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
	int (*eof)(void *user); // returns nonzero if we are at end of file/data
} stbi_io_callbacks;

////////////////////////////////////
//
// 8-bits-per-channel interface
//

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file,
                                       int desired_channels);

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                          int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_uc *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);

STBIDEF stbi_uc *stbi_load_from_file(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);

// for stbi_load_from_file, file pointer is left pointing immediately after image
#endif


////////////////////////////////////
//
// 16-bits-per-channel interface
//

STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file,
                                          int desired_channels);

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                             int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_us *stbi_load_16(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);

STBIDEF stbi_us *stbi_load_from_file_16(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
#endif

////////////////////////////////////
//
// float-per-channel interface
//
#ifndef STBI_NO_LINEAR
STBIDEF float *stbi_loadf_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file,
                                      int desired_channels);

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                         int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF float *stbi_loadf(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);

STBIDEF float *stbi_loadf_from_file(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
#endif
#endif


#ifndef STBI_NO_LINEAR
STBIDEF void stbi_ldr_to_hdr_gamma(float gamma);

STBIDEF void stbi_ldr_to_hdr_scale(float scale);
#endif // STBI_NO_LINEAR

#ifndef STBI_NO_STDIO

#endif // STBI_NO_STDIO


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *stbi_failure_reason(void);

// free the loaded image -- this is just free()
STBIDEF void stbi_image_free(void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp);

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp);

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len);

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *clbk, void *user);

#ifndef STBI_NO_STDIO
STBIDEF int stbi_info(char const *filename, int *x, int *y, int *comp);

STBIDEF int stbi_info_from_file(FILE *f, int *x, int *y, int *comp);

STBIDEF int stbi_is_16_bit(char const *filename);

STBIDEF int stbi_is_16_bit_from_file(FILE *f);
#endif


// for image formats that explicitly notate that they have premultiplied alpha,
// we just return the colors as stored in the file. set this flag to force
// unpremultiplication. results are undefined if the unpremultiply overflow.
STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply);

// indicate whether we should process iphone images back to canonical format,
// or just pass them through "as-is"
STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert);

// flip the image vertically, so the first pixel in the output array is the bottom left
STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

// as above, but only applies to images loaded on the thread that calls the function
// this function is only available if your compiler supports thread-local variables;
// calling it will fail to link if your compiler doesn't
STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply);

STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert);

STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip);


#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // STBI_INCLUDE_STB_IMAGE_H


#if defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_PNG) || defined(STBI_ONLY_BMP) \
  || defined(STBI_ONLY_TGA) || defined(STBI_ONLY_GIF) || defined(STBI_ONLY_PSD) \
  || defined(STBI_ONLY_HDR) || defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNM) \
  || defined(STBI_ONLY_ZLIB)
#ifndef STBI_ONLY_JPEG
#define STBI_NO_JPEG
#endif
#ifndef STBI_ONLY_PNG
   #define STBI_NO_PNG
#endif
#ifndef STBI_ONLY_BMP
#define STBI_NO_BMP
#endif
#ifndef STBI_ONLY_PSD
#define STBI_NO_PSD
#endif
#ifndef STBI_ONLY_TGA
#define STBI_NO_TGA
#endif
#ifndef STBI_ONLY_GIF
#define STBI_NO_GIF
#endif
#ifndef STBI_ONLY_HDR
#define STBI_NO_HDR
#endif
#ifndef STBI_ONLY_PIC
#define STBI_NO_PIC
#endif
#ifndef STBI_ONLY_PNM
#define STBI_NO_PNM
#endif
#endif

#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif


#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR)
#include <math.h>  // ldexp, pow
#endif

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif

#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
#define STBI_EXTERN extern "C"
#else
#define STBI_EXTERN extern
#endif


#ifndef _MSC_VER
#ifdef __cplusplus
#define stbi_inline inline
#else
   #define stbi_inline
#endif
#else
   #define stbi_inline __forceinline
#endif

#ifndef STBI_NO_THREAD_LOCALS
#if defined(__cplusplus) &&  __cplusplus >= 201103L
#define STBI_THREAD_LOCAL       thread_local
#elif defined(__GNUC__) && __GNUC__ < 5
      #define STBI_THREAD_LOCAL       __thread
#elif defined(_MSC_VER)
      #define STBI_THREAD_LOCAL       __declspec(thread)
#elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBI_THREAD_LOCAL       _Thread_local
#endif

#ifndef STBI_THREAD_LOCAL
#if defined(__GNUC__)
        #define STBI_THREAD_LOCAL       __thread
#endif
#endif
#endif

#if defined(_MSC_VER) || defined(__SYMBIAN32__)
typedef unsigned short stbi__uint16;
typedef   signed short stbi__int16;
typedef unsigned int   stbi__uint32;
typedef   signed int   stbi__int32;
#else
#include <stdint.h>
typedef uint16_t stbi__uint16;
typedef int16_t stbi__int16;
typedef uint32_t stbi__uint32;
typedef int32_t stbi__int32;
#endif

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbi__uint32) == 4 ? 1 : -1];

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef _MSC_VER
#define STBI_HAS_LROTL
#endif

#ifdef STBI_HAS_LROTL
   #define stbi_lrot(x,y)  _lrotl(x,y)
#else
#define stbi_lrot(x,y)  (((x) << (y)) | ((x) >> (-(y) & 31)))
#endif

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
// gcc doesn't support sse2 intrinsics unless you compile with -msse2,
// which in turn means it gets to use SSE2 everywhere. This is unfortunate,
// but previous attempts to provide the SSE2 functions with runtime
// detection caused numerous issues. The way architecture extensions are
// exposed in GCC/Clang is, sadly, not really suited for one-file libs.
// New behavior: if compiled with -msse2, we use SSE2 without any
// detection; if not, we don't use it at all.
#define STBI_NO_SIMD
#endif

#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)
// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET
//
// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the
// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.
// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not
// simultaneously enabling "-mstackrealign".
//
// See https://github.com/nothings/stb/issues/81 for more information.
//
// So default to no SSE2 on 32-bit MinGW. If you've read this far and added
// -mstackrealign to your build settings, feel free to #define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>

#ifdef _MSC_VER

#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuid
static int stbi__cpuid3(void)
{
   int info[4];
   __cpuid(info,1);
   return info[3];
}
#else
static int stbi__cpuid3(void)
{
   int res;
   __asm {
      mov  eax,1
      cpuid
      mov  res,edx
   }
   return res;
}
#endif

#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   int info3 = stbi__cpuid3();
   return ((info3 >> 26) & 1) != 0;
}
#endif

#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   // If we're even attempting to compile this on GCC/Clang, that means
   // -msse2 is on, which means the compiler is allowed to use SSE2
   // instructions at will, and so are we.
   return 1;
}
#endif

#endif
#endif

// ARM NEON
#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif

#ifdef STBI_NEON
#include <arm_neon.h>
#ifdef _MSC_VER
#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name
#else
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif
#endif

#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif

#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif

///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
struct stbi__context
{
	stbi__uint32 img_x, img_y;
	int img_n, img_out_n;

	stbi_io_callbacks io;
	void *io_user_data;

	int read_from_callbacks;
	int buflen;
	stbi_uc buffer_start[128];
	int callback_already_read;

	stbi_uc *img_buffer, *img_buffer_end;
	stbi_uc *img_buffer_original, *img_buffer_original_end;
};


static void stbi__refill_buffer(stbi__context *s);

// initialize a memory-decode context
static void stbi__start_mem(stbi__context *s, stbi_uc const *buffer, int len)
{
	s->io.read = NULL;
	s->read_from_callbacks = 0;
	s->callback_already_read = 0;
	s->img_buffer = s->img_buffer_original = (stbi_uc *) buffer;
	s->img_buffer_end = s->img_buffer_original_end = (stbi_uc *) buffer + len;
}

// initialize a callback-based context
static void stbi__start_callbacks(stbi__context *s, stbi_io_callbacks *c, void *user)
{
	s->io = *c;
	s->io_user_data = user;
	s->buflen = sizeof(s->buffer_start);
	s->read_from_callbacks = 1;
	s->callback_already_read = 0;
	s->img_buffer = s->img_buffer_original = s->buffer_start;
	stbi__refill_buffer(s);
	s->img_buffer_original_end = s->img_buffer_end;
}

#ifndef STBI_NO_STDIO

static int stbi__stdio_read(void *user, char *data, int size)
{
	return (int) fread(data, 1, size, (FILE *) user);
}

static void stbi__stdio_skip(void *user, int n)
{
	int ch;
	fseek((FILE *) user, n, SEEK_CUR);
	ch = fgetc((FILE *) user); /* have to read a byte to reset feof()'s flag */
	if (ch != EOF)
	{
		ungetc(ch, (FILE *) user); /* push byte back onto stream if valid. */
	}
}

static int stbi__stdio_eof(void *user)
{
	return feof((FILE *) user) || ferror((FILE *) user);
}

static stbi_io_callbacks stbi__stdio_callbacks =
{
	stbi__stdio_read,
	stbi__stdio_skip,
	stbi__stdio_eof,
};

static void stbi__start_file(stbi__context *s, FILE *f)
{
	stbi__start_callbacks(s, &stbi__stdio_callbacks, (void *) f);
}

//static void stop_file(stbi__context *s) { }

#endif // !STBI_NO_STDIO

static void stbi__rewind(stbi__context *s)
{
	// conceptually rewind SHOULD rewind to the beginning of the stream,
	// but we just rewind to the beginning of the initial buffer, because
	// we only use it after doing 'test', which only ever looks at at most 92 bytes
	s->img_buffer = s->img_buffer_original;
	s->img_buffer_end = s->img_buffer_original_end;
}

struct stbi__result_info
{
	int bits_per_channel;
	int num_channels;
	int channel_order;
};


static int stbi__png_test(stbi__context *s);

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp);

static int stbi__png_is16(stbi__context *s);



static
#ifdef STBI_THREAD_LOCAL
STBI_THREAD_LOCAL
#endif
const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
{
	return stbi__g_failure_reason;
}

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
	stbi__g_failure_reason = str;
	return 0;
}
#endif

static void *stbi__malloc(size_t size)
{
	return STBI_MALLOC(size);
}


// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   #define stbi__err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
#define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))

STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
	STBI_FREE(retval_from_stbi_load);
}

#ifndef STBI_NO_LINEAR
static float *stbi__ldr_to_hdr(stbi_uc *data, int x, int y, int comp);
#endif


static int stbi__vertically_flip_on_load_global = 0;

STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip)
{
	stbi__vertically_flip_on_load_global = flag_true_if_should_flip;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__vertically_flip_on_load  stbi__vertically_flip_on_load_global
#else
static STBI_THREAD_LOCAL int stbi__vertically_flip_on_load_local, stbi__vertically_flip_on_load_set;

STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip)
{
	stbi__vertically_flip_on_load_local = flag_true_if_should_flip;
	stbi__vertically_flip_on_load_set = 1;
}

#define stbi__vertically_flip_on_load  (stbi__vertically_flip_on_load_set       \
                                         ? stbi__vertically_flip_on_load_local  \
                                         : stbi__vertically_flip_on_load_global)
#endif // STBI_THREAD_LOCAL

static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
	memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
	ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
	ri->num_channels = 0;

	// test the formats with a very explicit header first (at least a FOURCC
	// or distinctive magic number first)
	if (stbi__png_test(s)) return stbi__png_load(s, x, y, comp, req_comp, ri);

	return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

static stbi_uc *stbi__convert_16_to_8(stbi__uint16 *orig, int w, int h, int channels)
{
	int i;
	int img_len = w * h * channels;
	stbi_uc *reduced;

	reduced = (stbi_uc *) stbi__malloc(img_len);
	if (reduced == NULL) return stbi__errpuc("outofmem", "Out of memory");

	for (i = 0; i < img_len; ++i)
		reduced[i] = (stbi_uc) ((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

	STBI_FREE(orig);
	return reduced;
}

static stbi__uint16 *stbi__convert_8_to_16(stbi_uc *orig, int w, int h, int channels)
{
	int i;
	int img_len = w * h * channels;
	stbi__uint16 *enlarged;

	enlarged = (stbi__uint16 *) stbi__malloc(img_len * 2);
	if (enlarged == NULL) return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");

	for (i = 0; i < img_len; ++i)
		enlarged[i] = (stbi__uint16) ((orig[i] << 8) + orig[i]); // replicate to high and low byte, maps 0->0, 255->0xffff

	STBI_FREE(orig);
	return enlarged;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
	int row;
	size_t bytes_per_row = (size_t) w * bytes_per_pixel;
	stbi_uc temp[2048];
	stbi_uc *bytes = (stbi_uc *) image;

	for (row = 0; row < (h >> 1); row++)
	{
		stbi_uc *row0 = bytes + row * bytes_per_row;
		stbi_uc *row1 = bytes + (h - row - 1) * bytes_per_row;
		// swap row0 with row1
		size_t bytes_left = bytes_per_row;
		while (bytes_left)
		{
			size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
			memcpy(temp, row0, bytes_copy);
			memcpy(row0, row1, bytes_copy);
			memcpy(row1, temp, bytes_copy);
			row0 += bytes_copy;
			row1 += bytes_copy;
			bytes_left -= bytes_copy;
		}
	}
}


static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

	if (result == NULL)
		return NULL;

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 8)
	{
		result = stbi__convert_16_to_8((stbi__uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 8;
	}

	// @TODO: move stbi__convert_format to here

	if (stbi__vertically_flip_on_load)
	{
		int channels = req_comp ? req_comp : *comp;
		stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi_uc));
	}

	return (unsigned char *) result;
}

static stbi__uint16 *stbi__load_and_postprocess_16bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	stbi__result_info ri;
	void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 16);

	if (result == NULL)
		return NULL;

	// it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
	STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

	if (ri.bits_per_channel != 16)
	{
		result = stbi__convert_8_to_16((stbi_uc *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
		ri.bits_per_channel = 16;
	}

	// @TODO: move stbi__convert_format16 to here
	// @TODO: special case RGB-to-Y (and RGBA-to-YA) for 8-bit-to-16-bit case to keep more precision

	if (stbi__vertically_flip_on_load)
	{
		int channels = req_comp ? req_comp : *comp;
		stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi__uint16));
	}

	return (stbi__uint16 *) result;
}

#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
static void stbi__float_postprocess(float *result, int *x, int *y, int *comp, int req_comp)
{
   if (stbi__vertically_flip_on_load && result != NULL) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(float));
   }
}
#endif

#ifndef STBI_NO_STDIO

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBI_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBI_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);
#endif

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
	return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
}
#endif

static FILE *stbi__fopen(char const *filename, char const *mode)
{
	FILE *f;
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
	if (0 != _wfopen_s(&f, wFilename, wMode))
		f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
	f = fopen(filename, mode);
#endif
	return f;
}


STBIDEF stbi_uc *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp)
{
	FILE *f = stbi__fopen(filename, "rb");
	unsigned char *result;
	if (!f) return stbi__errpuc("can't fopen", "Unable to open file");
	result = stbi_load_from_file(f, x, y, comp, req_comp);
	fclose(f);
	return result;
}

STBIDEF stbi_uc *stbi_load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
	unsigned char *result;
	stbi__context s;
	stbi__start_file(&s, f);
	result = stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
	if (result)
	{
		// need to 'unget' all the characters in the IO buffer
		fseek(f, -(int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
	}
	return result;
}

STBIDEF stbi__uint16 *stbi_load_from_file_16(FILE *f, int *x, int *y, int *comp, int req_comp)
{
	stbi__uint16 *result;
	stbi__context s;
	stbi__start_file(&s, f);
	result = stbi__load_and_postprocess_16bit(&s, x, y, comp, req_comp);
	if (result)
	{
		// need to 'unget' all the characters in the IO buffer
		fseek(f, -(int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
	}
	return result;
}

STBIDEF stbi_us *stbi_load_16(char const *filename, int *x, int *y, int *comp, int req_comp)
{
	FILE *f = stbi__fopen(filename, "rb");
	stbi__uint16 *result;
	if (!f) return (stbi_us *) stbi__errpuc("can't fopen", "Unable to open file");
	result = stbi_load_from_file_16(f, x, y, comp, req_comp);
	fclose(f);
	return result;
}


#endif //!STBI_NO_STDIO

STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file,
                                          int desired_channels)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__load_and_postprocess_16bit(&s, x, y, channels_in_file, desired_channels);
}

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                             int *channels_in_file, int desired_channels)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
	return stbi__load_and_postprocess_16bit(&s, x, y, channels_in_file, desired_channels);
}

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
}

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp,
                                          int req_comp)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
	return stbi__load_and_postprocess_8bit(&s, x, y, comp, req_comp);
}

#ifndef STBI_NO_LINEAR
static float *stbi__loadf_main(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
	unsigned char *data;

	data = stbi__load_and_postprocess_8bit(s, x, y, comp, req_comp);
	if (data)
		return stbi__ldr_to_hdr(data, *x, *y, req_comp ? req_comp : *comp);
	return stbi__errpf("unknown image type", "Image not of any known type, or corrupt");
}

STBIDEF float *stbi_loadf_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
	stbi__context s;
	stbi__start_mem(&s, buffer, len);
	return stbi__loadf_main(&s, x, y, comp, req_comp);
}

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp,
                                         int req_comp)
{
	stbi__context s;
	stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
	return stbi__loadf_main(&s, x, y, comp, req_comp);
}

#ifndef STBI_NO_STDIO
STBIDEF float *stbi_loadf(char const *filename, int *x, int *y, int *comp, int req_comp)
{
	float *result;
	FILE *f = stbi__fopen(filename, "rb");
	if (!f) return stbi__errpf("can't fopen", "Unable to open file");
	result = stbi_loadf_from_file(f, x, y, comp, req_comp);
	fclose(f);
	return result;
}

STBIDEF float *stbi_loadf_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
	stbi__context s;
	stbi__start_file(&s, f);
	return stbi__loadf_main(&s, x, y, comp, req_comp);
}
#endif // !STBI_NO_STDIO

#endif // !STBI_NO_LINEAR



#ifndef STBI_NO_LINEAR
static float stbi__l2h_gamma = 2.2f, stbi__l2h_scale = 1.0f;

STBIDEF void stbi_ldr_to_hdr_gamma(float gamma) { stbi__l2h_gamma = gamma; }
STBIDEF void stbi_ldr_to_hdr_scale(float scale) { stbi__l2h_scale = scale; }
#endif

static float stbi__h2l_gamma_i = 1.0f / 2.2f, stbi__h2l_scale_i = 1.0f;

STBIDEF void stbi_hdr_to_ldr_gamma(float gamma) { stbi__h2l_gamma_i = 1 / gamma; }
STBIDEF void stbi_hdr_to_ldr_scale(float scale) { stbi__h2l_scale_i = 1 / scale; }





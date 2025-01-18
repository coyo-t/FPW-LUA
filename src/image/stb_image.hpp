#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#include <cstdint>

#define STBI_VERSION 1

enum
{
	STBI_default = 0, // only used for desired_channels

	STBI_grey = 1,
	STBI_grey_alpha = 2,
	STBI_rgb = 3,
	STBI_rgb_alpha = 4
};

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

#define STBI_NO_STDIO


struct stbi_io_callbacks
{
	int (*read)(void *user, char *data, int size);

	// fill 'data' with 'size' bytes.  return number of bytes actually read
	void (*skip)(void *user, int n); // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
	int (*eof)(void *user); // returns nonzero if we are at end of file/data
};

////////////////////////////////////
//
// 8-bits-per-channel interface
//

STBIDEF std::uint8_t *stbi_load_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file,
                                       int desired_channels);

STBIDEF std::uint8_t *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                          int *channels_in_file, int desired_channels);


////////////////////////////////////
//
// 16-bits-per-channel interface
//

STBIDEF std::uint16_t *stbi_load_16_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file,
                                          int desired_channels);

STBIDEF std::uint16_t *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                             int *channels_in_file, int desired_channels);


////////////////////////////////////
//
// float-per-channel interface
//
#ifndef STBI_NO_LINEAR
STBIDEF float *stbi_loadf_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file,
                                      int desired_channels);

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y,
                                         int *channels_in_file, int desired_channels);


#endif



// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *stbi_failure_reason(void);

// free the loaded image -- this is just free()
STBIDEF void stbi_image_free(void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int stbi_info_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *comp);

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp);

STBIDEF int stbi_is_16_bit_from_memory(std::uint8_t const *buffer, int len);

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *clbk, void *user);



#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // STBI_INCLUDE_STB_IMAGE_H




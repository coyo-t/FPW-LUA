#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#include <cstdint>

enum
{
	STBI_default = 0, // only used for desired_channels

	STBI_grey = 1,
	STBI_grey_alpha = 2,
	STBI_rgb = 3,
	STBI_rgb_alpha = 4
};

extern "C" {

#define STBIDEF extern

#define STBI_NO_STDIO


struct stbi_io_callbacks
{
	int (*read)(void *user, char *data, int size);

	// fill 'data' with 'size' bytes.  return number of bytes actually read
	void (*skip)(void *user, int n); // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
	int (*eof)(void *user); // returns nonzero if we are at end of file/data
};


STBIDEF std::uint8_t*
stbi_load_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);

STBIDEF std::uint16_t*
stbi_load_16_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char*
stbi_failure_reason(void);

// free the loaded image -- this is just free()
STBIDEF void
stbi_image_free(void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF
int stbi_info_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *comp);

STBIDEF
int stbi_is_16_bit_from_memory(std::uint8_t const *buffer, int len);


}
#endif // STBI_INCLUDE_STB_IMAGE_H




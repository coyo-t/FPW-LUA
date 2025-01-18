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


STBIDEF std::uint8_t*
stbi_load_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char*
stbi_failure_reason(void);

// free the loaded image -- this is just free()
STBIDEF void
stbi_image_free(void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int
stbi_info_from_memory(std::uint8_t const *buffer, int len, int *x, int *y, int *comp);


}
#endif // STBI_INCLUDE_STB_IMAGE_H




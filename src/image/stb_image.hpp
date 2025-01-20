#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
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
#define STBIDEF extern auto

STBIDEF coyote_stbi_load_from_memory(
	std::uint8_t const *buffer,
	std::size_t len,
	std::size_t* x,
	std::size_t* y,
	std::size_t* channels_in_file,
	std::size_t desired_channels
) -> unsigned char*;


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF coyote_stbi_failure_reason(void) -> const char*;

// free the loaded image -- this is just free()
STBIDEF coyote_stbi_image_free(void *retval_from_stbi_load) -> void;

// get image dimensions & components without fully decoding
STBIDEF coyote_stbi_info_from_memory(
	unsigned char const *buffer,
	int len,
	int *x,
	int *y,
	int *comp
) -> int;

}

#endif // STBI_INCLUDE_STB_IMAGE_H

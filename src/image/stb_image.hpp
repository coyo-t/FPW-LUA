#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#include "coyote/numberz.hpp"

using Coyote::Byte;
using Coyote::U64;

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


STBIDEF auto stbi_failure_reason() -> const char*;

STBIDEF auto stbi_image_free(void *retval_from_stbi_load) -> void;

STBIDEF auto stbi_load_from_memory (
	Byte const *buffer,
	U64 len,
	U64*x,
	U64*y,
	U64 *channels_in_file,
	U64 desired_channels
) -> Byte*;


STBIDEF auto stbi_info_from_memory (
	Byte const *buffer,
	U64 len,
	U64* x,
	U64* y,
	U64* comp
) -> bool;


}
#endif // STBI_INCLUDE_STB_IMAGE_H




#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

#include "coyote/numberz.hpp"

using Coyote::Byte;
using Coyote::U64;

enum class DesiredChannels : U64
{
	// only used for desired_channels
	Default = 0,

	Grey = 1,
	GreyAlpha = 2,
	RGB = 3,
	RGBA = 4
};

extern "C" {

#define STBIDEF extern auto

#define STBI_NO_STDIO


STBIDEF coyote_stbi_failure_reason () -> const char*;

STBIDEF coyote_stbi_image_free(void *retval_from_stbi_load) -> void;

STBIDEF coyote_stbi_load_from_memory (
	Byte const *source_buffer,
	U64 source_buffer_size,
	U64 *out_x,
	U64 *out_y,
	U64 *channels_in_file
) -> Byte*;


STBIDEF coyote_stbi_info_from_memory (
	Byte const *source_buffer,
	U64 source_buffer_size,
	U64* out_x,
	U64* out_y,
	U64* channels_in_file
) -> U64;


}
#endif // STBI_INCLUDE_STB_IMAGE_H




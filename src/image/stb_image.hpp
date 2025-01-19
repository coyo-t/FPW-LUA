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


STBIDEF stbi_failure_reason() -> const char*;

STBIDEF stbi_image_free(void *retval_from_stbi_load) -> void;

STBIDEF stbi_load_from_memory (
	Byte const *buffer,
	U64 len,
	U64*x,
	U64*y,
	U64 *channels_in_file,
	DesiredChannels desired_channels
) -> Byte*;


STBIDEF stbi_info_from_memory (
	Byte const *buffer,
	U64 len,
	U64* x,
	U64* y,
	U64* comp
) -> bool;


}
#endif // STBI_INCLUDE_STB_IMAGE_H




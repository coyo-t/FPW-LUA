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

struct DllInterface
{
	uint8_t const* source_png_buffer;
	size_t source_png_size;
	size_t desired_channel_count;
	bool is_success;
	union
	{
		struct
		{
			size_t pic_data_size;
			uint8_t* pic_data;
		} success;

		struct
		{
			const char* reason;
		} failure;
	} result;

	auto set_failure (const char* reason) -> bool
	{
		result.failure.reason = reason;
		return false;
	}
};

extern "C" {
#define STBIDEF extern auto


STBIDEF coyote_stbi_interface_sizeof () -> uint64_t;
STBIDEF coyote_stbi_interface_setup (
	DllInterface* interface,
	uint8_t const* source_png_buffer,
	uint64_t source_png_size,
	uint64_t desired_channel_count
) -> void;


STBIDEF coyote_stbi_get_failure (DllInterface* res) -> const char*;
STBIDEF coyote_stbi_get_success (DllInterface* res, uint64_t* out_size) -> uint8_t*;


STBIDEF coyote_stbi_load_from_memory(
	DllInterface *interface,
	uint8_t const *source_png_buffer,
	uint64_t source_length,
	uint64_t *out_wide,
	uint64_t *out_tall
) -> uint8_t*;

STBIDEF coyote_stbi_info_from_memory(
	DllInterface *interface,
	uint64_t *out_pic_wide,
	uint64_t *out_pic_tall,
	uint64_t *out_required_output_size
) -> uint32_t;

// free the loaded image -- this is just free()
STBIDEF coyote_stbi_image_free(void *retval_from_stbi_load) -> void;

}

#endif // STBI_INCLUDE_STB_IMAGE_H

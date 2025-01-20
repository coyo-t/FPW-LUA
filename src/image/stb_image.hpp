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

struct DllInterface
{
	uint8_t const* source_png_buffer;
	size_t source_png_size;
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
	};
};

extern "C" {
#define STBIDEF extern auto


STBIDEF coyote_stbi_interface_sizeof () -> std::uint64_t;
STBIDEF coyote_stbi_interface_setup (
	DllInterface* interface,
	uint8_t const* source_png_buffer,
	uint64_t source_png_size
) -> void;


STBIDEF coyote_stbi_is_success (DllInterface* res) -> uint8_t;
STBIDEF coyote_stbi_failure_get_info (DllInterface* res) -> const char*;

STBIDEF coyote_stbi_success_get_pic (DllInterface* res, uint64_t* out_size) -> uint8_t*;


STBIDEF coyote_stbi_load_from_memory(
	std::uint8_t const *source_png_buffer,
	std::uint64_t source_length,
	std::uint64_t* x,
	std::uint64_t* y,
	std::uint64_t* channels_in_file,
	std::uint64_t desired_channels
) -> std::uint8_t*;

STBIDEF coyote_stbi_info_from_memory(
	std::uint8_t const *source_png_buffer,
	std::uint64_t source_length,
	std::uint64_t* x,
	std::uint64_t* y,
	std::uint64_t* component_count
) -> std::uint32_t;

// free the loaded image -- this is just free()
STBIDEF coyote_stbi_image_free(void *retval_from_stbi_load) -> void;

}

#endif // STBI_INCLUDE_STB_IMAGE_H

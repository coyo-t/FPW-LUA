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

struct SuccessResult
{
	size_t pic_data_size;
	uint8_t* pic_data;
};

struct FailureResult
{
	const char* reason;
};

struct DecodeResult
{

	bool is_success;
	union
	{
		SuccessResult success;
		FailureResult failure;
	};

	explicit DecodeResult (const SuccessResult result):
		is_success(true),
		success(result)
	{
	}

	explicit DecodeResult (const FailureResult result):
		is_success(false),
		failure(result)
	{
	}
};

STBIDEF coyote_stbi_result_is_success (DecodeResult* res) -> uint8_t
{
	return res->is_success != 0;
}

STBIDEF coyote_stbi_failure_get_info (DecodeResult* res) -> const char*
{
	if (res->is_success)
	{
		return "not actually a failure dingus!!!";
	}
	return res->failure.reason;
}

STBIDEF coyote_stbi_success_get_info (
	DecodeResult* res,
	size_t* out_size
) -> uint8_t*
{
	if (!res->is_success)
	{
		return nullptr;
	}

	auto [pic_data_size, pic_data] = res->success;
	if (out_size != nullptr)
	{
		*out_size = pic_data_size;
	}
	return pic_data;
}

STBIDEF coyote_stbi_load_from_memory(
	std::uint8_t const *buffer,
	std::uint64_t len,
	std::uint64_t* x,
	std::uint64_t* y,
	std::uint64_t* channels_in_file,
	std::uint64_t desired_channels
) -> std::uint8_t*;


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF coyote_stbi_failure_reason(void) -> const char*;

// free the loaded image -- this is just free()
STBIDEF coyote_stbi_image_free(void *retval_from_stbi_load) -> void;

// get image dimensions & components without fully decoding
STBIDEF coyote_stbi_info_from_memory(
	std::uint8_t const *buffer,
	std::uint64_t len,
	std::uint64_t* x,
	std::uint64_t* y,
	std::uint64_t* comp
) -> std::uint32_t;

}

#endif // STBI_INCLUDE_STB_IMAGE_H

#ifndef THA_COMBO_MACHINE_HPP
#define THA_COMBO_MACHINE_HPP
#include <cstdint>

namespace Wowee {

	template<typename T>
	static auto luma (T r, T g, T b) -> T
	{
		const auto ir =static_cast<uint64_t>(r);
		const auto ig =static_cast<uint64_t>(g);
		const auto ib =static_cast<uint64_t>(b);
		return static_cast<T>((ir * 77 + ig * 150 + ib * 29) >> 8);
	}

	template<
		size_t SRC_SIZE,
		size_t DST_SIZE,
		typename T,
		auto (*CALLBACK)(T* src, T* dst) -> void
	>
	struct Combo
	{
		static_assert(0 < SRC_SIZE && (SRC_SIZE & 0b111) == SRC_SIZE);
		static_assert(0 < DST_SIZE && (DST_SIZE & 0b111) == DST_SIZE);
		static_assert(CALLBACK != nullptr);

		constexpr auto key = (SRC_SIZE << 3) | DST_SIZE;

		auto get_key () -> decltype(key)
		{
			return key;
		}

		auto get_callback () -> decltype(CALLBACK)
		{
			return CALLBACK;
		}

		auto test (size_t xx, T* src, T* dest) -> void
		{
			for (size_t i=xx-1;; --i, src += SRC_SIZE, dest += DST_SIZE)
			{
				CALLBACK(src, dest);
				if (i == 0)
				{
					break;
				}
			}
		}
	};

	const Combo u8makers[] = {
		Combo<1, 2, uint8_t, [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = 255;
		}>(),
		Combo<1, 3, uint8_t, [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
		}>(),
		Combo<1, 4, uint8_t, [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
			dest[3] = 255;
		}>(),
		Combo<2, 1, uint8_t, [](auto src, auto dest) {
			dest[0] = src[0];
		}>(),
		Combo<2, 3, uint8_t, [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
		}>(),
		Combo<2, 4, uint8_t, [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
			dest[3] = src[1];
		}>(),
		Combo<3, 4, uint8_t, [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = src[1];
			dest[2] = src[2];
			dest[3] = 255;
		}>(),
		Combo<3, 1, uint8_t, [](auto src, auto dest) {
			dest[0] = luma(src[0], src[1], src[2]);
		}>(),
		Combo<3, 2, uint8_t, [](auto src, auto dest) {
			dest[0] = luma(src[0], src[1], src[2]);
			dest[1] = 255;
		}>(),
		Combo<4, 1, uint8_t, [](auto src, auto dest) {
			dest[0] = luma(src[0], src[1], src[2]);
		}>(),
		Combo<4, 2, uint8_t, [](auto src, auto dest) {
			dest[0] = luma(src[0], src[1], src[2]);
			dest[1] = src[3];
		}>(),
		Combo<4, 3, uint8_t, [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = src[1];
			dest[2] = src[2];
		}>(),
	};
}



#endif //THA_COMBO_MACHINE_HPP

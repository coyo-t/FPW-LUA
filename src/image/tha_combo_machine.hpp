#ifndef THA_COMBO_MACHINE_HPP
#define THA_COMBO_MACHINE_HPP
#include <cstdint>

#include <array>

namespace Wowee {

	template<typename T>
	static auto luma (T r, T g, T b) -> T
	{
		const auto ir =static_cast<uint64_t>(r);
		const auto ig =static_cast<uint64_t>(g);
		const auto ib =static_cast<uint64_t>(b);
		return static_cast<T>((ir * 77 + ig * 150 + ib * 29) >> 8);
	}

	template<typename T, size_t S>
	struct ThaArray
	{
		T items[S];

		auto size () -> size_t
		{
			return S;
		}

		auto get (size_t i) -> T*
		{
			if (i >= S)
			{
				return nullptr;
			}
			return items[i];
		}
	};

	static constexpr auto
	COMBO = [](auto a, auto b) -> size_t { return ((a-1)<<2)|(b-1); };

	static constexpr auto MAXS = COMBO(4,4);

	template<typename T>
	auto AAAA ()
	{
		static constexpr auto
		ALPHA_VALUE = static_cast<T>((1<<(sizeof(T)-1))-1)|(1<<(sizeof(T)-1));

		static ThaArray<auto(*)(T*src, T*dst)->void, MAXS> stuffs;

		stuffs[COMBO(1,2)] = [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = ALPHA_VALUE;
		};
		stuffs[COMBO(1,3)] = [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
		};
		stuffs[COMBO(1,4)] = [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
			dest[3] = ALPHA_VALUE;
		};
		stuffs[COMBO(2,1)] = [](auto src, auto dest) {
			dest[0] = src[0];
		};
		stuffs[COMBO(2,3)] = [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
		};
		stuffs[COMBO(2,4)] = [](auto src, auto dest) {
			dest[0] = dest[1] = dest[2] = src[0];
			dest[3] = src[1];
		};
		stuffs[COMBO(3,4)] = [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = src[1];
			dest[2] = src[2];
			dest[3] = ALPHA_VALUE;
		};
		stuffs[COMBO(3,1)] = [](auto src, auto dest) {
			dest[0] = compute_luma(src[0], src[1], src[2]);
		};
		stuffs[COMBO(3,2)] = [](auto src, auto dest) {
			dest[0] = compute_luma(src[0], src[1], src[2]);
			dest[1] = ALPHA_VALUE;
		};
		stuffs[COMBO(4,1)] = [](auto src, auto dest) {
			dest[0] = compute_luma(src[0], src[1], src[2]);
		};
		stuffs[COMBO(4,2)] = [](auto src, auto dest) {
			dest[0] = compute_luma(src[0], src[1], src[2]);
			dest[1] = src[3];
		};
		stuffs[COMBO(4,3)] = [](auto src, auto dest) {
			dest[0] = src[0];
			dest[1] = src[1];
			dest[2] = src[2];
		};
		return stuffs;
	}

	const static auto MAKERU8 = AAAA<uint8_t>();

}



#endif //THA_COMBO_MACHINE_HPP

#ifndef NUMBERZ_HPP
#define NUMBERZ_HPP

namespace Coyote {
	using U8  = unsigned char;
	using U16 = unsigned short;
	using U32 = unsigned int;
	using U64 = unsigned long long;

	using S8  = signed char;
	using S16 = signed short;
	using S32 = signed int;
	using S64 = signed long long;

	using Byte  = U8;
	using Short = S16;
	using Int   = S32;
	using Long  = S64;

	using SizeOf = U64;

	static constexpr auto UMAX(const U64 bits) -> U64
	{
		static_assert(0 < bits && bits <= 64);
		return ((1 << (bits - 1)) - 1) | (1 << (bits - 1));
	}

	static constexpr auto SMIN(const U64 bits) -> S64
	{
		return -static_cast<S64>(UMAX(bits) >> 1) - 1;
	}

	static constexpr auto SMAX(const U64 bits) -> S64
	{
		return static_cast<S64>(UMAX(bits) >> 1);
	}

	template<U64 BITS>
	struct IntSizes
	{
		static_assert(0 < BITS && BITS <= 64);
		U64 bit_size = BITS;
		U64 byte_size = ((BITS-1) >> 3) + 1;
		U64 mask = UMAX(BITS);
	};

	template<U64 Bits>
	struct UnsignedBounds
	{
		static_assert(0 < Bits && Bits <= 64);
		U64 min = 0;
		U64 max = UMAX(Bits);
	};

	template<U64 Bits>
	struct SignedBounds
	{
		static_assert(0 < Bits && Bits <= 64);
		S64 min = SMIN(Bits);
		S64 max = SMAX(Bits);
	};

	constexpr auto Sizes8 = IntSizes<8>();
	constexpr auto Sizes16 = IntSizes<16>();
	constexpr auto Sizes32 = IntSizes<32>();
	constexpr auto Sizes64 = IntSizes<64>();

	constexpr auto BoundsU8  = UnsignedBounds<8>();
	constexpr auto BoundsU16 = UnsignedBounds<16>();
	constexpr auto BoundsU32 = UnsignedBounds<32>();
	constexpr auto BoundsU64 = UnsignedBounds<64>();

	constexpr auto BoundsS8  = SignedBounds<8>();
	constexpr auto BoundsS16 = SignedBounds<16>();
	constexpr auto BoundsS32 = SignedBounds<32>();
	constexpr auto BoundsS64 = SignedBounds<64>();

}


#endif //NUMBERZ_HPP

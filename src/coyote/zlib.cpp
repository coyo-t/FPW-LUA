#include "./zlib.hpp"

#include <cstring>

static int bit_reverse(const int v, const int bits)
{
	// to bit reverse n bits, reverse 16 and shift
	// e.g. 11 bits, bit reverse and shift away 5
	int n = v;
	n = ((n & 0xAAAA) >> 1) | ((n & 0x5555) << 1);
	n = ((n & 0xCCCC) >> 2) | ((n & 0x3333) << 2);
	n = ((n & 0xF0F0) >> 4) | ((n & 0x0F0F) << 4);
	n = ((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8);
	return n >> (16 - bits);
}

auto Coyote::Huffman::zbuild_huffman(const Byte *sizelist, int num) -> bool
{

	int i;
	int k = 0;
	int next_code[16];
	int sizes[17] = {};

	memset(fast, 0, sizeof(fast));
	for (i = 0; i < num; ++i)
	{
		++sizes[sizelist[i]];
	}
	sizes[0] = 0;
	for (i = 1; i < 16; ++i)
	{
		if (sizes[i] > (1 << i))
		{
			failure_reason = "bad sizes";
			return false;
			// return stbi__err("bad sizes", "Corrupt PNG");
		}
	}
	int code = 0;
	for (i = 1; i < 16; ++i)
	{
		next_code[i] = code;
		firstcode[i] = static_cast<U16>(code);
		firstsymbol[i] = static_cast<U16>(k);
		code = (code + sizes[i]);
		if (sizes[i] && (code - 1 >= 1 << i))
		{
			failure_reason = "bad codelengths";
			return false;
			// return stbi__err("bad codelengths", "Corrupt PNG");
		}
		maxcode[i] = code << (16 - i); // preshift for inner loop
		code <<= 1;
		k += sizes[i];
	}
	this->maxcode[16] = 0x10000; // sentinel
	for (i = 0; i < num; ++i)
	{
		if (const int s = sizelist[i])
		{
			const int c = next_code[s] - firstcode[s] + firstsymbol[s];
			auto fastv = static_cast<U16>((s << 9) | i);
			this->size[c] = static_cast<Byte>(s);
			this->value[c] = static_cast<U16>(i);
			if (s <= FAST_BITS)
			{
				int j = bit_reverse(next_code[s], s);
				while (j < (1 << FAST_BITS))
				{
					fast[j] = fastv;
					j += (1 << s);
				}
			}
			++next_code[s];
		}
	}
	return true;

}

auto Coyote::Huffman::get_failure_reason () -> const char *
{
	return this->failure_reason;
}

#pragma once
#include <cstddef>
#include <cstdint>

namespace app::audio
{
    // 0xDEADBEEF
    // 0xACE1ACE1
    // 0x80200003  feedback
    class Lfsr
    {
    public:
        Lfsr(uint32_t seed, uint32_t feedback = 0x80200003) : seed_(seed), feedback_(feedback) {};

        uint32_t Process()
        {
            // Extract the bits to XOR using the feedback mask
            uint32_t bits_to_xor = seed_& feedback_;

            // Compute the XOR of the extracted bits
            uint32_t xor_result = 0;
            while (bits_to_xor)
            {
                xor_result ^= (bits_to_xor & 1); // XOR the least significant bit
                bits_to_xor >>= 1;               // Shift to process the next bit
            }

            // Shift the seed and insert the XOR result into the MSB
            seed_ = (seed_ >> 1) | (xor_result << 31);

            // Return the new value of the seed
            return seed_;
        }
        
    private:
        uint32_t seed_;
        uint32_t feedback_;
    };

} // namespace app::audio
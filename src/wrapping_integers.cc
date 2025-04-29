#include "wrapping_integers.hh"
#include <cstdint>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32 { zero_point + n };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t rounder = (uint64_t)1 << 32;
  uint32_t diff = raw_value_ - Wrap32::wrap( checkpoint, zero_point ).raw_value_;
  if ( diff <= ( rounder >> 1 ) || checkpoint + diff < rounder )
    return checkpoint + diff;
  return checkpoint + diff - rounder;
}

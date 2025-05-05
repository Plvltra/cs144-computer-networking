#include "reassembler.hh"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sys/types.h>
#include <utility>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // Corner case: if the last substring is empty and the first index is equal to the first unassembled index,
  // close the writer and return
  if ( is_last_substring && data.length() == 0 && first_index == first_unassembled_index() ) {
    output_.writer().close();
    return;
  }

  // Check if the packet should be discarded
  uint64_t end_index = first_index + data.length();
  if ( first_index >= first_unacceptable_index() || end_index <= first_unassembled_index() || data.length() == 0
       || first_unassembled_index() == first_unacceptable_index() ) {
    return;
  }

  // Truncate the packet if it is out of bounds
  uint64_t bgein_discarded = first_unassembled_index() > first_index ? first_unassembled_index() - first_index : 0;
  uint64_t end_discarded = end_index > first_unacceptable_index() ? end_index - first_unacceptable_index() : 0;
  first_index += bgein_discarded;
  data = data.substr( bgein_discarded, data.length() - bgein_discarded - end_discarded );
  if ( end_discarded > 0 ) {
    is_last_substring = false;
  }
  assert( first_unassembled_index() <= first_index && first_index < first_unacceptable_index() );

  // // Implementation1: slow but simple
  // // Write the packet to the buffer
  // for ( uint64_t i = 0; i < data.length(); i++ ) {
  //   uint64_t index = first_index + i;
  //   buffer[index] = data[i];
  //   if ( last_indices.contains( index ) ) {
  //     last_indices.erase( index );
  //   }
  // }
  // if ( is_last_substring ) {
  //   last_indices.insert( first_index + data.length() - 1 );
  // }

  // // If the packet is the first unassembled packet, write it to the output
  // if ( first_index == first_unassembled_index() ) {
  //   uint64_t idx_to_write = first_unassembled_index();
  //   string data_to_write = "";
  //   while ( buffer.contains( idx_to_write ) && !last_indices.contains( idx_to_write ) ) {
  //     data_to_write += buffer[idx_to_write];
  //     buffer.erase( idx_to_write );
  //     idx_to_write++;
  //   }
  //   output_.writer().push( std::move( data_to_write ) );

  //   if ( buffer.contains( idx_to_write ) && last_indices.contains( idx_to_write ) ) {
  //     output_.writer().push( string { buffer[idx_to_write] } );
  //     buffer.erase( idx_to_write );
  //     output_.writer().close();
  //   }
  // }

  // Implementation2: fast but complex
  // Write the packet to the buffer
  Packet packet { first_index, data, is_last_substring };
  cache( first_index, move( data ), is_last_substring );

  // If the packet is the first unassembled packet, write it to the output
  while ( buffer.front().first_index == first_unassembled_index() ) {
    Packet& front = buffer.front();
    output_.writer().push( move( front.data ) );
    bool is_last = front.is_last;
    buffer.pop_front();

    if ( is_last ) {
      output_.writer().close();
      break;
    }
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  // return buffer.size();
  uint64_t count = 0;
  for ( auto it = buffer.begin(); it != buffer.end(); ++it ) {
    count += it->data.length();
  }
  return count;
}

void Reassembler::cache( uint64_t first_index, std::string data, bool is_last_substring )
{
  uint64_t end_idx = first_index + data.length();
  // Find the first packet that intersect with the packet
  auto left = lower_bound( buffer.begin(), buffer.end(), first_index, []( const Packet& packet, uint64_t fst_idx ) {
    return packet.first_index + packet.data.length() <= fst_idx;
  } );
  // Find the first packet that end after the packet
  auto right = upper_bound(
    buffer.begin(), buffer.end(), first_index + data.length(), []( uint64_t end_index, const Packet& packet ) {
      return end_index <= packet.first_index + packet.data.length();
    } );

  // Truncate the left packet if it is overlapping with the new packet
  if ( left != buffer.end() && left->first_index <= first_index ) {
    uint64_t left_end_idx = left->first_index + left->data.length();
    if ( left->first_index <= first_index && end_idx <= left_end_idx ) {
      left->data.replace( first_index - left->first_index, data.length(), data );
      if ( end_idx == left_end_idx ) {
        left->is_last = is_last_substring;
      }
      return;
    } else if ( left->first_index == first_index && left_end_idx < end_idx ) {
      left = buffer.erase( left );
    } else if ( left->first_index < first_index && left_end_idx < end_idx ) {
      assert( first_index < left->first_index + left->data.length() );
      left->data = left->data.substr( 0, first_index - left->first_index );
      left->is_last = false;
    }
  }
  // Erase all packets that are completely covered by the new packet
  while ( left != right && left != buffer.end() ) {
    if ( first_index <= left->first_index && left->first_index + left->data.length() <= end_idx ) {
      left = buffer.erase( left );
    } else {
      ++left;
    }
  }
  // Insert the new packet
  auto insert_it = buffer.begin();
  while ( insert_it != buffer.end() && insert_it->first_index < first_index ) {
    ++insert_it;
  }
  right = buffer.insert( insert_it, { first_index, data, is_last_substring } );
  ++right;
  // Truncate the right packet if it is overlapping with the new packet
  if ( right != buffer.end() && first_index + data.length() > right->first_index ) {
    uint64_t right_end_idx = right->first_index + right->data.length();

    if ( right->first_index < end_idx && end_idx < right_end_idx ) {
      uint64_t length = end_idx - right->first_index;
      right->first_index += length;
      right->data = right->data.substr( length, right->data.length() - length );
    } else if ( end_idx == right_end_idx ) {
      buffer.erase( right );
    }
  }
}
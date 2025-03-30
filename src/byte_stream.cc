#include "byte_stream.hh"
#include <cassert>
#include <cstddef>
#include <string_view>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  uint64_t size = min( available_capacity(), data.length() );
  byts_pushed += size;
  for ( size_t i = 0; i < size; i++ ) {
    winds.emplace( data[i] );
    peek_.push_back( data[i] );
  }
}

void Writer::close()
{
  is_closed_ = true;
}

bool Writer::is_closed() const
{
  return is_closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - winds.size();
}

uint64_t Writer::bytes_pushed() const
{
  return byts_pushed;
}

string_view Reader::peek() const
{
  return peek_;
}

void Reader::pop( uint64_t len )
{
  uint64_t pop_size = min( len, winds.size() );
  byts_popped += pop_size;
  peek_ = peek_.substr( pop_size );
  while ( pop_size-- > 0 ) {
    winds.pop();
  }
}

bool Reader::is_finished() const
{
  return is_closed_ && winds.empty();
}

uint64_t Reader::bytes_buffered() const
{
  return winds.size();
}

uint64_t Reader::bytes_popped() const
{
  return byts_popped;
}

#include "tcp_receiver.hh"
#include <algorithm>
#include <cstdint>
#include <optional>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
  }
  if ( message.SYN && ISN_.has_value() ) {
    reassembler_.reader().set_error();
  } else if ( message.SYN && !ISN_.has_value() ) {
    ISN_ = message.seqno;
  } else if ( !message.SYN && !ISN_.has_value() ) {
    return;
  }

  uint64_t first_unasm_idx = reassembler_.writer().bytes_pushed();
  uint64_t stream_index = message.SYN ? 0 : message.seqno.unwrap( ISN_.value(), first_unasm_idx ) - 1; // In case of SYN
  reassembler_.insert( stream_index, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  std::optional<Wrap32> ackno;
  if ( !ISN_.has_value() ) {
    ackno = nullopt;
  } else {
    uint64_t first_unasm_idx = reassembler_.writer().bytes_pushed();
    uint64_t abs_seqno = first_unasm_idx + 1 + ( reassembler_.writer().is_closed() ? 1 : 0 ); // Add syn/fin if needed
    ackno = Wrap32::wrap( abs_seqno, ISN_.value() );
  }

  uint16_t window_size = min( reassembler_.writer().available_capacity(), static_cast<uint64_t>( UINT16_MAX ) );
  bool rst = reassembler_.writer().has_error();
  return TCPReceiverMessage { .ackno = ackno, .window_size = window_size, .RST = rst };
}

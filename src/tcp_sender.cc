#include "tcp_sender.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  assert( LSS >= LAR );
  return LSS - LAR;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consec_retx_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  bool window_is_empty = SWS == 0; // Corner case
  auto can_push = [&]() {
    bool has_content = window_not_full() && reader().bytes_buffered() > 0;
    bool has_fin = window_not_full() && reader().bytes_buffered() == 0 && reader().is_finished();
    return ( window_is_empty || has_content || has_fin ) && !has_fin_pushed;
  };

  while ( can_push() ) {
    if ( window_is_empty ) {
      // Pretend window size is 1
      SWS = 1;
    }

    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap( LSS, isn_ );
    msg.SYN = LSS == 0;
    uint64_t payload_size
      = window_remaining() > msg.sequence_length() ? window_remaining() - msg.sequence_length() : 0;
    payload_size = min( (uint64_t)TCPConfig::MAX_PAYLOAD_SIZE, min( reader().bytes_buffered(), payload_size ) );
    msg.payload = reader().peek().substr( 0, payload_size );
    reader().pop( payload_size );
    LSS += msg.sequence_length();
    has_fin_pushed = window_not_full() && reader().bytes_buffered() == 0 && reader().is_finished();
    msg.FIN = has_fin_pushed;
    LSS += has_fin_pushed;
    msg.RST = reader().has_error();

    if ( msg.sequence_length() > 0 ) {
      transmit( msg );
      if ( !retx_timer_.is_active() ) {
        retx_timer_.active();
      }
      outstanding_segments_.emplace( move( msg ) );
    }

    if ( window_is_empty ) {
      window_is_empty = false;
      SWS = 0;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { .seqno = Wrap32::wrap( LSS, isn_ ), .RST = reader().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  SWS = msg.window_size;
  window_has_set = true;
  if ( msg.RST ) {
    reader().set_error();
  }

  // Handle ackno
  if ( !msg.ackno.has_value() ) {
    return;
  }
  uint64_t checkpoint = reader().bytes_popped();
  uint64_t ackno = msg.ackno.value().unwrap( isn_, checkpoint ); // Convert to absolute seqno
  if ( ackno > LSS || ackno <= LAR ) {
    return; // Ignore impossible ackno or duplicate ackno
  }

  LAR = ackno;
  retx_timer_ = RetxTimer { initial_RTO_ms_ }.active();
  consec_retx_ = 0;

  while ( !outstanding_segments_.empty() ) {
    const TCPSenderMessage& front = outstanding_segments_.front();
    uint64_t abs_seqno = front.seqno.unwrap( isn_, checkpoint );
    if ( abs_seqno + front.sequence_length() <= ackno ) {
      outstanding_segments_.pop();
    } else {
      break;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( outstanding_segments_.empty() || !retx_timer_.is_active() ) {
    return;
  }

  retx_timer_.tick( ms_since_last_tick );
  if ( retx_timer_.is_expired() ) {
    transmit( outstanding_segments_.front() );
    bool skip_backoff = SWS == 0 && window_has_set;
    if ( !skip_backoff ) {
      retx_timer_.backoff();
      ++consec_retx_;
    }
    retx_timer_.reset();
  }
}

bool TCPSender::window_not_full() const
{
  assert( LSS >= LAR );
  // In push special-case(window size is zero), it can happens that LSS - LAR > SWS
  // assert( LSS - LAR <= SWS );
  return LSS - LAR < SWS;
}

uint64_t TCPSender::window_remaining() const
{
  return LAR + SWS > LSS ? LAR + SWS - LSS : 0;
}

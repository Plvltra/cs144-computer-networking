#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <queue>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), retx_timer_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // For testing: how many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  class RetxTimer
  {
  public:
    RetxTimer( uint64_t initial_RTO ) : RTO( initial_RTO ) {};

    RetxTimer& tick( uint64_t ms )
    {
      if ( is_active_ ) {
        time_passed_ += ms;
      }
      return *this;
    }
    RetxTimer& active()
    {
      is_active_ = true;
      time_passed_ = 0;
      return *this;
    }
    void backoff() { RTO *= 2; }
    RetxTimer& reset()
    {
      time_passed_ = 0;
      return *this;
    }
    bool is_expired() const { return time_passed_ >= RTO; }
    bool is_active() const { return is_active_; }

  private:
    uint64_t RTO;
    uint64_t time_passed_ { 0 };
    bool is_active_ { false };
  };

  bool window_not_full() const;
  uint64_t window_remaining() const;
  Reader& reader() { return input_.reader(); }

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  bool has_fin_pushed { false };
  RetxTimer retx_timer_; // Retransmission timer
  std::queue<TCPSenderMessage> outstanding_segments_ {};
  uint64_t consec_retx_ { 0 };

  /* Maintain invariant: (LSS - LAR) ≤ SWS */
  uint16_t SWS { 0 };            // Send window size
  uint64_t LAR { 0 };            // Last acknowledgment received, absolute seqno
  uint64_t LSS { 0 };            // Last segment sent, absolute seqno
  bool window_has_set { false }; // Has the SWS been set
};

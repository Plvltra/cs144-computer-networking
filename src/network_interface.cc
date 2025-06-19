#include <algorithm>
#include <iostream>
#include <vector>

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t next_ipv4 = next_hop.ipv4_numeric();
  if ( mapping_table_.contains( next_ipv4 ) ) {
    EthernetFrame frame = make_frame( ethernet_address_,
                                      std::get<EthernetAddress>( mapping_table_[next_ipv4] ),
                                      EthernetHeader::TYPE_IPv4,
                                      serialize( dgram ) );
    transmit( frame );
  } else {
    dgrams_to_send_.emplace( next_ipv4, DgramAndTTL { dgram, ARP_REQUEST_GAP } );

    if ( !arp_gap_.contains( next_ipv4 ) || arp_gap_[next_ipv4] > ARP_REQUEST_GAP ) {
      ARPMessage arp
        = make_arp( ARPMessage::OPCODE_REQUEST, ethernet_address_, ip_address_.ip(), {}, next_hop.ip() );
      EthernetFrame frame
        = make_frame( ethernet_address_, ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, serialize( arp ) );
      transmit( frame );
      arp_gap_[next_ipv4] = 0;
    }
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      datagrams_received_.emplace( move( dgram ) );
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp;
    if ( !parse( arp, frame.payload ) ) {
      return;
    }
    if ( ip_address_.ipv4_numeric() != arp.target_ip_address ) {
      return;
    }

    // Recorp IP-to-Ethernet mappings
    EthernetAddress sender_eth = arp.sender_ethernet_address;
    uint32_t sender_ipv4 = arp.sender_ip_address;
    mapping_table_[arp.sender_ip_address] = pair( sender_eth, EXPIRED_TIME );

    // Send queued datagrams
    if ( dgrams_to_send_.contains( sender_ipv4 ) ) {
      auto [head, tail] = dgrams_to_send_.equal_range( sender_ipv4 );
      for_each( head, tail, [this, &sender_eth]( const auto& pair ) {
        EthernetFrame frame
          = make_frame( ethernet_address_, sender_eth, EthernetHeader::TYPE_IPv4, serialize( pair.second.first ) );
        transmit( frame );
      } );
      dgrams_to_send_.erase( head, tail );
    }

    // Reply ARP
    if ( arp.opcode == ARPMessage::OPCODE_REQUEST ) {
      Address sender_ip_addr = Address::from_ipv4_numeric( sender_ipv4 );
      ARPMessage reply_arp = make_arp(
        ARPMessage::OPCODE_REPLY, ethernet_address_, ip_address_.ip(), sender_eth, sender_ip_addr.ip() );
      EthernetFrame reply_frame
        = make_frame( ethernet_address_, sender_eth, EthernetHeader::TYPE_ARP, serialize( reply_arp ) );
      transmit( reply_frame );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  std::erase_if( mapping_table_, [ms_since_last_tick]( auto& item ) {
    auto& [ip, eth_and_ttl] = item;
    if ( eth_and_ttl.second <= ms_since_last_tick ) {
      return true;
    } else {
      eth_and_ttl.second -= ms_since_last_tick;
      return false;
    }
  } );

  std::erase_if( dgrams_to_send_, [ms_since_last_tick]( auto& item ) {
    auto& [ip, dgram_and_ttl] = item;
    if ( dgram_and_ttl.second <= ms_since_last_tick ) {
      return true;
    } else {
      dgram_and_ttl.second -= ms_since_last_tick;
      return false;
    }
  } );

  for ( auto it = arp_gap_.begin(); it != arp_gap_.end(); ++it ) {
    auto& [ip, time] = *it;
    time += ms_since_last_tick;
  }
}

ARPMessage NetworkInterface::make_arp( const uint16_t opcode,
                                       const EthernetAddress sender_ethernet_address,
                                       const string& sender_ip_address,
                                       const EthernetAddress target_ethernet_address,
                                       const string& target_ip_address )
{
  ARPMessage arp;
  arp.opcode = opcode;
  arp.sender_ethernet_address = sender_ethernet_address;
  arp.sender_ip_address = Address( sender_ip_address, 0 ).ipv4_numeric();
  arp.target_ethernet_address = target_ethernet_address;
  arp.target_ip_address = Address( target_ip_address, 0 ).ipv4_numeric();
  return arp;
}

EthernetFrame NetworkInterface::make_frame( const EthernetAddress& src,
                                            const EthernetAddress& dst,
                                            const uint16_t type,
                                            vector<Ref<string>> payload )
{
  EthernetFrame frame;
  frame.header.src = src;
  frame.header.dst = dst;
  frame.header.type = type;
  frame.payload = move( payload );
  return frame;
}

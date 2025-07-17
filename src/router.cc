#include "router.hh"
#include "debug.hh"

#include <algorithm>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  // cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
  //      << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
  //      << " on interface " << interface_num << "\n";

  route_tables_.emplace(
    PrefixInfo{route_prefix, prefix_length},
    ForwardingTarget{next_hop, interface_num});
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  ranges::for_each(interfaces_, [&route_tables = this->route_tables_, &interfaces = this->interfaces_](std::shared_ptr<NetworkInterface> interface){
    std::queue<InternetDatagram>& dgrams = interface->datagrams_received();

    auto ProcessDgrams = [&route_tables, &interfaces](std::queue<InternetDatagram>& dgrams){
      while (!dgrams.empty()) {
        InternetDatagram& dgram = dgrams.front();
        uint32_t dst_ip = dgram.header.dst;
        auto match_prefix = route_tables | views::filter([dst_ip](auto& item){
          auto& [route_prefix, prefix_length] = item.first;
          uint32_t mask;
          if (prefix_length == 0)
            mask = 0;
          else if (prefix_length == 32)
            mask = 0xFFFFFFFF;
          else
            mask = 0xFFFFFFFF << (32 - prefix_length);

          return (route_prefix & mask) == (dst_ip & mask);
        });
        auto proj = [](pair<PrefixInfo, ForwardingTarget> p){
          return p.first.second;
        };
        auto longest_prefix_it = ranges::max_element(match_prefix, {}, proj);

        // If no routes matched or ttl belows 1, drop the datagram
        if (longest_prefix_it == match_prefix.end() || dgram.header.ttl <= 1) {
          dgrams.pop();
          continue;
        }
        // Send datagram
        auto& [next_hop, interface_num] = longest_prefix_it->second;
        dgram.header.ttl--;
        dgram.header.compute_checksum();
        interfaces[interface_num]->send_datagram(dgram,
            next_hop.has_value() ? next_hop.value() : Address::from_ipv4_numeric(dst_ip));
        dgrams.pop();
      }
    };

    ProcessDgrams(dgrams);
  });
}

// MODULE(socks_rfc1928)
// LANGUAGE(c++11)
// SOCKS proxying protocol

#if !defined(SOCKS_RFC1928_API)
#define SOCKS_RFC1928_API static
#endif

#if !defined(SOCKS_RFC1928_PROTOTYPES)
#define SOCKS_RFC1928_PROTOTYPES
#include <cstddef>
#include <cstdint>

namespace socks_rfc1928
{

struct SOCKSClientProtocol {
        enum Error {
                Error_NeedInputPacket,
                Success_Done,
                Error_Init,
                Error_OutputBufferTooSmall,
                Error_UnexpectedInputPacket,
                Error_AuthenticationMethodRejected,
                Error_ConnectRejected,
                Error_Unimplemented,
                ErrorCount,
        };

        // Init:
        char const *dest_domain_name;
        std::size_t dest_domain_name_size;
        uint16_t dest_port;
        uint8_t *output_buffer;
        uint8_t *output_buffer_last;

        // Output:
        uint8_t *output_buffer_written_last;
        enum Error error = Error_Init;
        char *error_msg;

        // Private:
        enum { A,
               B,
               C,
               Done,
        } state = A;
        char error_msg_buffer[128];
        std::size_t error_msg_size;
};

/**
 * Precondition(client->dest_domain_name, client->dest_port,
 * client->output_buffer, client->output_buffer_last)
 * Postcondition([client->output_buffer..client->output_buffer_written_last]
 * with first message to send)
 */
SOCKS_RFC1928_API SOCKSClientProtocol::Error
SOCKSClientProtocolInit(SOCKSClientProtocol *client);

/**
 * Returns Success_Done on succesfull completion of the negotiation.
 *
 * From that point on, the socket can directly be used to talk to the
 * destination.
 *
 * Precondition(SOCKSClientProtocolInit(client) == Error_NeedInputPacket)
 */
SOCKS_RFC1928_API SOCKSClientProtocol::Error
SOCKSClientProtocolNext(SOCKSClientProtocol *client,
                        uint8_t *packet_data,
                        std::size_t packet_data_size);

} // namespace socks_rfc1928

#endif // END(SOCKS_RFC1928_PROTOTYPES)

#if !defined(SOCKS_RFC1928_IMPLEMENTATION) && defined(SOCKS_RFC1928_MAIN)
#define SOCKS_RFC1928_IMPLEMENTATION
#endif

#if defined(SOCKS_RFC1928_IMPLEMENTATION)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(__clang__) || defined (__GNUC__)
#define SOCKS_PACKED_STRUCT_BEGIN _Pragma("pack(push)")
#define SOCKS_PACKED_STRUCT_END _Pragma("pack(pop)")
#endif

#if !defined(SOCKS_PACKED_STRUCT_BEGIN)
#error "Missing packed struct pragmas"
#endif

#if !defined(SOCKS_PACKED_STRUCT_END)
#error "Missing packed struct pragmas"
#endif

#define SOCKS_PACKED_STRUCT_ASSERT(name, expected_size)                        \
        static_assert(sizeof(name) == expected_size, "packing struct "         \
                                                     "violation")

#if !defined(fatal_if)
#include <cstdio>
#include <cstdlib>
#define fatal_if(__expr, __message)                                            \
        if (__expr) {                                                          \
                std::fprintf(stderr, "%s:%d: fatal: %s\n", __FILE__, __LINE__, \
                             __message);                                       \
                std::abort();                                                  \
        }
#endif

namespace socks_rfc1928
{

template <typename T, size_t N> constexpr size_t countof(T (&)[N]) { return N; }

enum socks_method : uint8_t {
        socks_method_no_authentication_required = 0x00,
        socks_method_gssapi = 0x01,
        socks_method_username_password = 0x02,
        socks_method_iana_assigned_first = 0x03,
        socks_method_iana_assigned_last = 0x80,
        socks_method_private_first = 0x80,
        socks_method_last = 0xFF,
};

SOCKS_PACKED_STRUCT_BEGIN
struct client_msg_version_identifier_method_selection_header {
        uint8_t ver = 5;
        uint8_t nmethods;
        // socks_method methods<255>; // up-to-nmethods
};
SOCKS_PACKED_STRUCT_ASSERT(
    client_msg_version_identifier_method_selection_header, 2);
SOCKS_PACKED_STRUCT_END

SOCKS_PACKED_STRUCT_BEGIN
struct server_msg_method_selection {
        uint8_t ver;
        socks_method method;
};
SOCKS_PACKED_STRUCT_ASSERT(server_msg_method_selection, 2);
SOCKS_PACKED_STRUCT_END

enum class socks_cmd : uint8_t {
        connect = 0x01,
        bind = 0x02,
        udp = 0x03,
};

enum class socks_address_type : uint8_t {
        ipv4 = 0x01,
        domainname = 0x03,
        ipv6 = 0x04,
};

SOCKS_PACKED_STRUCT_BEGIN
struct client_msg_socks_request_header {
        uint8_t ver = 0x05;
        socks_cmd cmd;
        uint8_t reserved = 0x00;
        socks_address_type atyp;
        // follows an addressport_{ipv4,domain_name,ipv6}
};
SOCKS_PACKED_STRUCT_ASSERT(client_msg_socks_request_header, 4);
SOCKS_PACKED_STRUCT_END

SOCKS_PACKED_STRUCT_BEGIN
struct address_port_ipv4 {
        uint8_t address[4];
        uint8_t port_hi;
        uint8_t port_lo;
};
SOCKS_PACKED_STRUCT_ASSERT(address_port_ipv4, 4 + 2);
SOCKS_PACKED_STRUCT_END

// struct address_port_domain_name {
//   uint8_t len;
//   uint8_t name<len>;
//   uint8_t port_hi;
//   uint8_t port_lo;
// };

uint8_t *address_port_domain_name_pack(char const *name_first,
                                       std::size_t name_size,
                                       uint16_t port,
                                       uint8_t *d_first,
                                       uint8_t const *d_last)
{
        if (d_first == d_last)
                return {};
        fatal_if(std::numeric_limits<uint8_t>::max() < name_size,
                 "domain name too large");
        *d_first = uint8_t(name_size);
        ++d_first;
        while (name_size--) {
                if (d_first == d_last)
                        return {};
                *d_first = *name_first;
                ++name_first;
                ++d_first;
        }
        if (d_first == d_last)
                return {};
        *d_first = ((port >> 8) & 0xFF);
        ++d_first;
        if (d_first == d_last)
                return {};
        *d_first = (port & 0xFF);
        ++d_first;
        return d_first;
}

uint8_t *address_port_domain_name_unpack(uint8_t *first,
                                         uint8_t *last,
                                         char **name_first,
                                         std::size_t *name_size,
                                         uint16_t *port)
{
        fatal_if(first == last, "not enough bytes available");
        *name_size = *first;
        ++first;
        *name_first = reinterpret_cast<char *>(first);
        fatal_if(last - first < ptrdiff_t(*name_size),
                 "not enough bytes available");
        first += *name_size;
        fatal_if(last - first < 2, "not enough bytes available");
        *port = *first << 8;
        ++first;
        *port |= *first;
        ++first;
        return first;
}

SOCKS_PACKED_STRUCT_BEGIN
struct address_port_ipv6 {
        uint8_t address[16];
        uint8_t port_hi;
        uint8_t port_lo;
};
SOCKS_PACKED_STRUCT_ASSERT(address_port_ipv6, 16 + 2);
SOCKS_PACKED_STRUCT_END

enum class socks_server_reply : uint8_t {
        succeeded = 0x00,
        general_socks_server_failure = 0x01,
        connection_not_allowed_by_ruleset = 0x02,
        network_unreachable = 0x03,
        host_unreachable = 0x04,
        connection_refused = 0x05,
        ttl_expired = 0x06,
        command_not_supported = 0x07,
        address_type_not_supported = 0x08,
        unassigned_first = 0x09
};

struct server_msg_reply_header {
        uint8_t ver = 0x05;
        socks_server_reply rep;
        uint8_t rsv = 0x00;
        socks_address_type atyp;
        // follows an address_port_{ipv4,domain_name,ipv6}
};

static char const *enum_description(SOCKSClientProtocol::Error x)
{
        static char const *descs[] = {
            "Error_NeedInputPacket",
            "Success_Done",
            "Error_Init",
            "Error_OutputBufferTooSmall",
            "Error_UnexpectedInputPacket",
            "Error_AuthenticationMethodRejected",
            "Error_ConnectRejected",
            "Error_Unimplemented",
        };
        static_assert(countof(descs) == SOCKSClientProtocol::ErrorCount,
                      "sync");
        return descs[x];
}

SOCKSClientProtocol::Error
socks_client_protocol_error(SOCKSClientProtocol *client,
                            SOCKSClientProtocol::Error error,
                            char const *error_msg)
{
        std::strncpy(client->error_msg, error_msg, client->error_msg_size);
        return client->error = error;
}

template <typename... Ts>
SOCKSClientProtocol::Error
socks_client_protocol_error(SOCKSClientProtocol *client,
                            SOCKSClientProtocol::Error error,
                            char const *error_msg_format,
                            Ts... args)
{

        std::snprintf(client->error_msg, client->error_msg_size,
                      error_msg_format, args...);
        return client->error = error;
}

SOCKSClientProtocol::Error
socks_client_protocol_error(SOCKSClientProtocol *client,
                            SOCKSClientProtocol::Error error)
{
        return socks_client_protocol_error(client, error, "%s",
                                           enum_description(error));
}

SOCKSClientProtocol::Error SOCKSClientProtocolInit(SOCKSClientProtocol *client)
{
        client->output_buffer_written_last = client->output_buffer;
        client->error_msg = client->error_msg_buffer;
        client->error_msg_size = sizeof client->error_msg_buffer;
        if (client->output_buffer_last == client->output_buffer) {
                return socks_client_protocol_error(
                    client, SOCKSClientProtocol::Error_OutputBufferTooSmall);
        }
        return SOCKSClientProtocolNext(client, nullptr, 0);
}

SOCKSClientProtocol::Error SOCKSClientProtocolNext(SOCKSClientProtocol *client,
                                                   uint8_t *packet_data,
                                                   std::size_t packet_data_size)
{
        uint8_t const *const d_last = client->output_buffer_last;
        uint8_t *d; // destination

        auto const reset_output = [&]() {
                d = client->output_buffer;
                client->output_buffer_written_last = d;
        };

        auto const too_large = [&](std::size_t size) {
                return std::size_t(d_last - d) < size;
        };

        reset_output();

        switch (client->state) {
        case SOCKSClientProtocol::A: {
                client_msg_version_identifier_method_selection_header p_header;
                {
                        p_header.nmethods = 1;
                }
                uint8_t p_methods[] = {socks_method_no_authentication_required};
                if (too_large(sizeof p_header)) {
                        return socks_client_protocol_error(
                            client,
                            SOCKSClientProtocol::Error_OutputBufferTooSmall);
                }
                memcpy(d, &p_header, sizeof p_header);
                d += sizeof p_header;
                if (too_large(sizeof p_methods)) {
                        return socks_client_protocol_error(
                            client,
                            SOCKSClientProtocol::Error_OutputBufferTooSmall);
                }
                memcpy(d, p_methods, sizeof p_methods);
                d += sizeof p_methods;
                client->state = SOCKSClientProtocol::B;
                client->output_buffer_written_last = d;
                return client->error =
                           SOCKSClientProtocol::Error_NeedInputPacket;
        } /* case SOCKSClientProtocol::A */
        case SOCKSClientProtocol::B: {
                /* process input */ {
                        server_msg_method_selection p;
                        if (packet_data_size != sizeof p)
                                return socks_client_protocol_error(
                                    client,
                                    SOCKSClientProtocol::
                                        Error_UnexpectedInputPacket,
                                    "input packet size expected %d got %d",
                                    sizeof p, packet_data_size);
                        memcpy(&p, packet_data, sizeof p);
                        if (p.ver != 0x05)
                                return socks_client_protocol_error(
                                    client,
                                    SOCKSClientProtocol::
                                        Error_UnexpectedInputPacket,
                                    "wrong version, expected 5 got %d", p.ver);
                        if (p.method == socks_method::socks_method_last)
                                return socks_client_protocol_error(
                                    client,
                                    SOCKSClientProtocol::
                                        Error_AuthenticationMethodRejected);
                        if (p.method !=
                            socks_method::
                                socks_method_no_authentication_required)
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::
                                                Error_UnexpectedInputPacket);
                }
                {
                        client_msg_socks_request_header p;
                        p.cmd = socks_cmd::connect;
                        p.atyp = socks_address_type::domainname;
                        if (too_large(sizeof p)) {
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::
                                                Error_OutputBufferTooSmall);
                        }
                        memcpy(d, &p, sizeof p);
                        d += sizeof p;
                        auto domain_name = client->dest_domain_name;
                        if (!domain_name && client->dest_domain_name_size) {
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::Error_Init,
                                    "missing domain name");
                        }
                        d = address_port_domain_name_pack(
                            domain_name, client->dest_domain_name_size,
                            client->dest_port, d, d_last);
                        if (!d) {
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::
                                                Error_OutputBufferTooSmall);
                        }
                        client->output_buffer_written_last = d;
                        client->state = SOCKSClientProtocol::C;
                }
                return client->error =
                           SOCKSClientProtocol::Error_NeedInputPacket;
        } /* case SOCKSClientProtocol::B */
        case SOCKSClientProtocol::C: {
                server_msg_reply_header p;
                auto s = packet_data;
                auto s_size = packet_data_size;
                if (s_size < sizeof p) {
                        return socks_client_protocol_error(
                            client,
                            SOCKSClientProtocol::Error_UnexpectedInputPacket,
                            "input packet: server_msg_reply_header: "
                            "expected at least %d got %d\n",
                            sizeof p, s_size, 3, 5);
                }
                memcpy(&p, s, sizeof p);
                s += sizeof p;
                s_size -= sizeof p;
                if (p.rep != socks_server_reply::succeeded) {
                        return socks_client_protocol_error(
                            client, SOCKSClientProtocol::Error_ConnectRejected);
                } else {
                        switch (p.atyp) {
                        case socks_address_type::ipv4: {
                                address_port_ipv4 p_a;
                                if (s_size != sizeof p_a) {
                                        return socks_client_protocol_error(
                                            client,
                                            SOCKSClientProtocol::
                                                Error_UnexpectedInputPacket,
                                            "input packet: "
                                            "address_port_ipv4: "
                                            "expected size %d got %d",
                                            sizeof p_a, s_size);
                                }
                                memcpy(&p_a, s, sizeof p_a);
                                s += sizeof p_a;
                                s_size -= sizeof p_a;
                                address_port_ipv4 null_address = {};
                                if (memcmp(&null_address, &p_a, sizeof p_a)) {
                                        return socks_client_protocol_error(
                                            client,
                                            SOCKSClientProtocol::
                                                Error_UnexpectedInputPacket);
                                }
                        } break;
                        case socks_address_type::domainname: {
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::
                                                Error_UnexpectedInputPacket);
                        } break;
                        case socks_address_type::ipv6: {
                                return socks_client_protocol_error(
                                    client, SOCKSClientProtocol::
                                                Error_UnexpectedInputPacket);
                        } break;
                        }
                }
                client->state = SOCKSClientProtocol::Done;
                return socks_client_protocol_error(
                    client, SOCKSClientProtocol::Success_Done,
                    "SOCKS negotiation finished");
        } /* case SOCKSClientProtocol::C */
        case SOCKSClientProtocol::Done: {
                return socks_client_protocol_error(
                    client, SOCKSClientProtocol::Success_Done,
                    "SOCKS negotiation finished");
        }
        };
        return client->error = SOCKSClientProtocol::Error_Unimplemented;
}

} // namespace socks_rfc1928

#endif // END(SOCKS_RFC1928_IMPLEMENTATION)

#if defined(SOCKS_RFC1928_MAIN)

#include <cstring>

static void test_address_port_domain_name()
{
        char const *const a_name = "foo.bar.duck";
        auto const a_name_size = std::strlen(a_name);
        uint16_t a_port = 1080;

        uint8_t buffer[4096];
        uint8_t *buffer_last = buffer + sizeof buffer;

        auto x0_last = socks_rfc1928::address_port_domain_name_pack(
            a_name, a_name_size, a_port, buffer, buffer_last);
        fatal_if(x0_last - buffer != std::ptrdiff_t(a_name_size + 1 + 2),
                 "not packed enough");

        char *x1_name;
        std::size_t x1_name_size;
        uint16_t x1_port;
        socks_rfc1928::address_port_domain_name_unpack(
            buffer, x0_last, &x1_name, &x1_name_size, &x1_port);
        fatal_if(x1_name_size != a_name_size, "roundtrip size mismatch");
        fatal_if(memcmp(x1_name, a_name, a_name_size),
                 "roundtrip name mismatch");
        fatal_if(x1_port != a_port, "port mismatch");
}

#include <vector>

static void test_fake_client_server()
{
        // naming conventions
        // {s<server>,s<client>}
        // p<packet> o<output> i<input>

        uint8_t s_buffer[4096] = {};
        uint8_t *s_buffer_last = s_buffer;
        uint8_t c_buffer[4096] = {};
        uint8_t *c_buffer_last = c_buffer;

        auto const s_copy = [&s_buffer_last](uint8_t const *cursor, void *d,
                                             std::size_t d_size) {
                fatal_if(std::size_t(s_buffer_last - cursor) < d_size,
                         "not enough data");
                memcpy(d, cursor, d_size);
                return cursor + d_size;
        };

        auto const client_next = [&](socks_rfc1928::SOCKSClientProtocol
                                         *client) {
                auto error = socks_rfc1928::SOCKSClientProtocolNext(
                    client, c_buffer, c_buffer_last - c_buffer);
                s_buffer_last = client->output_buffer_written_last;
                if (client->error !=
                    socks_rfc1928::SOCKSClientProtocol::Error_NeedInputPacket) {
                        std::fprintf(stderr, "client:error:%d:%s\n",
                                     client->error, client->error_msg);
                }
                fatal_if(error != socks_rfc1928::SOCKSClientProtocol::
                                      Error_NeedInputPacket,
                         "unexpected error");
        };

        socks_rfc1928::SOCKSClientProtocol client = {};
        {
                client.dest_domain_name = "foo.com";
                client.dest_domain_name_size = strlen(client.dest_domain_name);
                client.dest_port = 22;
                client.output_buffer = s_buffer;
                client.output_buffer_last = s_buffer + sizeof s_buffer;
        }
        SOCKSClientProtocolInit(&client);
        s_buffer_last = client.output_buffer_written_last;

        /* server */ {
                uint8_t const *s_i = s_buffer;

                socks_rfc1928::
                    client_msg_version_identifier_method_selection_header
                        p_header;
                s_i = s_copy(s_i, &p_header, sizeof p_header);
                fatal_if(p_header.ver != 0x05, "wrong version");
                fatal_if(p_header.nmethods == 0,
                         "must provide at least one method");
                std::vector<uint8_t> methods;
                methods.resize(p_header.nmethods);
                s_i = s_copy(s_i, methods.data(), methods.size());
        }

        /* server happy */ {
                socks_rfc1928::server_msg_method_selection p;
                {
                        p.ver = 0x05;
                        p.method = socks_rfc1928::
                            socks_method_no_authentication_required;
                }

                uint8_t *s_o = c_buffer;
                memcpy(s_o, &p, sizeof p);
                s_o += sizeof p;
                c_buffer_last = s_o;
        }

        client_next(&client);

        /* server */ {
                socks_rfc1928::client_msg_socks_request_header p;
                char *domain_name_first;
                std::size_t domain_name_size;
                uint16_t domain_port;

                uint8_t *s_i = s_buffer;
                memcpy(&p, s_i, sizeof p);
                s_i += sizeof p;
                fatal_if(p.atyp !=
                             socks_rfc1928::socks_address_type::domainname,
                         "wrong atyp");
                s_i = socks_rfc1928::address_port_domain_name_unpack(
                    s_i, s_buffer + sizeof s_buffer, &domain_name_first,
                    &domain_name_size, &domain_port);
                std::fprintf(
                    stdout,
                    "server received: cmd:%x atyp:%x domain:%.*s port:%d\n",
                    int(p.cmd), int(p.atyp), int(domain_name_size),
                    domain_name_first, domain_port);
        }

        /* server happy */ {
                socks_rfc1928::server_msg_reply_header p;
                {
                        p.rep = socks_rfc1928::socks_server_reply::succeeded;
                        p.atyp = socks_rfc1928::socks_address_type::ipv4;
                }
                socks_rfc1928::address_port_ipv4 p_address =
                    {}; // server returns a null address
                uint8_t *s_o = c_buffer;
                memcpy(s_o, &p, sizeof p);
                s_o += sizeof p;
                memcpy(s_o, &p_address, sizeof p_address);
                s_o += sizeof p_address;
                c_buffer_last = s_o;
        }

        auto error = socks_rfc1928::SOCKSClientProtocolNext(
            &client, c_buffer, c_buffer_last - c_buffer);
        if (client.error != socks_rfc1928::SOCKSClientProtocol::Success_Done) {
                std::fprintf(stderr, "client:error:%d:%s\n", client.error,
                             client.error_msg);
        }
        fatal_if(error != socks_rfc1928::SOCKSClientProtocol::Success_Done,
                 "expected end of protocol");
}

#if defined(__APPLE__) && defined(__MACH__)
#define SOCKS_RFC1928_HAS_BSD_SOCK (1)
#else
#define SOCKS_RFC1928_HAS_BSD_SOCK (0)
#endif

#if SOCKS_RFC1928_HAS_BSD_SOCK
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

static void bsd_test_client(char const *socks_proxy_address,
                            char const *socks_proxy_port)
{
        std::fprintf(stdout, "bsd_test_client\n");
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct addrinfo *addrinfo = nullptr;
        auto x = getaddrinfo(socks_proxy_address, socks_proxy_port, nullptr,
                             &addrinfo);
        if (x != 0) {
                perror("getaddrinfo");
        }
        x = connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen);
        if (x != 0) {
                perror("connect");
        }
        freeaddrinfo(addrinfo);

        uint8_t output_buffer[4096];

        socks_rfc1928::SOCKSClientProtocol socks_client = {};
        {
                socks_client.dest_domain_name = "google.com";
                socks_client.dest_domain_name_size =
                    strlen(socks_client.dest_domain_name);
                socks_client.dest_port = 80;
                socks_client.output_buffer = output_buffer;
                socks_client.output_buffer_last =
                    output_buffer + sizeof output_buffer;
        }
        SOCKSClientProtocolInit(&socks_client);

        uint8_t input_buffer[4096];
        do {
                auto o_size = socks_client.output_buffer_written_last -
                              socks_client.output_buffer;
                auto x = send(sock, socks_client.output_buffer, o_size, 0);
                if (x != o_size) {
                        perror("send");
                        break;
                }
                auto i_size = recv(sock, input_buffer, sizeof input_buffer, 0);
                if (i_size <= 0) {
                        perror("read");
                        break;
                }
                SOCKSClientProtocolNext(&socks_client, input_buffer, i_size);
        } while (socks_client.error ==
                 socks_rfc1928::SOCKSClientProtocol::Error_NeedInputPacket);
        if (socks_client.error !=
            socks_rfc1928::SOCKSClientProtocol::Success_Done) {
                std::fprintf(stderr, "SOCKS:error:%d:%s\n", socks_client.error,
                             socks_client.error_msg);
        } else {
                std::fprintf(stdout, "SOCKS proxy established\n");
                send(sock, "GET / HTTP/1.0\r\n\r\n",
                     strlen("GET / HTTP/1.0\r\n\r\n"), 0);
                recv(sock, input_buffer, sizeof input_buffer, 0);
                std::fprintf(stdout, "%s", input_buffer);
        }
        close(sock);
}
#endif

int main(int argc, char **argv)
{
        test_address_port_domain_name();
        test_fake_client_server();
#if SOCKS_RFC1928_HAS_BSD_SOCK
        auto arg_first = argv + 1;
        auto arg_last = argv + argc;
        fatal_if(arg_first == arg_last, "missing socks proxy address");
        auto socks_proxy_address = *arg_first;
        ++arg_first;
        fatal_if(arg_first == arg_last, "missing socks proxy port");
        auto socks_proxy_port = *arg_first;
        ++arg_first;
        bsd_test_client(socks_proxy_address, socks_proxy_port);
#endif
        return 0; // not implemented
}

#endif

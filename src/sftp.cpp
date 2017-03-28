// MODULE(sftp)
// LANGUAGE(c++11)
// sftp client module
// runtime dependency to libssh2 > 1.2.8

#if !defined(SFTP_API)
#define SFTP_API static
#endif

#if defined(SFTP_MAIN)
#define SFTP_IMPLEMENTATION (1)
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define SFTP_SSH2_MODULE_USE_DLFCN 1
#define TCP_IP_CONNECTION_USE_BSD 1
#endif

#if !defined(SFTP_PROTOTYPES)
#define SFTP_PROTOTYPES

#include <cstddef>
#include <cstdint>
#include <unistd.h>

namespace sftp
{

#if TCP_IP_CONNECTION_USE_BSD
struct tcp_ip_connection {
        int sock;
        enum Error {
                Error_None,
                Error_CannotCreateSocket,
                Error_CannotFindAddress,
                Error_CannotConnect,
                ErrorCount,
        };
        Error error;
        char const *error_msg;
        char error_msg_buffer[128];
        std::size_t error_msg_buffer_size = sizeof error_msg_buffer;
};
#else
#error "Missing socket implementation"
#endif

struct module {
        void *handle;
};

struct libssh2_api {
        module ssh2_module;
        struct session_api {
                typedef struct {
                } * handle;
                typedef void *(*alloc_fn)(size_t count, void **data);
                typedef void (*free_fn)(void *ptr, void **data);
                typedef void *(*realloc_fn)(void *ptr,
                                            size_t count,
                                            void **data);
#if defined(WIN32)
                typedef SOCKET socket_t;
#else
                typedef int socket_t;
#endif
                handle (*init_ex)(alloc_fn, free_fn, realloc_fn, void *);
                void (*free)(handle session);
                enum { flag_compress = 0x2 };
                void (*flag)(handle session, int flag, int value);
                int (*last_error)(handle session,
                                  char **errmsg,
                                  int *errmsg_len,
                                  int want_buf);
                int (*handshake)(handle session, socket_t socket);
        } session;
        struct agent_api {
                typedef struct {
                } * handle;
                struct publickey {
                        unsigned int magic;
                        void *node;
                        unsigned char *blob;
                        size_t blob_len;
                        char *comment;
                };
                handle (*init)(libssh2_api::session_api::handle session);
                void (*free)(handle agent);
                int (*connect)(handle agent);
                int (*disconnect)(handle agent);
                int (*list_identities)(handle agent);
                int (*get_identity)(handle, publickey **store, publickey *prev);
                int (*userauth)(handle agent,
                                char const *username,
                                publickey *identity);

        } agent;
        struct sftp_api {
                typedef struct {
                } * handle;
                typedef struct {
                } * stream_handle;
                enum open_flag {
                        open_flag_READ = 0x00000001,
                };
                enum open_type {
                        open_type_OPENFILE = 0,
                        open_type_OPENDIR = 1,
                };
                struct attributes {
                        unsigned long flags;
                        uint64_t filesize;
                        unsigned long uid, gid;
                        unsigned long permissions;
                        unsigned long atime, mtime;
                };
                handle (*init)(libssh2_api::session_api::handle session);
                int (*shutdown)(handle sftp);
                unsigned long (*last_error)(handle sftp);
                stream_handle (*open_ex)(handle sftp,
                                         char const *filename,
                                         unsigned int filename_len,
                                         unsigned long flags,
                                         long mode,
                                         int open_type);
                int (*close_handle)(stream_handle handle);
                int (*readdir_ex)(stream_handle handle,
                                  char *buffer,
                                  size_t buffer_maxlen,
                                  char *longentry,
                                  size_t longentry_maxlen,
                                  attributes *attrs);
                ssize_t (*read)(stream_handle handle,
                                char *buffer,
                                std::size_t buffer_maxlen);
        } sftp;
};

struct SFTPClient {
        // input
        char const *hostname;
        std::size_t hostname_size;
        char const *username;
        std::size_t username_size;
        int port;
        char const *opt_socks_proxy_hostname;
        std::size_t opt_socks_proxy_hostname_size;
        int opt_socks_proxy_port;

        // status
        enum Error {
                Error_None,
                Error_ApiMissing,
                Error_Session,
                Error_Connection,
                Error_SessionHandshake,
                Error_Auth,
                Error_SFTPOpen,
                Error_SFTP,
                ErrorCount,
        } error;
        char const *error_msg;
        enum SFTPStatusCode {
                SFTPStatusCode_OK = 0,
                SFTPStatusCode_EOF = 1,
                SFTPStatusCode_NO_SUCH_FILE = 2,
                SFTPStatusCode_PERMISSION_DENIED = 3,
                SFTPStatusCode_FAILURE = 4,
                SFTPStatusCode_BAD_MESSAGE = 5,
                SFTPStatusCode_NO_CONNECTION = 6,
                SFTPStatusCode_CONNECTION_LOST = 7,
                SFTPStatusCode_OP_UNSUPPORTED = 8,
                SFTPStatusCode_INVALID_HANDLE = 9,
                SFTPStatusCode_NO_SUCH_PATH = 10,
                SFTPStatusCode_FILE_ALREADY_EXISTS = 11,
                SFTPStatusCode_WRITE_PROTECT = 12,
                SFTPStatusCode_NO_MEDIA = 13,
                SFTPStatusCode_NO_SPACE_ON_FILESYSTEM = 14,
                SFTPStatusCode_QUOTA_EXCEEDED = 15,
                SFTPStatusCode_UNKNOWN_PRINCIPAL = 16,
                SFTPStatusCode_LOCK_CONFLICT = 17,
                SFTPStatusCode_DIR_NOT_EMPTY = 18,
                SFTPStatusCode_NOT_A_DIRECTORY = 19,
                SFTPStatusCode_INVALID_FILENAME = 20,
                SFTPStatusCode_LINK_LOOP = 21,
                SFTPStatusCodeCount,
        } sftp_status;
        char const *sftp_status_msg;

        // private
        tcp_ip_connection connection;
        libssh2_api::session_api::handle session;
        libssh2_api::sftp_api::handle sftp;
        char error_msg_buffer[128];
        std::size_t const error_msg_buffer_size = sizeof error_msg_buffer;
};

SFTP_API SFTPClient::Error SFTPClientOpen(SFTPClient *client);
SFTP_API void SFTPClientClose(SFTPClient *client);

struct SFTPClientListFiles {
        enum EntryType {
                EntryType_File,
                EntryType_Directory,
                EntryType_Unknown,
        };
        struct Entry {
                char *filename;                  // c string
                std::size_t filename_size;       // capacity
                char *long_list_name;            // c string
                std::size_t long_list_name_size; // capacity
                EntryType entry_type;
                uint64_t file_size;
        };

        // user supplied input
        char const *path;
        std::size_t path_size;

        // input/output
        Entry d_entry; // output buffers, you must set them before-hand

        // status
        enum Error {
                Error_None,
                Error_Client,
                Error_MissingPath,
                Error_CannotOpenDirectory,
                Error_NoMoreListEntry,
                ErrorCount,
        };
        Error error;
        char const *error_msg;

        // private
        SFTPClient *client;
        libssh2_api::sftp_api::stream_handle directory;
        char error_msg_buffer[64];
        std::size_t const error_msg_buffer_size = sizeof error_msg_buffer;
};

SFTP_API SFTPClientListFiles::Error
SFTPClientListFilesOpen(SFTPClientListFiles *file_list, SFTPClient *client);

SFTP_API SFTPClientListFiles::Error
SFTPClientListFilesNext(SFTPClientListFiles *file_list);

SFTP_API void SFTPClientListFilesClose(SFTPClientListFiles *file_list);

struct SFTPClientStreamReader {
        char const *path;
        std::size_t path_size;

        enum Error {
                Error_None,
                Error_MissingPath,
                Error_CannotOpenFile,
                Error_Client,
                Error_NoMoreBytes,
                ErrorCount,
        };

        Error error;
        char const *error_msg;

        uint8_t *d_buffer;         // where to store results
        std::size_t d_buffer_size; // capacity
        std::size_t d_bytes_read;  // size actually read

        // private
        SFTPClient *client;
        libssh2_api::sftp_api::stream_handle file;
        char error_msg_buffer[128];
        std::size_t const error_msg_buffer_size = sizeof error_msg_buffer;
};

SFTP_API SFTPClientStreamReader::Error
SFTPClientStreamReaderOpen(SFTPClientStreamReader *stream, SFTPClient *client);

SFTP_API void SFTPClientStreamReaderClose(SFTPClientStreamReader *stream);

/**
 * Postcondition(counted range [[stream->d_buffer..stream->d_bytes_read))
 * filled)
 */
SFTP_API SFTPClientStreamReader::Error
SFTPClientStreamReaderNext(SFTPClientStreamReader *stream);

} // END(namespace sftp)

#endif

#if defined(SFTP_IMPLEMENTATION)

#include "socks_rfc1928.cpp"

#if SFTP_SSH2_MODULE_USE_DLFCN
#include <dlfcn.h>
#include <string> // TODO(nil): remove
#endif

#if TCP_IP_CONNECTION_USE_BSD
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "Missing socket implementation"
#endif

#include <cstdio>
#include <cstring>
#include <limits>

#if !defined(fatal_if)
#include <cstdlib>
#define fatal_if(__expr, __message)                                            \
        if (__expr) {                                                          \
                std::fprintf(stderr, "fatal: %s\n", __message);                \
                std::abort();                                                  \
        }
#endif

namespace sftp
{

template <typename T, size_t N> constexpr size_t countof(T (&)[N]) { return N; }

static int saturated_int(std::size_t x)
{
        int upper_bound = std::numeric_limits<int>::max();
        if (x > std::size_t(upper_bound)) {
                return upper_bound;
        }
        return static_cast<int>(x);
}

template <typename Resource, typename... MsgArgs>
typename Resource::Error assign_error(Resource *resource,
                                      typename Resource::Error x,
                                      char const *msg_format,
                                      MsgArgs... msg_args)
{
        std::snprintf(resource->error_msg_buffer,
                      resource->error_msg_buffer_size, msg_format, msg_args...);
        resource->error_msg = resource->error_msg_buffer;
        return resource->error = x;
}

template <typename Resource>
typename Resource::Error assign_error(Resource *resource,
                                      typename Resource::Error x)
{
        return assign_error(resource, x, "%s", enum_description(x));
}

struct temp_cstring {
        char const *first;
        size_t size;
        void *buffer;
        std::size_t buffer_size;
        ~temp_cstring() { std::free(buffer); }
};

static temp_cstring cstring(char const *x, std::size_t size)
{
        temp_cstring result = {};
        if (x[size] == 0) {
                result.first = x;
                result.size = size;
        } else {
                result.buffer = std::calloc(size + 1, 1);
                result.buffer_size = size + 1;
                result.first = (char *)result.buffer;
                std::memcpy(result.buffer, x, size);
        }
        return result;
}

#if SFTP_SSH2_MODULE_USE_DLFCN

static bool module_open(module *module, char const *name)
{
        // TODO(nicolas): TAG(portability) where should the library come from?
        std::string path{name};
        path += ".dylib"; // TAG(MacOS)
        module->handle = dlopen(path.c_str(), RTLD_NOW);
        if (!module->handle) {
                std::fprintf(stderr, "error: could not load module '%s'\n",
                             path.c_str());
        }
        return module->handle;
}

static bool module_opened(module const *module) { return module->handle; }

template <typename T>
void modulesym_assign(T &dest, module const module, char const *symbol_name)
{
        dest = reinterpret_cast<T>(dlsym(module.handle, symbol_name));
        fatal_if(!dest, "could not obtain symbol");
}

static void module_close(module *module)
{
        if (module->handle) {
                dlclose(module->handle);
                module->handle = nullptr;
        }
}
#else
#error "Missing module implementation"
#endif

static const char *enum_description(tcp_ip_connection::Error x)
{
        static char const *descs[] = {
            "None", "CannotCreateSocket", "CannotFindAddress", "CannotConnect",
        };
        static_assert(countof(descs) == tcp_ip_connection::ErrorCount, "sync");
        return descs[x];
}

static void tcp_ip_close(tcp_ip_connection *connection)
{
        if (connection->sock >= 0) {
                close(connection->sock);
                connection->sock = -1;
        }
        connection->error = tcp_ip_connection::Error_CannotCreateSocket;
}

static tcp_ip_connection::Error
tcp_ip_terminating_error(tcp_ip_connection *connection,
                         tcp_ip_connection::Error error)
{
        strerror_r(errno, connection->error_msg_buffer,
                   connection->error_msg_buffer_size - 1);
        connection->error_msg = connection->error_msg_buffer;
        std::fprintf(stderr, "tcp_ip_connection:error:%s (%d) (%s)\n",
                     enum_description(error), error, connection->error_msg);
        tcp_ip_close(connection);
        return connection->error = error;
}

// open internet address
static tcp_ip_connection::Error tcp_ip_open(tcp_ip_connection *connection,
                                            char const *hostname,
                                            std::size_t hostname_size,
                                            int port)
{
        *connection = {};

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct addrinfo *addrinfo = nullptr;
        char port_buffer[64];
        std::snprintf(port_buffer, sizeof port_buffer, "%d", port);
        auto res = getaddrinfo(cstring(hostname, hostname_size).first,
                               port_buffer, nullptr, &addrinfo);
        if (res != 0) {
                perror("getaddrinfo(3)");
                return tcp_ip_terminating_error(
                    connection, tcp_ip_connection::Error_CannotFindAddress);
        }

        auto connect_res =
            connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen);
        freeaddrinfo(addrinfo);
        addrinfo = nullptr;
        if (connect_res != 0) {
                perror("connect(3)");
                return tcp_ip_terminating_error(
                    connection, tcp_ip_connection::Error_CannotConnect);
        }
        connection->sock = sock;
        return connection->error = tcp_ip_connection::Error_None;
}

static libssh2_api global_sftp;

static bool sftp_init()
{
        if (module_opened(&global_sftp.ssh2_module))
                return true;
        module module = {};
        auto has_module = module_open(&module, "libssh2");
        if (!has_module) {
                goto failure;
        }

        char const *(*version)(int);
        modulesym_assign(version, module, "libssh2_version");
        if (!version) {
                // pre 1.1
                goto failure;
        } else {
                auto const required_version = 0x010208;
                char const *version_string = version(required_version);
                if (!version_string) {
                        std::fprintf(stderr, "version: '%s' not recent enough, "
                                             "need at least %d.%d.%d\n",
                                     version(0),
                                     ((required_version >> 16) & 0xff),
                                     ((required_version >> 8) & 0xff),
                                     (required_version & 0xff));
                        goto failure;
                }
                modulesym_assign(global_sftp.session.init_ex, module,
                                 "libssh2_session_init_ex");
                modulesym_assign(global_sftp.session.free, module,
                                 "libssh2_session_free");
                modulesym_assign(global_sftp.session.flag, module,
                                 "libssh2_session_flag");
                modulesym_assign(global_sftp.session.last_error, module,
                                 "libssh2_session_last_error");
                modulesym_assign(global_sftp.session.handshake, module,
                                 "libssh2_session_handshake");
                // agent api
                modulesym_assign(global_sftp.agent.init, module,
                                 "libssh2_agent_init");
                modulesym_assign(global_sftp.agent.connect, module,
                                 "libssh2_agent_connect");
                modulesym_assign(global_sftp.agent.disconnect, module,
                                 "libssh2_agent_disconnect");
                modulesym_assign(global_sftp.agent.free, module,
                                 "libssh2_agent_free");
                modulesym_assign(global_sftp.agent.list_identities, module,
                                 "libssh2_agent_list_identities");
                modulesym_assign(global_sftp.agent.get_identity, module,
                                 "libssh2_agent_get_identity");
                modulesym_assign(global_sftp.agent.userauth, module,
                                 "libssh2_agent_userauth");
                // sftp api
                modulesym_assign(global_sftp.sftp.init, module,
                                 "libssh2_sftp_init");
                modulesym_assign(global_sftp.sftp.shutdown, module,
                                 "libssh2_sftp_shutdown");
                modulesym_assign(global_sftp.sftp.last_error, module,
                                 "libssh2_sftp_last_error");
                modulesym_assign(global_sftp.sftp.open_ex, module,
                                 "libssh2_sftp_open_ex");
                modulesym_assign(global_sftp.sftp.close_handle, module,
                                 "libssh2_sftp_close_handle");
                modulesym_assign(global_sftp.sftp.readdir_ex, module,
                                 "libssh2_sftp_readdir_ex");
                modulesym_assign(global_sftp.sftp.read, module,
                                 "libssh2_sftp_read");
        }
        global_sftp.ssh2_module = module;
        return true;
failure:
        module_close(&module);
        global_sftp = {};
        return false;
}

static char const *enum_description(SFTPClient::Error x)
{
        static char const *descs[] = {
            "None", "ApiMissing", "Session", "Connection", "SessionHandshake",
            "Auth", "SFTPInit",   "SFTP",
        };
        static_assert(countof(descs) == SFTPClient::ErrorCount,
                      "enum description must be kept in sync");
        return descs[x];
}

static char const *enum_description(SFTPClient::SFTPStatusCode x)
{
        static char const *descs[] = {
            "OK",
            "EOF",
            "NO_SUCH_FILE",
            "PERMISSION_DENIED",
            "FAILURE",
            "BAD_MESSAGE",
            "NO_CONNECTION",
            "CONNECTION_LOST",
            "OP_UNSUPPORTED",
            "INVALID_HANDLE",
            "NO_SUCH_PATH",
            "FILE_ALREADY_EXISTS",
            "WRITE_PROTECT",
            "NO_MEDIA",
            "NO_SPACE_ON_FILESYSTEM",
            "QUOTA_EXCEEDED",
            "UNKNOWN_PRINCIPAL",
            "LOCK_CONFLICT",
            "DIR_NOT_EMPTY",
            "NOT_A_DIRECTORY",
            "INVALID_FILENAME",
            "LINK_LOOP",
        };
        static_assert(countof(descs) == SFTPClient::SFTPStatusCodeCount,
                      "sync");
        return descs[x];
}

static inline SFTPClient::Error sftp_client_ssh2error(SFTPClient *client,
                                                      SFTPClient::Error error)
{
        if (client->session) {
                char *ssh2_msg;
                int ssh2_msg_size;
                global_sftp.session.last_error(client->session, &ssh2_msg,
                                               &ssh2_msg_size,
                                               0 /* !want_buf */);
                if (ssh2_msg_size > 0) {
                        std::fprintf(stderr, "error: %.*s\n", ssh2_msg_size,
                                     ssh2_msg);
                        return assign_error(client, error, "ssh2: %.*s",
                                            ssh2_msg_size, ssh2_msg);
                }
        }
        return assign_error(client, error);
}

static bool sftp_client_terminated(SFTPClient *client)
{
        return client->error == SFTPClient::Error_ApiMissing ||
               client->error == SFTPClient::Error_Session ||
               client->error == SFTPClient::Error_Connection ||
               client->error == SFTPClient::Error_Auth ||
               client->error == SFTPClient::Error_SFTPOpen;
}

static bool establish_socks_proxy(tcp_ip_connection *connection,
                                  char const *hostname,
                                  std::size_t hostname_size,
                                  uint16_t hostname_port)
{

        uint8_t output[4096];
        socks_rfc1928::SOCKSClientProtocol proxy = {};
        {
                proxy.dest_domain_name = hostname;
                proxy.dest_domain_name_size = hostname_size;
                proxy.dest_port = hostname_port;
                proxy.output_buffer = output;
                proxy.output_buffer_last = output + sizeof output;
        }
        uint8_t input_buffer[4096];
        auto i = input_buffer;
        auto i_size = sizeof input_buffer;
        SOCKSClientProtocolInit(&proxy);
        do {
                auto sock = connection->sock;
                auto o = proxy.output_buffer;
                auto o_size =
                    proxy.output_buffer_written_last - proxy.output_buffer;
                auto x = send(sock, o, o_size, 0);
                if (x != o_size) {
                        return false;
                }
                x = recv(sock, i, i_size, 0);
                if (x <= 0) {
                        return false;
                }
                SOCKSClientProtocolNext(&proxy, i, x);
        } while (proxy.error ==
                 socks_rfc1928::SOCKSClientProtocol::Error_NeedInputPacket);
        return proxy.error == socks_rfc1928::SOCKSClientProtocol::Success_Done;
}

SFTPClient::Error SFTPClientOpen(SFTPClient *client)
{
        sftp_init();
        if (!module_opened(&global_sftp.ssh2_module))
                return client->error = SFTPClient::Error_ApiMissing;

        libssh2_api::session_api::alloc_fn alloc = [](size_t count, void **) {
                return std::calloc(count, 1);
        };
        libssh2_api::session_api::free_fn free = [](void *ptr, void **) {
                std::free(ptr);
        };
        libssh2_api::session_api::realloc_fn realloc =
            [](void *ptr, size_t count, void **) {
                    return std::realloc(ptr, count);
            };

        client->session =
            global_sftp.session.init_ex(alloc, free, realloc, nullptr);
        if (!client->session)
                return sftp_client_ssh2error(client, SFTPClient::Error_Session);

        global_sftp.session.flag(client->session,
                                 libssh2_api::session_api::flag_compress, 1);

        if (client->opt_socks_proxy_hostname_size &&
            client->opt_socks_proxy_port) {
                if (tcp_ip_open(&client->connection,
                                client->opt_socks_proxy_hostname,
                                client->opt_socks_proxy_hostname_size,
                                client->opt_socks_proxy_port) !=
                    tcp_ip_connection::Error_None)
                        return sftp_client_ssh2error(
                            client, SFTPClient::Error_Connection);

                if (!establish_socks_proxy(&client->connection,
                                           client->hostname,
                                           client->hostname_size, client->port))
                        return sftp_client_ssh2error(
                            client, SFTPClient::Error_Connection);
                std::fprintf(stdout, "SOCKS proxy established\n");
        } else if (tcp_ip_open(&client->connection, client->hostname,
                               client->hostname_size,
                               client->port) != tcp_ip_connection::Error_None) {

                return sftp_client_ssh2error(client,
                                             SFTPClient::Error_Connection);
        }

        auto ssh2_error = global_sftp.session.handshake(
            client->session, client->connection.sock);
        if (ssh2_error != 0)
                return sftp_client_ssh2error(
                    client, SFTPClient::Error_SessionHandshake);

        auto agent = global_sftp.agent.init(client->session);
        if (!agent) {
                return sftp_client_ssh2error(client, SFTPClient::Error_Auth);
        }
        enum { agent_connect,
               agent_list_identities,
               agent_identity,
               agent_done,
        } auth_state = agent_connect;
        libssh2_api::agent_api::publickey *prev_key = nullptr;
        do {
                switch (auth_state) {
                case agent_done:
                        break;

                case agent_connect:
                        ssh2_error = global_sftp.agent.connect(agent);
                        auth_state = agent_list_identities;
                        break;
                case agent_list_identities:
                        ssh2_error = global_sftp.agent.list_identities(agent);
                        auth_state = agent_identity;
                        break;
                case agent_identity: {
                        libssh2_api::agent_api::publickey *current_key;
                        ssh2_error = global_sftp.agent.get_identity(
                            agent, &current_key, prev_key);
                        if (ssh2_error == 0) {
                                prev_key = current_key;
                                ssh2_error = global_sftp.agent.userauth(
                                    agent, cstring(client->username,
                                                   client->username_size)
                                               .first,
                                    current_key);
                                if (ssh2_error == 0) {
                                        auth_state = agent_done;
                                } else {
                                        ssh2_error = 0; // try next identity
                                }
                        }
                } break;
                }
        } while (auth_state != agent_done && ssh2_error == 0);
        if (ssh2_error != 0) {
                return sftp_client_ssh2error(client, SFTPClient::Error_Auth);
        }
        global_sftp.agent.disconnect(agent);
        global_sftp.agent.free(agent);
        if (auth_state != agent_done) {
                return sftp_client_ssh2error(client, SFTPClient::Error_Auth);
        }

        client->sftp = global_sftp.sftp.init(client->session);
        if (!client->sftp) {
                return sftp_client_ssh2error(client,
                                             SFTPClient::Error_SFTPOpen);
        }

        return client->error = SFTPClient::Error_None;
}

void SFTPClientClose(SFTPClient *client)
{
        if (client->sftp) {
                global_sftp.sftp.shutdown(client->sftp);
                client->sftp = 0;
        }

        tcp_ip_close(&client->connection);

        if (client->session) {
                global_sftp.session.free(client->session);
                client->session = 0;
        }
        assign_error(client, SFTPClient::Error_Session);
}

char const *enum_description(SFTPClientListFiles::Error x)
{
        static char const *descs[] = {
            "None",
            "Client",
            "MissingPath",
            "CannotOpenDirectory",
            "NoMoreListEntry",
        };
        static_assert(countof(descs) == SFTPClientListFiles::ErrorCount,
                      "sync");
        return descs[x];
}

SFTPClientListFiles::Error
SFTPClientListFilesOpen(SFTPClientListFiles *file_list, SFTPClient *client)
{
        if (!file_list->path || file_list->path_size <= 0) {
                return assign_error(file_list,
                                    SFTPClientListFiles::Error_MissingPath);
        }

        if (sftp_client_terminated(client))
                return assign_error(file_list,
                                    SFTPClientListFiles::Error_Client);

        file_list->client = client;

        file_list->directory =
            global_sftp.sftp.open_ex(file_list->client->sftp, file_list->path,
                                     saturated_int(file_list->path_size),
                                     libssh2_api::sftp_api::open_flag_READ, 0,
                                     libssh2_api::sftp_api::open_type_OPENDIR);
        if (!file_list->directory) {
                sftp_client_ssh2error(file_list->client,
                                      SFTPClient::Error_SFTP);
                return assign_error(
                    file_list, SFTPClientListFiles::Error_CannotOpenDirectory);
        }

        return file_list->error = SFTPClientListFiles::Error_None;
}

static void sftp_fill_status_ssh2error(SFTPClient *client, ssize_t ssh2_error)
{
        enum { LIBSSH2_ERROR_SFTP_PROTOCOL = -31 };
        sftp_client_ssh2error(client, SFTPClient::Error_SFTP);
        if (ssh2_error == LIBSSH2_ERROR_SFTP_PROTOCOL) {
                client->sftp_status = SFTPClient::SFTPStatusCode(
                    global_sftp.sftp.last_error(client->sftp));
                client->sftp_status_msg = enum_description(client->sftp_status);
        } else {
                client->sftp_status = SFTPClient::SFTPStatusCode_OK;
        }
}

SFTPClientListFiles::Error
SFTPClientListFilesNext(SFTPClientListFiles *file_list)
{
        if (file_list->error != SFTPClientListFiles::Error_None)
                return file_list->error;

        if (sftp_client_terminated(file_list->client))
                return assign_error(file_list,
                                    SFTPClientListFiles::Error_Client);

        enum { LIBSSH2_ERROR_EAGAIN = -37,
               LIBSSH2_SFTP_ATTR_HAS_SIZE = 0x1,
               LIBSSH2_SFTP_ATTR_HAS_PERMISSIONS = 0x4,
               LIBSSH2_SFTP_S_IFREG = 0100000,
               LIBSSH2_SFTP_S_IFDIR = 0040000,
        };
        int ssh2_error = 0;
        do {
                libssh2_api::sftp_api::attributes attributes;
                ssh2_error = global_sftp.sftp.readdir_ex(
                    file_list->directory, file_list->d_entry.filename,
                    file_list->d_entry.filename_size,
                    file_list->d_entry.long_list_name,
                    file_list->d_entry.long_list_name_size, &attributes);
                if (attributes.flags & LIBSSH2_SFTP_ATTR_HAS_PERMISSIONS) {
                        if (attributes.permissions & LIBSSH2_SFTP_S_IFDIR) {
                                file_list->d_entry.entry_type =
                                    SFTPClientListFiles::EntryType_Directory;
                        } else if (attributes.permissions &
                                   LIBSSH2_SFTP_S_IFREG) {
                                file_list->d_entry.entry_type =
                                    SFTPClientListFiles::EntryType_File;
                        }
                }
                if (attributes.flags & LIBSSH2_SFTP_ATTR_HAS_SIZE) {
                        file_list->d_entry.file_size = attributes.filesize;
                } else {
                        file_list->d_entry.file_size = -1;
                }
        } while (ssh2_error == LIBSSH2_ERROR_EAGAIN);
        if (ssh2_error < 0) {
                sftp_fill_status_ssh2error(file_list->client, ssh2_error);
                assign_error(file_list, SFTPClientListFiles::Error_Client,
                             "client:%s", file_list->client->error_msg);
        }
        if (ssh2_error == 0) {
                return assign_error(file_list,
                                    SFTPClientListFiles::Error_NoMoreListEntry);
        }
        return SFTPClientListFiles::Error_None;
}

void SFTPClientListFilesClose(SFTPClientListFiles *file_list)
{
        if (file_list->directory) {
                global_sftp.sftp.close_handle(file_list->directory);
                file_list->directory = 0;
        }
}

char const *enum_description(SFTPClientStreamReader::Error const x)
{
        static char const *descs[] = {
            "None", "MissingPath", "CannotOpenFile", "Client", "NoMoreBytes",
        };
        static_assert(countof(descs) == SFTPClientStreamReader::ErrorCount,
                      "sync");
        return descs[x];
}

SFTPClientStreamReader::Error
SFTPClientStreamReaderOpen(SFTPClientStreamReader *stream, SFTPClient *client)
{
        if (!stream->path || stream->path_size <= 0) {
                return assign_error(stream,
                                    SFTPClientStreamReader::Error_MissingPath);
        }

        if (sftp_client_terminated(client))
                return assign_error(stream,
                                    SFTPClientStreamReader::Error_Client);

        stream->client = client;

        stream->file =
            global_sftp.sftp.open_ex(stream->client->sftp, stream->path,
                                     saturated_int(stream->path_size),
                                     libssh2_api::sftp_api::open_flag_READ, 0,
                                     libssh2_api::sftp_api::open_type_OPENFILE);
        if (!stream->file) {
                sftp_client_ssh2error(stream->client, SFTPClient::Error_SFTP);
                return assign_error(
                    stream, SFTPClientStreamReader::Error_CannotOpenFile,
                    "client:%s", stream->client->error_msg);
        }

        return stream->error = SFTPClientStreamReader::Error_None;
}

void SFTPClientStreamReaderClose(SFTPClientStreamReader *stream)
{
        if (stream->file) {
                global_sftp.sftp.close_handle(stream->file);
                stream->file = 0;
        }
}

SFTPClientStreamReader::Error
SFTPClientStreamReaderNext(SFTPClientStreamReader *stream)
{
        if (sftp_client_terminated(stream->client))
                return assign_error(stream,
                                    SFTPClientStreamReader::Error_Client);

        ssize_t ssh2_error;
        enum { LIBSSH2_ERROR_EAGAIN = -37 };
        do {
                ssh2_error = global_sftp.sftp.read(
                    stream->file, reinterpret_cast<char *>(stream->d_buffer),
                    stream->d_buffer_size);
        } while (ssh2_error == LIBSSH2_ERROR_EAGAIN);
        if (ssh2_error < 0) {
                sftp_fill_status_ssh2error(stream->client, ssh2_error);
                return assign_error(stream,
                                    SFTPClientStreamReader::Error_Client,
                                    "client: %s", stream->client->error_msg);
        }
        stream->d_bytes_read = ssh2_error;
        if (ssh2_error == 0) {
                return assign_error(stream,
                                    SFTPClientStreamReader::Error_NoMoreBytes);
        }

        return stream->error = SFTPClientStreamReader::Error_None;
}

} // END(namespace sftp)

#define SOCKS_RFC1928_IMPLEMENTATION
#include "socks_rfc1928.cpp"

#endif // END(SFTP_IMPLEMENTATION)

#if defined(SFTP_MAIN)

#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

struct File {
        std::string path;
        uint64_t size;
};

enum class sftp_list_files_directory_action { Stop, Recurse };

static std::vector<File>
sftp_list_files(sftp::SFTPClient *sftp_client,
                std::string root_dir,
                sftp_list_files_directory_action directory_action)
{
        std::vector<std::string> directories_to_visit = {root_dir};
        std::vector<File> files = {};

        while (!directories_to_visit.empty()) {
                auto path = directories_to_visit.back();
                directories_to_visit.pop_back();

                if (path.back() != '/') {
                        std::fprintf(stderr,
                                     "how the hell did this path lose its /\n");
                        continue;
                }
                sftp::SFTPClientListFiles list_files = {};
                char filename_buffer[4096];
                char long_list_name_buffer[4096];
                {
                        list_files.path = path.data();
                        list_files.path_size = path.size();
                        list_files.d_entry.filename = filename_buffer;
                        list_files.d_entry.filename_size =
                            sizeof filename_buffer;
                        list_files.d_entry.long_list_name =
                            long_list_name_buffer;
                        list_files.d_entry.long_list_name_size =
                            sizeof long_list_name_buffer;
                }
                std::fprintf(stdout, "Opening directory %s\n", path.c_str());
                SFTPClientListFilesOpen(&list_files, sftp_client);
                std::fprintf(stderr, "error:%s (%d)\n", sftp_client->error_msg,
                             sftp_client->error);
                std::fprintf(stderr, "file_list_error:%s (%d)\n",
                             list_files.error_msg, list_files.error);
                while (SFTPClientListFilesNext(&list_files) ==
                       sftp::SFTPClientListFiles::Error_None) {
                        std::fprintf(stdout, "\t%s\n",
                                     list_files.d_entry.long_list_name);
                        if (list_files.d_entry.filename[0] == '.') {
                                continue;
                        }

                        auto file_path =
                            path + std::string(list_files.d_entry.filename);
                        switch (list_files.d_entry.entry_type) {
                        case sftp::SFTPClientListFiles::EntryType_Directory:
                                std::fprintf(stdout, "[d]\n");
                                if (directory_action ==
                                    sftp_list_files_directory_action::Recurse) {
                                        directories_to_visit.push_back(
                                            file_path + "/");
                                }
                                break;

                        case sftp::SFTPClientListFiles::EntryType_File:
                                std::fprintf(stdout, "[f]\n");
                                files.push_back(
                                    {file_path, list_files.d_entry.file_size});
                                break;

                        default:
                                break;
                        }
                }
                fprintf(stderr, "file_list_error:%s (%d)\n",
                        list_files.error_msg, list_files.error);
                fprintf(stderr, "error:%s (%d)\n", sftp_client->error_msg,
                        sftp_client->error);
                SFTPClientListFilesClose(&list_files);
        }

        return files;
}

int main(int argc, char **argv)
{
        // Let's connect to our test server
        char const *hostname = "home.uucidl.com";
        std::size_t hostname_size = std::strlen(hostname);
        int port = 10022;
        char const *username = "nicolas";
        std::size_t username_size = std::strlen(username);
        char const *path = "/";
        std::size_t path_size = std::strlen(path);
        char const *opt_socks_proxy_hostname = nullptr;
        std::size_t opt_socks_proxy_hostname_size = 0;
        int opt_socks_proxy_port = 0;
        bool must_recurse = false;

        auto arg = argv + 1, last_arg = argv + argc;

        while (arg != last_arg) {
                if (0 == std::strcmp(*arg, "--socks")) {
                        ++arg;
                        if (arg != last_arg) {
                                opt_socks_proxy_hostname = *arg;
                                opt_socks_proxy_hostname_size =
                                    std::strlen(opt_socks_proxy_hostname);
                                ++arg;
                        }
                        if (arg != last_arg) {
                                opt_socks_proxy_port = std::atoi(*arg);
                                ++arg;
                        }
                } else if (0 == std::strcmp(*arg, "--recurse")) {
                        must_recurse = true;
                } else {
                        break;
                }
        }

        if (arg != last_arg) {
                hostname = *arg;
                hostname_size = std::strlen(hostname);
                ++arg;
        }
        if (arg != last_arg) {
                port = std::atoi(*arg);
                ++arg;
        }
        if (arg != last_arg) {
                username = *arg;
                username_size = std::strlen(username);
                ++arg;
        }

        if (arg != last_arg) {
                path = *arg;
                path_size = std::strlen(path);
                ++arg;
        }

        sftp::SFTPClient sftp_client = {};
        {
                sftp_client.hostname = hostname;
                sftp_client.hostname_size = std::strlen(hostname);
                sftp_client.username = username;
                sftp_client.username_size = std::strlen(username);
                sftp_client.port = port;
                sftp_client.opt_socks_proxy_hostname = opt_socks_proxy_hostname;
                sftp_client.opt_socks_proxy_hostname_size =
                    opt_socks_proxy_hostname_size;
                sftp_client.opt_socks_proxy_port = opt_socks_proxy_port;
        };
        {
                std::fprintf(stdout, "Connecting to %.*s@%.*s:%d\n",
                             sftp::saturated_int(sftp_client.username_size),
                             sftp_client.username,
                             sftp::saturated_int(sftp_client.hostname_size),
                             sftp_client.hostname, sftp_client.port);
                if (sftp_client.opt_socks_proxy_hostname_size) {
                        std::fprintf(
                            stdout, "Via SOCKS proxy at %.*s:%d\n",
                            sftp::saturated_int(
                                sftp_client.opt_socks_proxy_hostname_size),
                            sftp_client.opt_socks_proxy_hostname,
                            sftp_client.opt_socks_proxy_port);
                }
                SFTPClientOpen(&sftp_client);
                std::fprintf(stderr, "error:%s (%d)\n",
                             enum_description(sftp_client.error),
                             sftp_client.error);

                auto files = sftp_list_files(
                    &sftp_client, std::string(path, path_size),
                    must_recurse ? sftp_list_files_directory_action::Recurse
                                 : sftp_list_files_directory_action::Stop);
                for (auto file : files) {
                        sftp::SFTPClientStreamReader reader = {};
                        std::fprintf(stdout, "opening '%s'\n",
                                     file.path.c_str());
                        reader.path = file.path.data();
                        reader.path_size = file.path.size();
                        auto reader_error =
                            SFTPClientStreamReaderOpen(&reader, &sftp_client);
                        std::fprintf(stderr, "reader_error:%"
                                             "s (%d) for "
                                             "path %.*s\n",
                                     enum_description(reader_error),
                                     reader_error, int(reader.path_size),
                                     reader.path);
                        if (reader_error ==
                            sftp::SFTPClientStreamReader::Error_Client) {
                                fprintf(stderr, "error:%s (%d)\n",
                                        enum_description(sftp_client.error),
                                        sftp_client.error);
                                if (sftp_client.error ==
                                    sftp::SFTPClient::Error_SFTP) {
                                        fprintf(stderr, "sftp "
                                                        "status:%s "
                                                        "(%d)\n",
                                                enum_description(
                                                    sftp_client.sftp_status),
                                                sftp_client.sftp_status);
                                }
                        }
                        size_t size = 0;
                        uint8_t buffer[4096];
                        reader.d_buffer = buffer;
                        reader.d_buffer_size = sizeof buffer;
                        while (SFTPClientStreamReaderNext(&reader) ==
                               sftp::SFTPClientStreamReader::Error_None) {
                                size += reader.d_bytes_read;
                                write(1, reader.d_buffer, reader.d_bytes_read);
                        }
                        std::fprintf(stderr, "read %lu bytes\n", size);
                        SFTPClientStreamReaderClose(&reader);
                        break;
                }
        }
        SFTPClientClose(&sftp_client);
        return sftp_client.error == sftp::SFTPClient::Error_None ? 0 : -1;
}
#endif

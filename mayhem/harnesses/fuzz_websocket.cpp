// Fuzz restbed's RFC 6455 WebSocket frame parser through its public network
// interface: a fixed HTTP upgrade request followed by the fuzz bytes (raw frames)
// is sent over loopback TCP to an in-process restbed::Service whose handler calls
// Session::upgrade. Reaches WebSocketImpl::listen/parse_flags/parse_length_and_mask/
// parse_payload, WebSocketManagerImpl::parse/compose and SocketImpl reads; every
// decoded message is echoed back (compose path).
#include "loopback.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace restbed;
using std::make_shared;
using std::shared_ptr;
using std::string;

static uint16_t g_port = 0;

static const char UPGRADE[] =
    "GET /ws HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

static void get_handler( const shared_ptr< Session > session )
{
    const std::multimap< string, string > headers
    {
        { "Upgrade", "websocket" },
        { "Connection", "Upgrade" },
        { "Sec-WebSocket-Accept", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" }
    };

    session->upgrade( SWITCHING_PROTOCOLS, headers, [ ]( const shared_ptr< WebSocket > socket )
    {
        if ( not socket->is_open( ) )
        {
            return;
        }

        socket->set_close_handler( [ ]( const shared_ptr< WebSocket > ) { } );
        socket->set_error_handler( [ ]( const shared_ptr< WebSocket > s, const std::error_code )
        {
            s->close( );
        } );
        socket->set_message_handler( [ ]( const shared_ptr< WebSocket > s, const shared_ptr< WebSocketMessage > message )
        {
            const auto opcode = message->get_opcode( );

            if ( opcode == WebSocketMessage::CONNECTION_CLOSE_FRAME )
            {
                s->close( );
            }
            else if ( opcode == WebSocketMessage::PING_FRAME )
            {
                s->send( make_shared< WebSocketMessage >( WebSocketMessage::PONG_FRAME, message->get_data( ) ) );
            }
            else if ( opcode == WebSocketMessage::TEXT_FRAME or opcode == WebSocketMessage::BINARY_FRAME )
            {
                s->send( make_shared< WebSocketMessage >( opcode, message->get_data( ) ) );
            }
        } );
    } );
}

extern "C" int LLVMFuzzerInitialize( int*, char*** )
{
    auto resource = make_shared< Resource >( );
    resource->set_path( "/ws" );
    resource->set_method_handler( "GET", get_handler );

    auto settings = make_shared< Settings >( );
    settings->set_web_socket_max_frame_size( 1 << 20 );

    static auto service = make_shared< Service >( );
    service->publish( resource );

    g_port = mayhem_loopback::start_service( service, settings );
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    std::vector< uint8_t > buffer( UPGRADE, UPGRADE + sizeof( UPGRADE ) - 1 );
    buffer.insert( buffer.end( ), data, data + size );
    mayhem_loopback::exchange( g_port, buffer.data( ), buffer.size( ) );
    return 0;
}

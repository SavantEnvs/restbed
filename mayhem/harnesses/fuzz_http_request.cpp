// Fuzz restbed's HTTP/1.x request parser through its public network interface:
// raw request bytes are sent over a loopback TCP connection to a real in-process
// restbed::Service (ServiceImpl::parse_request / request line / headers / version,
// Uri::parse and query decoding, resource routing and path parameters, typed
// header/query getters, Session::fetch by length and by delimiter, keep-alive).
#include "loopback.hpp"

#include <map>
#include <string>

using namespace restbed;
using std::make_shared;
using std::shared_ptr;
using std::string;

static uint16_t g_port = 0;

static void body_handler( const shared_ptr< Session > session )
{
    const auto request = session->get_request( );
    const size_t length = request->get_header( "Content-Length", 0 );
    const int limit = request->get_query_parameter( "limit", 0 );
    const string keep = request->get_header( "Connection", "close" );

    session->fetch( length, [ limit, keep ]( const shared_ptr< Session > session, const Bytes& body )
    {
        const auto req = session->get_request( );
        string reply = req->get_method( ) + " " + req->get_path( ) + " " + std::to_string( body.size( ) );
        for ( const auto& q : req->get_query_parameters( ) ) reply += " " + q.first + "=" + q.second;
        for ( const auto& p : req->get_path_parameters( ) ) reply += " " + p.first + "=" + p.second;
        if ( limit > 0 )
        {
            reply += " limit";
        }
        if ( String::lowercase( keep ) == "keep-alive" )
        {
            session->yield( 200, reply, { { "Content-Length", std::to_string( reply.size( ) ) } } );
        }
        else
        {
            session->close( 200, reply, { { "Content-Length", std::to_string( reply.size( ) ) } } );
        }
    } );
}

static void line_handler( const shared_ptr< Session > session )
{
    session->fetch( "\r\n", [ ]( const shared_ptr< Session > session, const Bytes& line )
    {
        const double v = session->get_request( )->get_header( "X-Number", 0.0 );
        session->close( 201, string( reinterpret_cast< const char* >( line.data( ) ), line.size( ) ) + std::to_string( v ) );
    } );
}

extern "C" int LLVMFuzzerInitialize( int*, char*** )
{
    auto fixed = make_shared< Resource >( );
    fixed->set_path( "/resource" );
    fixed->set_method_handler( "GET", body_handler );
    fixed->set_method_handler( "POST", body_handler );
    fixed->set_method_handler( "PUT", body_handler );

    auto params = make_shared< Resource >( );
    params->set_paths( { "/users/{id: [0-9]+}/items/{name: .*}", "/multi/{a: [a-z]*}" } );
    params->set_method_handler( "GET", body_handler );
    params->set_method_handler( "DELETE", body_handler );

    auto lines = make_shared< Resource >( );
    lines->set_path( "/line" );
    lines->set_method_handler( "POST", line_handler );
    lines->set_method_handler( "PATCH", line_handler );

    auto settings = make_shared< Settings >( );
    settings->set_case_insensitive_uris( true );
    settings->set_default_header( "Connection", "close" );

    static auto service = make_shared< Service >( );
    service->publish( fixed );
    service->publish( params );
    service->publish( lines );

    g_port = mayhem_loopback::start_service( service, settings );
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    mayhem_loopback::exchange( g_port, data, size );
    return 0;
}

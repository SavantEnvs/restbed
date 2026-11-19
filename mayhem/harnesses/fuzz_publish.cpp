// Fuzz resource path declarations through Service::publish / Service::suppress
// (ServiceImpl::sanitise_path, path-parameter declaration regex, route
// bookkeeping). The input is split on newlines into up to three resource paths.
#include <restbed>

#include <cstdint>
#include <exception>
#include <memory>
#include <set>
#include <string>

using namespace restbed;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    const std::string input( reinterpret_cast< const char* >( data ), size );
    std::set< std::string > paths;
    size_t start = 0;
    while ( paths.size( ) < 3 )
    {
        const size_t end = input.find( '\n', start );
        paths.insert( input.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
        if ( end == std::string::npos ) break;
        start = end + 1;
    }

    auto resource = std::make_shared< Resource >( );
    auto other = std::make_shared< Resource >( );
    try
    {
        resource->set_paths( paths );
        resource->set_method_handler( "GET", [ ]( const std::shared_ptr< Session > ) { } );
        other->set_path( *paths.begin( ) );

        Service service;
        service.publish( resource );
        try
        {
            service.publish( other );
        }
        catch ( const std::exception& )
        {
        }
        service.suppress( resource );
    }
    catch ( const std::exception& )
    {
    }

    return 0;
}

// Fuzz restbed::Uri: validation, parsing (absolute and relative), every getter
// (incl. get_port's numeric conversion and get_query_parameters), percent
// decoding/encoding, comparison operators.
#include <restbed>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

using restbed::Uri;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    if ( size < 1 )
    {
        return 0;
    }

    const bool relative = data[ 0 ] & 1;
    const std::string value( reinterpret_cast< const char* >( data + 1 ), size - 1 );

    try
    {
        (void) Uri::is_valid( value );
        const Uri uri( value, relative );
        (void) uri.get_port( );
        (void) uri.get_path( );
        (void) uri.get_query( );
        (void) uri.get_scheme( );
        (void) uri.get_fragment( );
        (void) uri.get_username( );
        (void) uri.get_password( );
        (void) uri.get_authority( );
        (void) uri.get_query_parameters( );
        (void) uri.is_relative( );
        const Uri copy = Uri::parse( uri.to_string( ) );
        (void) ( copy == uri );
        (void) ( copy < uri );
    }
    catch ( const std::exception& )
    {
    }

    try
    {
        (void) Uri::decode( value );
        (void) Uri::decode_parameter( value );
        (void) Uri::encode_parameter( value );
        // Round trip: percent-encoding then decoding must give back the input.
        if ( Uri::decode( Uri::encode( value ) ) not_eq value )
        {
            abort( );
        }
    }
    catch ( const std::exception& )
    {
    }

    return 0;
}

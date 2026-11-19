// Fuzz restbed::String: replace/remove (case sensitive and insensitive, which build
// regexes from the input), split, join, lowercase/uppercase, to_bytes.
#include <restbed>

#include <fuzzer/FuzzedDataProvider.h>

#include <cstdint>
#include <exception>
#include <map>
#include <string>

using restbed::String;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    FuzzedDataProvider fdp( data, size );
    const auto option = fdp.ConsumeBool( ) ? String::CASE_INSENSITIVE : String::CASE_SENSITIVE;
    const char delimiter = fdp.ConsumeIntegral< char >( );
    const std::string target = fdp.ConsumeRandomLengthString( 64 );
    const std::string substitute = fdp.ConsumeRandomLengthString( 64 );
    const std::string value = fdp.ConsumeRemainingBytesAsString( );

    try
    {
        (void) String::replace( target, substitute, value, option );
        (void) String::remove( target, value, option );
        const auto parts = String::split( value, delimiter );
        std::multimap< std::string, std::string > pairs;
        for ( size_t i = 0; i + 1 < parts.size( ); i += 2 )
        {
            pairs.emplace( parts[ i ], parts[ i + 1 ] );
        }
        (void) String::join( pairs, target, substitute );
        (void) String::lowercase( value );
        (void) String::uppercase( value );
        (void) String::to_bytes( value );
    }
    catch ( const std::exception& )
    {
    }

    return 0;
}

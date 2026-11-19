// Shared helpers for the restbed network harnesses: one in-process restbed::Service
// listening on 127.0.0.1 (OS-assigned port), and a blocking loopback client that
// sends one fuzz input per connection, half-closes, and drains the reply to EOF.
//
// State kept across inputs on purpose: the Service (its published resources and
// settings are fixed at start-up and never changed by an input). Each input gets
// its own TCP connection, so its result depends only on that input.
#pragma once

#include <restbed>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <future>
#include <memory>
#include <thread>

namespace mayhem_loopback
{
    inline uint16_t start_service( const std::shared_ptr< restbed::Service >& service,
                                   const std::shared_ptr< restbed::Settings >& settings )
    {
        signal( SIGPIPE, SIG_IGN );
        settings->set_port( 0 );
        settings->set_bind_address( "127.0.0.1" );

        auto ready = std::make_shared< std::promise< uint16_t > >( );
        service->set_ready_handler( [ ready ]( restbed::Service & s )
        {
            ready->set_value( s.get_http_uri( )->get_port( ) );
        } );

        std::thread( [ service, settings ]( )
        {
            service->start( settings );
        } ).detach( );

        return ready->get_future( ).get( );
    }

    // Send `data`, half-close, read until the server closes the connection.
    inline void exchange( uint16_t port, const uint8_t* data, size_t size )
    {
        int fd = socket( AF_INET, SOCK_STREAM, 0 );
        if ( fd < 0 )
        {
            perror( "socket" );
            abort( );
        }

        sockaddr_in addr { };
        addr.sin_family = AF_INET;
        addr.sin_port = htons( port );
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );

        if ( connect( fd, reinterpret_cast< sockaddr* >( &addr ), sizeof( addr ) ) not_eq 0 )
        {
            perror( "connect" );
            abort( );
        }

        size_t sent = 0;
        while ( sent < size )
        {
            ssize_t n = send( fd, data + sent, size - sent, MSG_NOSIGNAL );
            if ( n <= 0 )
            {
                if ( n < 0 and errno == EINTR ) continue;
                break; // server closed early (e.g. rejected the request)
            }
            sent += static_cast< size_t >( n );
        }

        shutdown( fd, SHUT_WR );

        char buffer[ 4096 ];
        for ( ;; )
        {
            ssize_t n = recv( fd, buffer, sizeof( buffer ), 0 );
            if ( n > 0 ) continue;
            if ( n < 0 and errno == EINTR ) continue;
            break;
        }

        close( fd );
    }
}

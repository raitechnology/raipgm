#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <raipgm/pgm_sock.h>
#include <hdr_histogram.h>


static const size_t PING_MSG_SIZE = 32;
struct PingMsg {
  uint64_t ping_src,
           time_sent,
           seqno_sent;
  char     pad[ PING_MSG_SIZE - sizeof( uint64_t ) * 3 ];
};

struct PgmSockData : public PgmSock {
  pgm_time_t   timeout_usecs,
               lost_tstamp;
  pgm_tsi_t    lost_tsi;
  uint32_t     lost_count;

  PgmSockData() : timeout_usecs( 0 ), lost_tstamp( 0 ), lost_count( 0 ) {}

  bool send( void *data,  size_t size ) noexcept;
  bool recv( void *data,  size_t &size ) noexcept;

  bool send_ping( uint64_t src, uint64_t now,  uint64_t seqno ) {
    PingMsg msg;
    msg.ping_src   = src;
    msg.time_sent  = now;
    msg.seqno_sent = seqno;
    return this->send( &msg, sizeof( msg ) );
  }
  bool recv_ping( uint64_t &src, uint64_t &stamp, uint64_t &seqno ) {
    PingMsg msg;
    size_t  len = sizeof( msg );
    if ( this->recv( &msg, len ) && len >= sizeof( msg ) ) {
      src   = msg.ping_src;
      stamp = msg.time_sent;
      seqno = msg.seqno_sent;
      return true;
    }
    return false;
  }
};
#if 0
static const char *
io_status_to_string( int status )
{
  switch ( status ) {
    case PGM_IO_STATUS_ERROR:         return "ERROR";
    case PGM_IO_STATUS_NORMAL:        return "NORMAL";
    case PGM_IO_STATUS_RESET:         return "RESET";
    case PGM_IO_STATUS_FIN:           return "FIN";
    case PGM_IO_STATUS_EOF:           return "EOF";
    case PGM_IO_STATUS_WOULD_BLOCK:   return "WOULD_BLOCK";
    case PGM_IO_STATUS_RATE_LIMITED:  return "RATE_LIMITED";
    case PGM_IO_STATUS_TIMER_PENDING: return "TIMER_PENDING";
    case PGM_IO_STATUS_CONGESTION:    return "CONGESTION";
    default:                          return "UNKNOWN";
  }
}
#endif
bool
PgmSockData::recv( void *data,  size_t &size ) noexcept
{
  const size_t      iov_len = 20;
  struct pgm_msgv_t msgv[ iov_len ];
  size_t            len;
  socklen_t         optlen;
  struct timeval    tv;

  int status = pgm_recvmsgv( this->sock, msgv, iov_len, MSG_ERRQUEUE, &len,
                             &this->pgm_err );

  this->timeout_usecs = 0;
  switch ( status ) {
    case PGM_IO_STATUS_NORMAL: {
      this->timeout_usecs = 0;
      if ( len > 0 ) {
        const struct pgm_sk_buff_t* pskb = msgv[ 0 ].msgv_skb[ 0 ];
        if ( size > pskb->len )
          size = pskb->len;
        memcpy( data, pskb->data, size );
        return true;
      }
      return false;
    }
    case PGM_IO_STATUS_TIMER_PENDING:
      optlen = sizeof( tv );
      pgm_getsockopt( this->sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv,
                      &optlen );
      this->timeout_usecs = tv.tv_sec * 1000000 + tv.tv_usec;
      return false;

    case PGM_IO_STATUS_RATE_LIMITED:
      optlen = sizeof( tv );
      pgm_getsockopt( this->sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv,
                      &optlen );
      this->timeout_usecs = tv.tv_sec * 1000000 + tv.tv_usec;
      return false;

    case PGM_IO_STATUS_WOULD_BLOCK:
      this->timeout_usecs = 1000;
      return false;

    case PGM_IO_STATUS_RESET: {
      struct pgm_sk_buff_t* skb = msgv[ 0 ].msgv_skb[ 0 ];
      this->lost_tstamp = skb->tstamp;
      if ( pgm_tsi_equal( &skb->tsi, &this->lost_tsi ) )
        this->lost_count += skb->sequence;
      else {
        this->lost_count = skb->sequence;
        memcpy( &this->lost_tsi, &skb->tsi, sizeof( pgm_tsi_t ) );
      }
      pgm_free_skb( skb );
      this->timeout_usecs = 0;
      return false;
    }
    default:
      if ( this->pgm_err != NULL ) {
        fprintf( stderr, "%s", this->pgm_err->message );
        pgm_error_free( this->pgm_err );
        this->pgm_err = NULL;
      }
      this->timeout_usecs = 0;
      return false;
  }
}

bool
PgmSockData::send( void *data, size_t size ) noexcept
{
  const size_t          header_size = pgm_pkt_offset( FALSE, 0 );
  struct pgm_sk_buff_t* skb         = pgm_alloc_skb( size + header_size );
  struct timeval        tv;
  socklen_t             optlen;
  size_t                bytes_written;
  int                   status;

  pgm_skb_reserve( skb, header_size );
  memcpy( pgm_skb_put( skb, size ), data, size );
  status = pgm_send_skbv( this->sock, &skb, 1, TRUE, &bytes_written );

  /*printf( "send: %s\n", io_status_to_string( status ) );*/
  switch ( status ) {
    case PGM_IO_STATUS_NORMAL:
      this->timeout_usecs = 0;
      return true;

    /* thse require sending the same data again */
    case PGM_IO_STATUS_RATE_LIMITED:
      optlen = sizeof( tv );
      pgm_getsockopt( this->sock, IPPROTO_PGM, PGM_TIME_REMAIN, &tv,
                      &optlen );
      this->timeout_usecs = tv.tv_sec * 1000000 + tv.tv_usec;
      return false;

    case PGM_IO_STATUS_CONGESTION:
    case PGM_IO_STATUS_WOULD_BLOCK:
      this->timeout_usecs = 0;
      return false;

    default:
      fprintf( stderr, "pgm_send_skbv failed, status:%d", status );
      return false;
  }
}

static const char *
get_arg( int argc, char *argv[], int b, const char *f, const char *def )
{
  for ( int i = 1; i < argc - b; i++ )
    if ( ::strcmp( f, argv[ i ] ) == 0 )
      return argv[ i + b ];
  return def; /* default value */
}

bool quit;

void
sigint_handler( int )
{
  quit = true;
}

uint64_t
current_time_nsecs( void )
{
  struct timespec ts;
  clock_gettime( CLOCK_MONOTONIC, &ts );
  return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int
main( int argc, char **argv )
{
  const char * ne = get_arg( argc, argv, 1, "-n", 0 ),
             * ct = get_arg( argc, argv, 1, "-c", 0 ),
             * re = get_arg( argc, argv, 0, "-r", 0 ),
             * bu = get_arg( argc, argv, 0, "-b", 0 ),
             * he = get_arg( argc, argv, 0, "-h", 0 );
  PgmSockData data;
  uint64_t    count = 0, warm = 0;
  
  if ( he != NULL || ne == NULL ) {
    fprintf( stderr,
             "%s -n network [-r] [-c count]\n"
             "  -n network = network to ping\n"
             "  -r         = reflect pings\n"
             "  -b         = busy wait\n"
             "  -c count   = number of pings\n", argv[ 0 ] );
    return 1;
  }
  if ( ct != NULL ) {
    if ( atoll( ct ) <= 0 ) {
      fprintf( stderr, "count should be > 0\n" );
    }
    count = (uint64_t) atoll( ct );
    warm  = count;
    count = warm / 100;
    if ( count == 0 )
      count = 1;
  }
  if ( ! data.start_pgm( ne, 7500 ) ) {
    fprintf( stderr, "create PGM/UDP socket failed\n" );
    return 1;
  }

  int efd = epoll_create1( 0 );
  if ( efd < 0 ||
       pgm_epoll_ctl( data.sock, efd, EPOLL_CTL_ADD, EPOLLIN ) < 0 ) {
    perror( "epoll" );
    return 1;
  }

  signal( SIGINT, sigint_handler );
  struct hdr_histogram * histogram = NULL;
  uint64_t ping_ival = 1000000000,
           last      = current_time_nsecs(),
           next      = last + ping_ival,
           seqno     = 0,
           my_id     = getpid(),
           src, stamp, num, delta, now;
  int      spin_cnt = 0;
  bool     reflect  = ( re != NULL ),
           busywait = ( bu != NULL );
  if ( ! reflect )
    hdr_init( 1, 1000000, 3, &histogram );
  while ( ! quit ) {
    if ( data.recv_ping( src, stamp, num ) ) {
      spin_cnt = 0;
      if ( src == my_id ) {
        now  = current_time_nsecs();
        next = now; /* send the next immediately */
        hdr_record_value( histogram, now - stamp );
        if ( count > 0 && --count == 0 ) {
          if ( warm > 0 ) {
             hdr_reset( histogram );
             count = warm;
             warm  = 0;
          }
          else {
            quit = true;
          }
        }
      }
      else { /* not mine, reflect */
        data.send_ping( src, stamp, num );
      }
    }
    /* activly send pings every second until response */
    else if ( ! reflect ) {
      now = current_time_nsecs();
      if ( now >= next ) {
        if ( data.send_ping( my_id, now, seqno ) ) {
          seqno++;
          last = now;
          next = now + ping_ival;
          data.timeout_usecs = 0;
          spin_cnt = 0;
        }
      }
      else {
        delta = next - now;
        if ( data.timeout_usecs > delta / 1000 )
          data.timeout_usecs = delta / 1000;
      }
    }

    if ( data.lost_count != 0 ) {
      pgm_time_t elapsed = pgm_time_update_now() - data.lost_tstamp;
      if ( elapsed >= pgm_secs( 1 ) ) {
        fprintf( stderr, "pgm data lost %u packets detected from %s",
                   data.lost_count, pgm_tsi_print( &data.lost_tsi ) );
        data.lost_count = 0;
      }
    }
    if ( ! busywait && data.timeout_usecs != 0 ) {
      if ( spin_cnt++ > 1000 ) {
        struct epoll_event event;
        uint64_t ms = data.timeout_usecs / 1000;
        epoll_wait( efd, &event, 1, ms );
      }
    }
  }
  data.close_pgm();
  if ( ! reflect )
    hdr_percentiles_print( histogram, stdout, 5, 1000.0, CLASSIC );
  return 0;
}

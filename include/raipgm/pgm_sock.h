#ifndef __rai_raipgm__pgm_sock_h__
#define __rai_raipgm__pgm_sock_h__

#include <pgm/pgm.h>

struct PgmSock {
  pgm_sock_t     * sock;
  pgm_error_t    * pgm_err;
  pgm_addrinfo_t * res;
  int              tpdu,               /* ip + udp + pgm + data */
                   txw_sqns,           /* transmit window */
                   rxw_sqns,           /* receive widnow */
                   ambient_spm,        /* SPM at this interval */
                   heartbeat_spm[ 9 ], /* HB after sends */
                   peer_expiry,         /* peers expire after last pkt/SPM */
                   spmr_expiry,         /* interval for SPMR peer requests */
                   nak_bo_ivl,          /* back off interval */
                   nak_rpt_ivl,         /* repeat interval */
                   nak_rdata_ivl,       /* wait for repair data */
                   nak_data_retry,      /* count of repair retries */
                   nak_ncf_retry,       /* count of nak confirm retries */
                   mcast_loop,          /* loopback to host */
                   mcast_hops;          /* ttl */
  bool             is_connected;

  PgmSock() : sock( 0 ), pgm_err( 0 ), res( 0 ),
      tpdu          ( 1500 ),
      txw_sqns      ( 1000 ),
      rxw_sqns      ( 100 ),
      ambient_spm   ( pgm_secs( 30 ) ),
      peer_expiry   ( pgm_secs( 600 ) ),
      spmr_expiry   ( pgm_msecs( 250 ) ),
      nak_bo_ivl    ( pgm_msecs( 50 ) ),
      nak_rpt_ivl   ( pgm_msecs( 200 ) ),
      nak_rdata_ivl ( pgm_msecs( 400 ) ),
      nak_data_retry( 50 ),
      nak_ncf_retry ( 50 ),
      mcast_loop    ( 0 ),
      mcast_hops    ( 16 ),
      is_connected  ( false )
  {
    int hb_spm[ 9 ] =
      { pgm_msecs( 100 ), pgm_msecs( 100 ),  pgm_msecs( 100 ),
        pgm_msecs( 100 ), pgm_msecs( 1300 ), pgm_secs( 7 ),
        pgm_secs( 16 ),   pgm_secs( 25 ),    pgm_secs( 30 ) };
    memcpy( this->heartbeat_spm, hb_spm, sizeof( hb_spm ) );
  }

  bool start_pgm( const char *network,  int svc );

  void close_pgm();
};

#endif

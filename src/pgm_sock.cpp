#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <raipgm/pgm_sock.h>

bool
PgmSock::start_pgm( const char *network,  int svc )
{
  if ( this->pgm_err != NULL ) {
    pgm_error_free( this->pgm_err );
    this->pgm_err = NULL;
  }
  if ( this->res != NULL ) {
    pgm_freeaddrinfo( this->res );
    this->res = NULL;
  }
  if ( ! pgm_init( &this->pgm_err ) )
    return false;
  if ( ! pgm_getaddrinfo( network, NULL, &this->res, &this->pgm_err ) ) {
    fprintf( stderr, "parsing network \"%s\": %s\n", network,
             this->pgm_err->message );
    return false;
  }
  sa_family_t sa_family = this->res->ai_send_addrs[ 0 ].gsr_group.ss_family;
  if ( ! pgm_socket( &this->sock, sa_family, SOCK_SEQPACKET, IPPROTO_UDP,
                     &this->pgm_err ) ) {
    fprintf( stderr, "socket: %s\n", this->pgm_err->message );
    return false;
  }
  bool b;
  b = pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_UDP_ENCAP_UCAST_PORT,
                      &svc, sizeof( svc ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_UDP_ENCAP_MCAST_PORT,
                      &svc, sizeof( svc ) );

  int is_uncontrolled = 1; /* uncontrolled odata, rdata */

  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_MTU, &this->tpdu,
                      sizeof( this->tpdu ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_UNCONTROLLED_ODATA,
                      &is_uncontrolled, sizeof( is_uncontrolled ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_UNCONTROLLED_RDATA,
                      &is_uncontrolled, sizeof( is_uncontrolled ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_TXW_SQNS, &this->txw_sqns,
                      sizeof( this->txw_sqns ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_AMBIENT_SPM,
                      &this->ambient_spm, sizeof( this->ambient_spm ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_HEARTBEAT_SPM,
                      this->heartbeat_spm, sizeof( this->heartbeat_spm ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_RXW_SQNS, &this->rxw_sqns,
                      sizeof( this->rxw_sqns ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_PEER_EXPIRY,
                      &this->peer_expiry, sizeof( this->peer_expiry ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_SPMR_EXPIRY,
                      &this->spmr_expiry, sizeof( this->spmr_expiry ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NAK_BO_IVL,
                      &this->nak_bo_ivl, sizeof( this->nak_bo_ivl ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NAK_RPT_IVL,
                      &this->nak_rpt_ivl, sizeof( this->nak_rpt_ivl ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NAK_RDATA_IVL,
                      &this->nak_rdata_ivl, sizeof( this->nak_rdata_ivl ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NAK_DATA_RETRIES,
                      &this->nak_data_retry, sizeof( this->nak_data_retry ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NAK_NCF_RETRIES,
                      &this->nak_ncf_retry, sizeof( this->nak_ncf_retry ) );

  if ( !b )
    return false;
  /* create global session identifier */
  struct pgm_sockaddr_t addr;
  memset( &addr, 0, sizeof( addr ) );
  addr.sa_port       = svc;
  addr.sa_addr.sport = DEFAULT_DATA_SOURCE_PORT;
  if ( ! pgm_gsi_create_from_hostname( &addr.sa_addr.gsi, &this->pgm_err ) ) {
    fprintf( stderr, "creating GSI: %s\n", this->pgm_err->message );
    return false;
  }
  /* assign socket to specified address */
  struct pgm_interface_req_t if_req;
  memset( &if_req, 0, sizeof( if_req ) );
  if_req.ir_interface = this->res->ai_recv_addrs[ 0 ].gsr_interface;
  if_req.ir_scope_id  = 0;
  if ( AF_INET6 == sa_family ) {
    struct sockaddr_in6 sa6;
    memcpy( &sa6, &this->res->ai_recv_addrs[ 0 ].gsr_group, sizeof( sa6 ) );
    if_req.ir_scope_id = sa6.sin6_scope_id;
  }
  if ( ! pgm_bind3( this->sock, &addr, sizeof( addr ), &if_req,
                    sizeof( if_req ),          /* tx interface */
                    &if_req, sizeof( if_req ), /* rx interface */
                    &this->pgm_err ) )
    return false;

  /* join IP multicast groups */
  for ( unsigned i = 0; i < this->res->ai_recv_addrs_len; i++ ) {
    if ( ! pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_JOIN_GROUP,
                           &this->res->ai_recv_addrs[ i ],
                           sizeof( struct group_req ) ) ) {
      char group[ INET6_ADDRSTRLEN ];
      getnameinfo( (struct sockaddr*) &this->res->ai_recv_addrs[ i ].gsr_group,
                   sizeof( struct sockaddr_in ), group, sizeof( group ), NULL,
                   0, NI_NUMERICHOST );
      fprintf( stderr, "setting PGM_JOIN_GROUP = { #%u %s }\n",
               (unsigned) this->res->ai_recv_addrs[ i ].gsr_interface, group );
      return false;
    }
  }
  if ( ! pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_SEND_GROUP,
                         &this->res->ai_send_addrs[ 0 ],
                         sizeof( struct group_req ) ) ) {
    char group[ INET6_ADDRSTRLEN ];
    getnameinfo( (struct sockaddr*) &this->res->ai_send_addrs[ 0 ].gsr_group,
                 sizeof( struct sockaddr_in ), group, sizeof( group ), NULL, 0,
                 NI_NUMERICHOST );
    fprintf( stderr, "setting PGM_SEND_GROUP = { #%u %s }\n",
             (unsigned) this->res->ai_send_addrs[ 0 ].gsr_interface, group );
    return false;
  }
  /* set IP parameters */
  const int nonblocking = 1;

  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_MULTICAST_LOOP,
                      &this->mcast_loop, sizeof( this->mcast_loop ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_MULTICAST_HOPS,
                      &this->mcast_hops, sizeof( this->mcast_hops ) );
  b&= pgm_setsockopt( this->sock, IPPROTO_PGM, PGM_NOBLOCK, &nonblocking,
                      sizeof( nonblocking ) );
  b&= pgm_connect( this->sock, &this->pgm_err );
  this->is_connected = b;

  return b;
}

void
PgmSock::close_pgm( void )
{
  if ( this->sock != NULL ) {
    pgm_close( this->sock, this->is_connected );
    this->sock = NULL;
    this->is_connected = false;
  }
  if ( this->pgm_err != NULL ) {
    pgm_error_free( this->pgm_err );
    this->pgm_err = NULL;
  }
  if ( this->res != NULL ) {
    pgm_freeaddrinfo( this->res );
    this->res = NULL;
  }
}


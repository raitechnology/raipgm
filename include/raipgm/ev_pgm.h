#ifndef __rai_raipgm__ev_pgm_h__
#define __rai_raipgm__ev_pgm_h__

#include <raikv/ev_net.h>
#include <raikv/kv_msg.h>
#include <raikv/kv_pubsub.h>
#include <raikv/route_ht.h>
#include <raikv/uint_ht.h>

namespace rai {
namespace pgm {

/* subscription route table element */
struct PgmSubRoute {
  uint32_t hash;       /* hash of subject */
  uint32_t sub;        /* the list of zipped routes for sub */
  uint16_t len;        /* length of subject string */
  char     value[ 2 ]; /* the subject string */
  bool equals( const void *s,  uint16_t l ) const {
    return l == this->len && ::memcmp( s, this->value, l ) == 0;
  }
  void copy( const void *s,  uint16_t l ) {
    ::memcpy( this->value, s, l );
  }
};
/* subscription route status */
enum PgmSubStatus {
  PGM_SUB_OK        = 0,
  PGM_SUB_NEW       = 1,
  PGM_SUB_EXISTS    = 2,
  PGM_SUB_NOT_FOUND = 3,
  PGM_SUB_REMOVED   = 4
};
/* subscription route table iterator */
struct PgmSubRoutePos {
  PgmSubRoute * rt;
  uint32_t v;
  uint16_t off;
};
/* subscription route table */
struct PgmSubMap {
  kv::RouteVec<PgmSubRoute> tab; /* ht of subject to zip ids */
  kv::RouteZip zip;                /* id hash to id array */

  bool is_null( void ) const {     /* if no subs */
    return this->tab.vec_size == 0;
  }
  size_t sub_count( void ) const { /* count of unique subs */
    return this->tab.pop_count();
  }
  void release( void ) {           /* free ht */
    this->tab.release();
    this->zip.reset();
  }
  /* add id to subject route */
  PgmSubStatus put( uint32_t h,  const char *sub,  size_t len, uint32_t id ) {
    kv::RouteLoc loc;
    PgmSubRoute * rt = this->tab.upsert( h, sub, len, loc );
    if ( rt == NULL )
      return PGM_SUB_NOT_FOUND;
    if ( loc.is_new ) {
      rt->sub = PgmSubMap::make_sub( id );
      return PGM_SUB_NEW;              /* if new subscription */
    }
    return PgmSubMap::merge_sub( this->zip, rt->sub, id );
  }
  /* remove id from subject route */
  PgmSubStatus rem( uint32_t h,  const char *sub,  size_t len, uint32_t id ) {
    kv::RouteLoc loc;
    PgmSubRoute * rt = this->tab.find( h, sub, len, loc );
    PgmSubStatus x;
    if ( rt == NULL )
      return PGM_SUB_NOT_FOUND;
    x = PgmSubMap::remove_sub( this->zip, rt->sub, id );
    if ( x == PGM_SUB_REMOVED )
      this->tab.remove( loc );
    return x;
  }
  /* iterate first tab[ sub ] */
  bool first( PgmSubRoutePos &pos ) {
    pos.rt = this->tab.first( pos.v, pos.off );
    return pos.rt != NULL;
  }
  /* iterate next tab[ sub ] */
  bool next( PgmSubRoutePos &pos ) {
    pos.rt = this->tab.next( pos.v, pos.off );
    return pos.rt != NULL;
  }
  /* after insert of first id in tab[ sub ] */
  static uint32_t make_sub( uint32_t i ) noexcept;
  /* merge insert id in tab[ sub ] */
  static PgmSubStatus merge_sub( kv::RouteZip &zip,  uint32_t &sub,
                                   uint32_t i ) noexcept;
  /* remove id in from tab[ sub ] */
  static PgmSubStatus remove_sub( kv::RouteZip &zip,  uint32_t &sub,
                                    uint32_t i ) noexcept;
  void print( void ) noexcept;
};
/* pattern sub route table element */
struct PgmPatternSubRoute {
  uint32_t hash;       /* hash of pattern prefix */
  uint32_t sub;        /* list of zipped routes for pattern */
  uint16_t len,        /* length of prefix and pattern */
           pref;       /* length of prefix */
  char     value[ 4 ]; /* prefix string and pattern string */
  bool equals( const void *s,  uint16_t ) const {
    return ::strcmp( (const char *) s, this->value ) == 0;
  }
  bool prefix_equals( const void *s,  uint16_t preflen ) const {
    return preflen == this->prefix_len() &&
           ::memcmp( s, this->prefix(), preflen ) == 0;
  }
  void copy( const void *s,  uint16_t l ) {
    ::memcpy( this->value, s, l );
  }
  const char *prefix( void ) const {
    return &this->value[ this->len - ( this->pref + 1 ) ];
  }
  size_t prefix_len( void ) const {
    return this->pref;
  }
  const char *pattern( void ) const {
    return this->value;
  }
  size_t pattern_len( void ) const {
    return this->len - ( this->pref + 2 );
  }
};
/* pattern sub route list elem */
struct PgmTmpElem {
  PgmTmpElem       * next,
                     * back;
  PgmPatternSubRoute x;    /* a pattern sub route element */
  void * operator new( size_t, void *ptr ) { return ptr; }
  PgmTmpElem( const PgmPatternSubRoute &rt )
      : next( 0 ), back( 0 ) {
    this->x.hash = rt.hash;
    this->x.len  = rt.len;
    this->x.pref = rt.pref;
    ::memcpy( this->x.value, rt.value, rt.len );
  }
  PgmTmpElem( const PgmSubRoute &rt )
      : next( 0 ), back( 0 ) {
    this->x.hash = rt.hash;
    this->x.len  = rt.len;
    this->x.pref = 0;
    ::memcpy( this->x.value, rt.value, rt.len );
  }
  static size_t alloc_size( const PgmPatternSubRoute &rt ) {
    return sizeof( PgmTmpElem ) + rt.len;
  }
  static size_t alloc_size( const PgmSubRoute &rt ) {
    return sizeof( PgmTmpElem ) + rt.len;
  }
};
/* pattern sub route list, used to remove several patterns at a time */
struct PgmTmpList {
  kv::WorkAllocT< 1024 > wrk;
  kv::DLinkList<PgmTmpElem> list;

  void append( const PgmPatternSubRoute &rt ) {
    void * p = this->wrk.alloc( PgmTmpElem::alloc_size( rt ) );
    this->list.push_tl( new ( p ) PgmTmpElem( rt ) );
  }
  void append( const PgmSubRoute &rt ) {
    void * p = this->wrk.alloc( PgmTmpElem::alloc_size( rt ) );
    this->list.push_tl( new ( p ) PgmTmpElem( rt ) );
  }
};
/* pattern sub route table iterator */
struct PgmPatternSubRoutePos {
  PgmPatternSubRoute * rt;
  uint32_t v;
  uint16_t off;
};
/* pattern sub route table */
struct PgmPatternSubMap {
  kv::RouteVec<PgmPatternSubRoute> tab; /* ht of sub pattern to zip ids */
  kv::RouteZip zip;                       /* id hash to id array */

  bool is_null( void ) const { /* test if no patterns are subscribed */
    return this->tab.vec_size == 0;
  }
  size_t sub_count( void ) const { /* return count of unique patterns */
    return this->tab.pop_count();
  }
  void release( void ) { /* release patterns table */
    this->tab.release();
    this->zip.reset();
  }
  /* add id to list of routes for a pattern prefix */
  PgmSubStatus put( uint32_t h,  const char *sub,  size_t len,
                    size_t pref,  uint32_t id ) {
    kv::RouteLoc loc;
    PgmPatternSubRoute * rt = this->tab.upsert( h, sub, len, loc );
    if ( rt == NULL )
      return PGM_SUB_NOT_FOUND;
    if ( loc.is_new ) {
      rt->pref = pref;
      rt->sub  = PgmSubMap::make_sub( id );
      return PGM_SUB_NEW;              /* if new subscription */
    }
    return PgmSubMap::merge_sub( this->zip, rt->sub, id );
  }
  /* remove id from list of routes for a pattern prefix */
  PgmSubStatus rem( uint32_t h,  const char *prefix,  size_t preflen,
                    uint32_t id,  PgmTmpList &tmp ) {
    kv::RouteLoc loc;
    PgmPatternSubRoute * rt = this->tab.find_by_hash( h, loc );
    if ( rt == NULL )
      return PGM_SUB_NOT_FOUND;
    do {
      if ( rt->prefix_equals( prefix, preflen ) ) {
        PgmSubStatus x;
        x = PgmSubMap::remove_sub( this->zip, rt->sub, id );
        if ( x == PGM_SUB_REMOVED )
          tmp.append( *rt );
      }
    } while ( (rt = this->tab.find_next_by_hash( h, loc )) != NULL );
    for ( PgmTmpElem *el = tmp.list.hd; el != NULL; el = el->next )
      this->tab.remove( h, el->x.value, el->x.len );
    return PGM_SUB_OK;
  }
  /* iterate first tab[ sub ] */
  bool first( PgmPatternSubRoutePos &pos ) {
    pos.rt = this->tab.first( pos.v, pos.off );
    return pos.rt != NULL;
  }
  /* iterate next tab[ sub ] */
  bool next( PgmPatternSubRoutePos &pos ) {
    pos.rt = this->tab.next( pos.v, pos.off );
    return pos.rt != NULL;
  }
  void print( void ) noexcept;
};

enum SessionState {
  SESSION_NEW      = 1, /* set initially, cleared after subs are sent */
  SESSION_DATALOSS = 2, /* when seqno is missing */
  SESSION_TIMEOUT  = 4, /* when timer expires after no heartbeats */
  SESSION_BYE      = 8  /* if session closes */
};

struct PgmSession {
  PgmSession    * next,        /* link in MyPeers::list or MyPeers::free_list */
                * back,
                * next_id,     /* link in session_idx[] collision chain */
                * last_id;
  kv::KvFragAsm * frag;
  const uint64_t  stamp;       /* identifies session uniquely */
  uint64_t        last_active, /* time in ns of last message recvd */
                  last_seqno,  /* seqno of last message recvd */
                  delta_seqno, /* if missing seqno, this is delta missing */
                  pub_count;   /* count of msgs published */
  const uint32_t  id;          /* id is index into sessions[] */
  uint32_t        sub_count,   /* count of subscriptions */
                  psub_count,  /* count of pattern subs */
                  state;       /* state of session, bits of SessionState */

  void     set( SessionState fl )        { this->state |= (uint32_t) fl; }
  uint32_t test( SessionState fl ) const { return this->state & (uint32_t) fl; }
  void     clear( void )                 { this->state = 0; }
  void     clear( SessionState fl )      { this->state &= ~(uint32_t) fl; }

  void * operator new( size_t, void *ptr ) { return ptr; }
  PgmSession( uint32_t i,  uint64_t stmp = 0,  uint64_t seq = 0,
                PgmSession *nid = 0 )
    : next( 0 ), back( 0 ), next_id( nid ), last_id( 0 ), frag( 0 ),
      stamp( stmp ), last_active( 0 ), last_seqno( seq ), delta_seqno( 1 ),
      pub_count( 0 ), id( i ), sub_count( 0 ), psub_count( 0 ),
      state( SESSION_NEW ) {
    if ( nid != NULL )
      nid->last_id = this;
  }
};

struct MyPeers {
  kv::DLinkList<PgmSession>
                    list,           /* ordered list, hd = last active */
                    free_list;      /* free sessions */
  kv::UIntHashTab * session_idx;    /* idx of sessions[] */
  PgmSession      * last_session,   /* last sessions[] used */
                 ** sessions;       /* array of sessions */
  uint32_t          session_size,   /* size of net_ses[] array */
                    ping_idx;       /* ping peers */
  PgmSession        dummy_session;  /* a null session */
  uint64_t          last_check_ns;  /* last timeout check */
  MyPeers() noexcept;

  static uint32_t hash( uint64_t stamp ) { /* hash of stamp */
    return (uint32_t) stamp ^ (uint32_t) ( stamp >> 32 );
  }
  /* find session and update last seqno seen */
  PgmSession *update_session( uint64_t stamp,  uint64_t seqno ) {
    if ( this->last_session->stamp == stamp )
      return this->update_last( seqno );

    uint32_t h = hash( stamp ), pos, id;
    if ( this->session_idx->find( h, pos, id ) ) {
      this->last_session = this->sessions[ id ];
      while ( this->last_session->stamp != stamp ) {
        this->last_session = this->last_session->next_id;
        if ( this->last_session == NULL )
          return this->new_session( stamp, seqno, h, pos, this->sessions[ id ]);
      }
      this->list.pop( this->last_session );
      this->list.push_hd( this->last_session );
      return this->update_last( seqno );
    }
    return this->new_session( stamp, seqno, h, pos, NULL );
  }
  /* update the last_session seen */
  PgmSession *update_last( uint64_t seqno ) {
    this->last_session->delta_seqno = seqno - this->last_session->last_seqno;
    if ( this->last_session->delta_seqno != 1 )
      this->last_session->set( SESSION_DATALOSS );
    else
      this->last_session->clear( SESSION_TIMEOUT );
    this->last_session->last_seqno = seqno; 
    return this->last_session;
  }
  /* allocate new session and insert into session_idx[] */
  PgmSession *new_session( uint64_t stamp,  uint64_t seqno,
                             uint32_t h,  uint32_t pos,
                             PgmSession *next_id ) noexcept;
  /* unlink session and put on free list */
  void release_session( PgmSession &session ) noexcept;
  /* check whether a session timed out */
  PgmSession *check_timeout( uint64_t age_ns ) {
    if ( age_ns > this->last_check_ns ) {
      this->last_check_ns = age_ns;
      if ( this->list.tl != NULL ) {
        if ( this->list.tl->last_active < age_ns ) {
          if ( this->list.tl->test( SESSION_TIMEOUT ) )
            return this->list.tl;
          this->list.tl->set( SESSION_TIMEOUT );
        }
      }
    }
    return NULL;
  }
  uint64_t next_ping( void ) noexcept {
    uint32_t j = this->ping_idx;
    for ( uint32_t i = 0; i < this->session_size; i++ ) {
      if ( j >= this->session_size )
        j = 0;
      if ( this->sessions[ j ] != NULL ) {
        this->ping_idx = j + 1;
        return this->sessions[ j ]->stamp;
      }
      j++;
    }
    return 0;
  }
  void print( void ) noexcept;
  void release( void ) noexcept;
};

struct MySubs {
  kv::UIntHashTab * subsc_idx;   /* subscriptions active internal */
  uint32_t        * subs;        /* array of subscription msgs */
  uint32_t          subs_free,   /* count of free message words */
                    subs_off,    /* end of subs[] words array */
                    subs_size;   /* alloc words size of subs[] array */
  MySubs() noexcept;
  void gc( void ) noexcept;
  void upsert( kv::KvSubMsg &msg ) noexcept;
  void remove( kv::KvSubMsg &msg ) noexcept;
  void remove_pattern( kv::KvSubMsg &msg ) noexcept;
  uint32_t append( kv::KvSubMsg &msg ) noexcept;
  static uint32_t subs_align( uint32_t sz ) {
    return kv::align<uint32_t>( sz, 4 );
  }
  void print( kv::EvPoll &poll ) noexcept;
  void release( void ) noexcept;
};

struct PgmSvcId {
  uint32_t pub_if,  sub_if;
  uint16_t pub_svc, sub_svc;
};

struct EvPgm : public kv::EvSocket, public kv::KvSendQueue,
               public kv::RouteNotify {
  enum {
    AE_FLAG_INIT         = 1,
    AE_FLAG_SHUTDOWN     = 2,
    AE_FLAG_BACKPRESSURE = 4
  };

  pgm_sock_t     * sock;
  PgmSubMap        sub_tab;     /* active subscriptions */
  PgmPatternSubMap pat_sub_tab; /* active wildcards */
  MyPeers          my_peers;
  MySubs           my_subs;
  uint64_t         next_timer_id,
                   timer_id,
                   cur_mono_ns;
  uint32_t         max_payload_len,
                   timer_count,
                   shutdown_count,
                   aeron_flags;

  uint32_t test_ae( uint32_t fl ) const { return this->aeron_flags & fl; }
  void set_ae( uint32_t fl )   { this->aeron_flags |= fl; }
  void clear_ae( uint32_t fl ) { this->aeron_flags &= ~fl; }

  void * operator new( size_t, void *ptr ) { return ptr; }
  EvPgm( kv::EvPoll &p ) noexcept;
  static EvPgm *create_aeron( kv::EvPoll &p ) noexcept;
  bool start_aeron( PgmSvcId *id,  const char *pub_channel,
                    int pub_stream_id,  const char *sub_channel,
                    int sub_stream_id ) noexcept;
  void do_shutdown( void ) noexcept;

  /* EvSocket */
  virtual void write( void ) noexcept final;
  virtual void read( void ) noexcept final;
  virtual void process( void ) noexcept final;
  virtual void release( void ) noexcept final;
  virtual bool timer_expire( uint64_t timer_id, uint64_t event_id ) noexcept;
  virtual void process_shutdown( void ) noexcept final;
  virtual void process_close( void ) noexcept final;
  virtual bool on_msg( kv::EvPublish &pub ) noexcept;
  virtual bool busy_poll( void ) noexcept;

  bool init_pubsub( const char *pub_channel,  int pub_stream_id,
                    const char *sub_channel,  int sub_stream_id ) noexcept;
  bool finish_init( void ) noexcept;
  void release_aeron( void ) noexcept;
  static void poll_handler( void *clientd,  const uint8_t *buffer,
                            size_t length,  aeron_header_t *header );
  void on_poll_handler( const uint8_t *buffer,  size_t length,
                        aeron_header_t *header ) noexcept;
  /* RouteNotify */
  virtual void on_sub( uint32_t h,  const char *sub,  size_t sublen,
                    uint32_t src_fd,  uint32_t rcnt,  char src_type,
                    const char *rep,  size_t rlen ) noexcept;
  virtual void on_unsub( uint32_t h,  const char *sub,  size_t sublen,
                    uint32_t src_fd,  uint32_t rcnt,  char src_type ) noexcept;
  virtual void on_psub( uint32_t h,  const char *pattern,  size_t patlen,
                    const char *prefix,  uint8_t prefix_len,
                    uint32_t src_fd,  uint32_t rcnt,  char src_type ) noexcept;
  virtual void on_punsub( uint32_t h,  const char *pattern,  size_t patlen,
                    const char *prefix,  uint8_t prefix_len,
                    uint32_t src_fd,  uint32_t rcnt,  char src_type ) noexcept;
  virtual void on_connect( void ) noexcept;

  void publish_my_subs( void ) noexcept;
  void send_dataloss( PgmSession &session ) noexcept;
  void clear_session( PgmSession &session ) noexcept;
  void clear_subs( PgmSession &session ) noexcept;
  void clear_pattern_subs( PgmSession &session ) noexcept;
  void clear_all_subs( void ) noexcept;
  void print_stats( void ) noexcept;
  void start_shutdown( void ) noexcept;
  bool check_shutdown( void ) noexcept;
};

}
}
#endif

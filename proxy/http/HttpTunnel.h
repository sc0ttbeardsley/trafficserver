/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

   HttpTunnel.h

   Description:


****************************************************************************/

#ifndef _HTTP_TUNNEL_H_
#define _HTTP_TUNNEL_H_

#include "libts.h"
#include "P_EventSystem.h"

// Get rid of any previous definition first... /leif
#ifdef MAX_PRODUCERS
#undef MAX_PRODUCERS
#endif
#ifdef MAX_CONSUMERS
#undef MAX_CONSUMERS
#endif
#define MAX_PRODUCERS   2
#define MAX_CONSUMERS   4

#define HTTP_TUNNEL_EVENT_DONE             (HTTP_TUNNEL_EVENTS_START + 1)
#define HTTP_TUNNEL_EVENT_PRECOMPLETE      (HTTP_TUNNEL_EVENTS_START + 2)
#define HTTP_TUNNEL_EVENT_CONSUMER_DETACH  (HTTP_TUNNEL_EVENTS_START + 3)

#define HTTP_TUNNEL_STATIC_PRODUCER  (VConnection*)!0

//YTS Team, yamsat Plugin
#define ALLOCATE_AND_WRITE_TO_BUF 1
#define WRITE_TO_BUF 2

struct HttpTunnelProducer;
class HttpSM;
class HttpPagesHandler;
typedef int (HttpSM::*HttpSMHandler) (int event, void *data);

struct HttpTunnelConsumer;
struct HttpTunnelProducer;
typedef int (HttpSM::*HttpProducerHandler) (int event, HttpTunnelProducer * p);
typedef int (HttpSM::*HttpConsumerHandler) (int event, HttpTunnelConsumer * c);

enum HttpTunnelType_t
{
  HT_HTTP_SERVER,
  HT_HTTP_CLIENT,
  HT_CACHE_READ,
  HT_CACHE_WRITE,
  HT_TRANSFORM,
  HT_STATIC
};

enum TunnelChunkingAction_t
{
  TCA_CHUNK_CONTENT,
  TCA_DECHUNK_CONTENT,
  TCA_PASSTHRU_CHUNKED_CONTENT,
  TCA_PASSTHRU_DECHUNKED_CONTENT
};

struct ChunkedHandler
{
  enum ChunkedState {
    CHUNK_READ_CHUNK = 0,
    CHUNK_READ_SIZE_START,
    CHUNK_READ_SIZE,
    CHUNK_READ_SIZE_CRLF,
    CHUNK_READ_TRAILER_BLANK,
    CHUNK_READ_TRAILER_CR,
    CHUNK_READ_TRAILER_LINE,
    CHUNK_READ_ERROR,
    CHUNK_READ_DONE,
    CHUNK_WRITE_CHUNK,
    CHUNK_WRITE_DONE,
    CHUNK_FLOW_CONTROL
  };

  static int const DEFAULT_MAX_CHUNK_SIZE = 4096;

  enum Action {
    ACTION_DOCHUNK = 0,
    ACTION_DECHUNK,
    ACTION_PASSTHRU,
  };

  Action action;

  IOBufferReader *chunked_reader;
  MIOBuffer *dechunked_buffer;
  int64_t dechunked_size;

  IOBufferReader *dechunked_reader;
  MIOBuffer *chunked_buffer;
  int64_t chunked_size;

  bool truncation;
  int64_t skip_bytes;

  ChunkedState state;
  int64_t cur_chunk_size;
  int64_t bytes_left;
  int last_server_event;

  // Parsing Info
  int running_sum;
  int num_digits;

  /// @name Output data.
  //@{
  /// The maximum chunk size.
  /// This is the preferred size as well, used whenever possible.
  int64_t max_chunk_size;
  /// Caching members to avoid using printf on every chunk.
  /// It holds the header for a maximal sized chunk which will cover
  /// almost all output chunks.
  char max_chunk_header[16];
  int max_chunk_header_len;
  //@}
  ChunkedHandler();

  void init(IOBufferReader *buffer_in, HttpTunnelProducer *p);
  void init_by_action(IOBufferReader *buffer_in, Action action);
  void clear();

  /// Set the max chunk @a size.
  /// If @a size is zero it is set to @c DEFAULT_MAX_CHUNK_SIZE.
  void set_max_chunk_size(int64_t size);

  // Returns true if complete, false otherwise
  bool process_chunked_content();
  bool generate_chunked_content();

private:
  void read_size();
  void read_chunk();
  void read_trailer();
  int64_t transfer_bytes();
};

struct HttpTunnelConsumer
{
  HttpTunnelConsumer();

  LINK(HttpTunnelConsumer, link);
  HttpTunnelProducer *producer;
  HttpTunnelProducer *self_producer;

  HttpTunnelType_t vc_type;
  VConnection *vc;
  IOBufferReader *buffer_reader;
  HttpConsumerHandler vc_handler;
  VIO *write_vio;

  int64_t skip_bytes;               // bytes to skip at beginning of stream
  int64_t bytes_written;            // total bytes written to the vc
  int handler_state;              // state used the handlers

  bool alive;
  bool write_success;
  const char *name;

  /** Check if this consumer is downstream from @a vc.
      @return @c true if any producer in the tunnel eventually feeds
      data to this consumer.
  */
  bool is_downstream_from(VConnection* vc);
  /** Check if this is a sink (final data destination).
      @return @c true if data exits the ATS process at this consumer.
  */
  bool is_sink() const;
};

struct HttpTunnelProducer
{
  HttpTunnelProducer();

  DLL<HttpTunnelConsumer> consumer_list;
  HttpTunnelConsumer *self_consumer;
  VConnection *vc;
  HttpProducerHandler vc_handler;
  VIO *read_vio;
  MIOBuffer *read_buffer;
  IOBufferReader *buffer_start;
  HttpTunnelType_t vc_type;

  ChunkedHandler chunked_handler;
  TunnelChunkingAction_t chunking_action;

  bool do_chunking;
  bool do_dechunking;
  bool do_chunked_passthru;

  int64_t init_bytes_done;          // bytes passed in buffer
  int64_t nbytes;                   // total bytes (client's perspective)
  int64_t ntodo;                    // what this vc needs to do
  int64_t bytes_read;               // total bytes read from the vc
  int handler_state;              // state used the handlers
  int last_event;                   ///< Tracking for flow control restarts.

  int num_consumers;

  bool alive;
  bool read_success;
  /// Flag and pointer for active flow control throttling.
  /// If this is set, it points at the source producer that is under flow control.
  /// If @c NULL then data flow is not being throttled.
  HttpTunnelProducer* flow_control_source;
  const char *name;

  /** Get the largest number of bytes any consumer has not consumed.
      Use @a limit if you only need to check if the backlog is at least @a limit.
      @return The actual backlog or a number at least @a limit.
   */
  uint64_t backlog(
		   uint64_t limit = UINT64_MAX ///< More than this is irrelevant
		   );
  /// Check if producer is original (to ATS) source of data.
  /// @return @c true if this producer is the source of bytes from outside ATS.
  bool is_source() const;
  /// Throttle the flow.
  void throttle();
  /// Unthrottle the flow.
  void unthrottle();
  /// Check throttled state.
  bool is_throttled() const;

  /// Update the handler_state member if it is still 0
  void  update_state_if_not_set(int new_handler_state);

  /** Set the flow control source producer for the flow.
      This sets the value for this producer and all downstream producers.
      @note This is the implementation for @c throttle and @c unthrottle.
      @see throttle
      @see unthrottle
  */
  void set_throttle_src(
			HttpTunnelProducer* srcp ///< Source producer of flow.
			);
};

class PostDataBuffers
{
public:
  PostDataBuffers()
    : postdata_producer_buffer(NULL), postdata_copy_buffer(NULL), postdata_producer_reader(NULL),
      postdata_copy_buffer_start(NULL), ua_buffer_reader(NULL)
  { Debug("http_redirect", "[PostDataBuffers::PostDataBuffers]");  }

  MIOBuffer *postdata_producer_buffer;
  MIOBuffer *postdata_copy_buffer;
  IOBufferReader *postdata_producer_reader;
  IOBufferReader *postdata_copy_buffer_start;
  IOBufferReader *ua_buffer_reader;
};

class HttpTunnel:public Continuation
{
  friend class HttpPagesHandler;
  friend class CoreUtils;

  /** Data for implementing flow control across a tunnel.

      The goal is to bound the amount of data buffered for a
      transaction flowing through the tunnel to (roughly) between the
      @a high_water and @a low_water water marks. Due to the chunky nater of data
      flow this always approximate.
  */
  struct FlowControl {
    // Default value for high and low water marks.
    static uint64_t const DEFAULT_WATER_MARK = 1<<16;

    uint64_t high_water; ///< Buffered data limit - throttle if more than this.
    uint64_t low_water; ///< Unthrottle if less than this buffered.
    bool enabled_p; ///< Flow control state (@c false means disabled).

    /// Default constructor.
    FlowControl();
  };

public:
  HttpTunnel();

  void init(HttpSM * sm_arg, ProxyMutex * amutex);
  void reset();
  void kill_tunnel();
  bool is_tunnel_active() const { return active; }
  bool is_tunnel_alive() const;
  bool has_cache_writer() const;

  // YTS Team, yamsat Plugin
  void copy_partial_post_data();
  void allocate_redirect_postdata_producer_buffer();
  void allocate_redirect_postdata_buffers(IOBufferReader * ua_reader);
  void deallocate_redirect_postdata_buffers();

  HttpTunnelProducer *add_producer(VConnection * vc,
                                   int64_t nbytes,
                                   IOBufferReader * reader_start,
                                   HttpProducerHandler sm_handler, HttpTunnelType_t vc_type, const char *name);

  void set_producer_chunking_action(HttpTunnelProducer * p, int64_t skip_bytes, TunnelChunkingAction_t action);
  /// Set the maximum (preferred) chunk @a size of chunked output for @a producer.
  void set_producer_chunking_size(HttpTunnelProducer* producer, int64_t size);

  HttpTunnelConsumer *add_consumer(VConnection * vc,
                                   VConnection * producer,
                                   HttpConsumerHandler sm_handler,
                                   HttpTunnelType_t vc_type, const char *name, int64_t skip_bytes = 0);

  int deallocate_buffers();
  DLL<HttpTunnelConsumer> *get_consumers(VConnection * vc);
  HttpTunnelProducer *get_producer(VConnection * vc);
  HttpTunnelConsumer *get_consumer(VConnection * vc);
  void tunnel_run(HttpTunnelProducer * p = NULL);

  int main_handler(int event, void *data);
  bool consumer_reenable(HttpTunnelConsumer* c);
  bool consumer_handler(int event, HttpTunnelConsumer * c);
  bool producer_handler(int event, HttpTunnelProducer * p);
  int producer_handler_dechunked(int event, HttpTunnelProducer * p);
  int producer_handler_chunked(int event, HttpTunnelProducer * p);
  void local_finish_all(HttpTunnelProducer * p);
  void chain_finish_all(HttpTunnelProducer * p);
  void chain_abort_cache_write(HttpTunnelProducer * p);
  void chain_abort_all(HttpTunnelProducer * p);
  void abort_cache_write_finish_others(HttpTunnelProducer * p);
  void append_message_to_producer_buffer(HttpTunnelProducer * p, const char *msg, int64_t msg_len);

  /** Mark a producer and consumer as the same underlying object.

      This is use to chain producer/consumer pairs together to
      indicate the data flows through them sequentially. The primary
      example is a transform which serves as a consumer on the server
      side and a producer on the cache/client side.
  */
  void chain(
	     HttpTunnelConsumer* c,  ///< Flow goes in here
	     HttpTunnelProducer* p   ///< Flow comes back out here
	     );

  void close_vc(HttpTunnelProducer * p);
  void close_vc(HttpTunnelConsumer * c);

private:

  void internal_error();
  void finish_all_internal(HttpTunnelProducer * p, bool chain);
  void update_stats_after_abort(HttpTunnelType_t t);
  void producer_run(HttpTunnelProducer * p);

  HttpTunnelProducer *get_producer(VIO * vio);
  HttpTunnelConsumer *get_consumer(VIO * vio);

  HttpTunnelProducer *alloc_producer();
  HttpTunnelConsumer *alloc_consumer();

  int num_producers;
  int num_consumers;
  HttpTunnelConsumer consumers[MAX_CONSUMERS];
  HttpTunnelProducer producers[MAX_PRODUCERS];
  HttpSM *sm;

  bool active;

  /// State data about flow control.
  FlowControl flow_state;

public:
  PostDataBuffers * postbuf;
};

// void HttpTunnel::abort_cache_write_finish_others
//
//    Abort all downstream cache writes and finsish
//      all other local consumers
//
inline void
HttpTunnel::abort_cache_write_finish_others(HttpTunnelProducer * p)
{
  chain_abort_cache_write(p);
  local_finish_all(p);
}

// void HttpTunnel::local_finish_all(HttpTunnelProducer* p)
//
//   After the producer has finished, causes direct consumers
//      to finish their writes
//
inline void
HttpTunnel::local_finish_all(HttpTunnelProducer * p)
{
  finish_all_internal(p, false);
}

// void HttpTunnel::chain_finish_all(HttpTunnelProducer* p)
//
//   After the producer has finished, cause everyone
//    downstream in the tunnel to send everything
//    that producer has placed in the buffer
//
inline void
HttpTunnel::chain_finish_all(HttpTunnelProducer * p)
{
  finish_all_internal(p, true);
}

inline bool
HttpTunnel::is_tunnel_alive() const
{
  bool tunnel_alive = false;

  for (int i = 0; i < MAX_PRODUCERS; i++) {
    if (producers[i].alive == true) {
      tunnel_alive = true;
      break;
    }
  }
  if (!tunnel_alive) {
    for (int i = 0; i < MAX_CONSUMERS; i++) {
      if (consumers[i].alive == true) {
        tunnel_alive = true;
        break;
      }
    }

  }

  return tunnel_alive;
}

inline HttpTunnelProducer *
HttpTunnel::get_producer(VConnection * vc)
{
  for (int i = 0; i < MAX_PRODUCERS; i++) {
    if (producers[i].vc == vc) {
      return producers + i;
    }
  }
  return NULL;
}

inline HttpTunnelConsumer *
HttpTunnel::get_consumer(VConnection * vc)
{
  for (int i = 0; i < MAX_CONSUMERS; i++) {
    if (consumers[i].vc == vc) {
      return consumers + i;
    }
  }
  return NULL;
}

inline HttpTunnelProducer *
HttpTunnel::get_producer(VIO * vio)
{
  for (int i = 0; i < MAX_PRODUCERS; i++) {
    if (producers[i].read_vio == vio) {
      return producers + i;
    }
  }
  return NULL;
}

inline HttpTunnelConsumer *
HttpTunnel::get_consumer(VIO * vio)
{
  for (int i = 0; i < MAX_CONSUMERS; i++) {
    if (consumers[i].write_vio == vio) {
      return consumers + i;
    }
  }
  return NULL;
}

inline void
HttpTunnel::append_message_to_producer_buffer(HttpTunnelProducer * p, const char *msg, int64_t msg_len)
{
  if (p == NULL || p->read_buffer == NULL)
    return;

  p->read_buffer->write(msg, msg_len);
  p->nbytes += msg_len;
  p->bytes_read += msg_len;
}

inline bool
HttpTunnel::has_cache_writer() const
{
  for (int i = 0; i < MAX_CONSUMERS; i++) {
    if (consumers[i].vc_type == HT_CACHE_WRITE && consumers[i].vc != NULL) {
      return true;
    }
  }
  return false;
}

inline bool
HttpTunnelConsumer::is_downstream_from(VConnection *vc)
{
  HttpTunnelProducer* p = producer;
  HttpTunnelConsumer* c;
  while (p) {
    if (p->vc == vc) return true;
    // The producer / consumer chain can contain a cycle in the case
    // of a blind tunnel so give up if we find ourself (the original
    // consumer).
    c = p->self_consumer;
    p = (c && c != this) ? c->producer : 0;
  }
  return false;
}

inline bool
HttpTunnelConsumer::is_sink() const
{
  return HT_HTTP_CLIENT == vc_type || HT_CACHE_WRITE == vc_type;
}

inline bool
HttpTunnelProducer::is_source() const
{
  // If a producer is marked as a client, then it's part of a bidirectional tunnel
  // and so is an actual source of data.
  return HT_HTTP_SERVER == vc_type || HT_CACHE_READ == vc_type || HT_HTTP_CLIENT == vc_type;
}

inline void
HttpTunnelProducer::update_state_if_not_set(int new_handler_state)
{
  if (this->handler_state == 0) {
    this->handler_state = new_handler_state;
  }
}

inline bool
HttpTunnelProducer::is_throttled() const
{
  return 0 != flow_control_source;
}

inline void
HttpTunnelProducer::throttle()
{
  if (!this->is_throttled())
    this->set_throttle_src(this);
}

inline void
HttpTunnelProducer::unthrottle()
{
  if (this->is_throttled())
    this->set_throttle_src(0);
}

inline
HttpTunnel::FlowControl::FlowControl()
	  : high_water(DEFAULT_WATER_MARK)
	  , low_water(DEFAULT_WATER_MARK)
	  , enabled_p(false)
{
}

#endif

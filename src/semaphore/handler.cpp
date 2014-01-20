// (C) 2014 Cybozu.

#include "handler.hpp"

#include "../config.hpp"
#include "stats.hpp"
#include "sockets.hpp"

namespace yrmcds { namespace semaphore {

semaphore_handler::semaphore_handler(const std::function<cybozu::worker*()>& finder,
                                     const std::function<bool()>& is_slave,
                                     cybozu::reactor& reactor):
    m_finder(finder),
    m_is_slave(is_slave),
    m_reactor(reactor),
    m_hash(g_config.semaphore().buckets()) {
}

void semaphore_handler::on_start() {
    cybozu::tcp_server_socket::wrapper w =
        [this](int s, const cybozu::ip_address&) {
            return make_semaphore_socket(s);
        };
    std::unique_ptr<cybozu::tcp_server_socket> ss =
        cybozu::make_server_socket(nullptr, g_config.semaphore().port(), w);
    m_reactor.add_resource(std::move(ss), cybozu::reactor::EVENT_IN);
}

std::unique_ptr<cybozu::tcp_socket> semaphore_handler::make_semaphore_socket(int s) {
    if( m_is_slave() )
        return nullptr;

    unsigned mc = g_config.semaphore().max_connections();
    if( mc != 0 &&
        g_stats.curr_connections.load(std::memory_order_relaxed) >= mc )
        return nullptr;

    return std::unique_ptr<cybozu::tcp_socket>(new semaphore_socket(s, m_finder, m_hash));
}

bool semaphore_handler::gc_ready() {
    std::time_t now = std::time(nullptr);

    if( m_gc_thread.get() != nullptr ) {
        if( ! m_gc_thread->done() )
            return false;
        m_last_gc = now;
        m_gc_thread = nullptr;  // join
    }

    return now > m_last_gc + g_config.semaphore().gc_interval();
}

void semaphore_handler::on_master_interval() {
    if( gc_ready() ) {
        m_gc_thread = std::unique_ptr<gc_thread>(new gc_thread(m_hash));
        m_gc_thread->start();
    }
}

void semaphore_handler::on_master_end() {
    m_gc_thread = nullptr; // join
}

void semaphore_handler::on_clear() {
    for( auto& bucket: m_hash )
        bucket.clear_nolock();
    g_stats.total_objects.store(0, std::memory_order_relaxed);
}


}} // namespace yrmcds::semaphore

#include <gtest/gtest.h>
#include <boost/thread.hpp>
#include <mu_coin/mu_coin.hpp>

TEST (network, construction)
{
    boost::asio::io_service service;
    mu_coin::block_store store (mu_coin::block_store_temp);
    mu_coin::ledger ledger (store);
    mu_coin::node node1 (service, 24001, ledger);
    node1.receive ();
}

TEST (network, send_keepalive)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    node1.send_keepalive (node2.socket.local_endpoint ());
    while (node1.keepalive_ack_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.keepalive_req_count);
    ASSERT_EQ (1, node1.keepalive_ack_count);
}

TEST (network, publish_req_invalid_point)
{
    auto block (std::unique_ptr <mu_coin::transaction_block> (new mu_coin::transaction_block));
    block->entries.push_back (mu_coin::entry ());
    block->entries.back ().id.address.point.bytes.fill (0xff);
    mu_coin::publish_req req (std::move (block));
    mu_coin::byte_write_stream stream;
    req.serialize (stream);
    mu_coin::publish_req req2;
    mu_coin::byte_read_stream stream2 (stream.data, stream.size);
    auto error (req2.deserialize (stream2));
    ASSERT_TRUE (error);
}

TEST (network, publish_req)
{
    auto block (std::unique_ptr <mu_coin::transaction_block> (new mu_coin::transaction_block));
    mu_coin::keypair key1;
    mu_coin::keypair key2;
    block->entries.push_back (mu_coin::entry (key1.pub, 100, 200));
    block->entries.push_back (mu_coin::entry (key2.pub, 300, 400));
    auto message (block->hash ());
    block->entries [0].sign (key1.prv, message);
    block->entries [1].sign (key2.prv, message);
    mu_coin::publish_req req (std::move (block));
    mu_coin::byte_write_stream stream;
    req.serialize (stream);
    mu_coin::publish_req req2;
    mu_coin::byte_read_stream stream2 (stream.data, stream.size);
    auto error (req2.deserialize (stream2));
    ASSERT_FALSE (error);
    ASSERT_EQ (*req.block, *req2.block);
}

TEST (network, send_discarded_publish)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    std::unique_ptr <mu_coin::transaction_block> block (new mu_coin::transaction_block);
    mu_coin::entry entry;
    block->entries.push_back (entry);
    node1.publish_transaction_block (node2.socket.local_endpoint (), std::move (block));
    while (node2.publish_req_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (0, node1.publish_nak_count);
}

TEST (network, send_invalid_publish)
{
    boost::asio::io_service service;
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    std::unique_ptr <mu_coin::transaction_block> block (new mu_coin::transaction_block);
    mu_coin::keypair key1;
    mu_coin::entry entry (key1.pub, 10, 0);
    block->entries.push_back (entry);
    block->entries [0].sign (key1.prv, block->hash ());
    node1.publish_transaction_block (node2.socket.local_endpoint (), std::move (block));
    while (node1.publish_nak_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (1, node1.publish_nak_count);
}

TEST (network, send_valid_publish)
{
    boost::asio::io_service service;
    mu_coin::keypair key1;
    mu_coin::transaction_block block1;
    mu_coin::entry entry1 (key1.pub, 100, 0);
    block1.entries.push_back (entry1);
    block1.entries [0].sign (key1.prv, block1.hash ());
    mu_coin::block_store store1 (mu_coin::block_store_temp);
    store1.insert_block (entry1.id, block1);
    mu_coin::ledger ledger1 (store1);
    mu_coin::node node1 (service, 24001, ledger1);
    mu_coin::block_store store2 (mu_coin::block_store_temp);
    store2.insert_block (entry1.id, block1);
    mu_coin::ledger ledger2 (store2);
    mu_coin::node node2 (service, 24002, ledger2);
    node1.receive ();
    node2.receive ();
    mu_coin::keypair key2;
    mu_coin::transaction_block block2;
    mu_coin::entry entry2 (key2.pub, 50, 0);
    mu_coin::entry entry3 (key1.pub, 49, 1);
    block2.entries.push_back (entry2);
    block2.entries.push_back (entry3);
    block2.entries [0].sign (key2.prv, block2.hash ());
    block2.entries [1].sign (key1.prv, block2.hash ());
    node1.publish_transaction_block (node2.socket.local_endpoint (), std::unique_ptr <mu_coin::transaction_block> (new mu_coin::transaction_block (block2)));
    while (node1.publish_ack_count == 0)
    {
        service.run_one ();
    }
    ASSERT_EQ (1, node2.publish_req_count);
    ASSERT_EQ (1, node1.publish_ack_count);
    auto block3 (store2.latest (entry2.id.address));
    ASSERT_NE (nullptr, block3);
    auto block4 (store2.latest (entry3.id.address));
    ASSERT_NE (nullptr, block4);
    ASSERT_EQ (block2, *block3);
    ASSERT_EQ (block2, *block4);
}
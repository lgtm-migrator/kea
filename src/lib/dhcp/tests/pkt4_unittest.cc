// Copyright (C) 2011-2022 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>

#include <asiolink/io_address.h>
#include <dhcp/dhcp4.h>
#include <dhcp/libdhcp++.h>
#include <dhcp/docsis3_option_defs.h>
#include <dhcp/option_int.h>
#include <dhcp/option_string.h>
#include <dhcp/option4_addrlst.h>
#include <dhcp/option_vendor.h>
#include <dhcp/pkt4.h>
#include <exceptions/exceptions.h>
#include <testutils/gtest_utils.h>
#include <util/buffer.h>
#include <util/encode/hex.h>
#include <pkt_captures.h>

#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/static_assert.hpp>
#include <gtest/gtest.h>

#include <iostream>
#include <sstream>

#include <arpa/inet.h>

using namespace std;
using namespace isc;
using namespace isc::asiolink;
using namespace isc::dhcp;
using namespace isc::util;
// Don't import the entire boost namespace.  It will unexpectedly hide uint8_t
// for some systems.
using boost::scoped_ptr;

namespace {

/// V4 Options being used for pack/unpack testing.
/// For test simplicity, all selected options have
/// variable length data so as there are no restrictions
/// on a length of their data.
static uint8_t v4_opts[] = {
    53, 1, 2, // Message Type (required to not throw exception during unpack)
    12,  3, 0,   1,  2, // Hostname
    14,  3, 10, 11, 12, // Merit Dump File
    60,  3, 20, 21, 22, // Class Id
    128, 3, 30, 31, 32, // Vendor specific
    254, 3, 40, 41, 42, // Reserved
};

// Sample data
const uint8_t dummyOp = BOOTREQUEST;
const uint8_t dummyHtype = 6;
const uint8_t dummyHlen = 6;
const uint8_t dummyHops = 13;
const uint32_t dummyTransid = 0x12345678;
const uint16_t dummySecs = 42;
const uint16_t dummyFlags = BOOTP_BROADCAST;

const IOAddress dummyCiaddr("192.0.2.1");
const IOAddress dummyYiaddr("1.2.3.4");
const IOAddress dummySiaddr("192.0.2.255");
const IOAddress dummyGiaddr("255.255.255.255");

// a dummy MAC address
const uint8_t dummyMacAddr[] = {0, 1, 2, 3, 4, 5};

// A dummy MAC address, padded with 0s
const uint8_t dummyChaddr[16] = {0, 1, 2, 3, 4, 5, 0, 0,
                                 0, 0, 0, 0, 0, 0, 0, 0 };

// Let's use some creative test content here (128 chars + \0)
const uint8_t dummyFile[] = "Lorem ipsum dolor sit amet, consectetur "
    "adipiscing elit. Proin mollis placerat metus, at "
    "lacinia orci ornare vitae. Mauris amet.";

// Yet another type of test content (64 chars + \0)
const uint8_t dummySname[] = "Lorem ipsum dolor sit amet, consectetur "
    "adipiscing elit posuere.";

BOOST_STATIC_ASSERT(sizeof(dummyFile)  == Pkt4::MAX_FILE_LEN + 1);
BOOST_STATIC_ASSERT(sizeof(dummySname) == Pkt4::MAX_SNAME_LEN + 1);


class Pkt4Test : public ::testing::Test {
public:
    Pkt4Test() {
    }

    /// @brief Generates test packet.
    ///
    /// Allocates and generates test packet, with all fixed fields set to non-zero
    /// values. Content is not always reasonable.
    ///
    /// See generateTestPacket2() function that returns exactly the same packet in
    /// on-wire format.
    ///
    /// @return pointer to allocated Pkt4 object.
    Pkt4Ptr generateTestPacket1() {

        boost::shared_ptr<Pkt4> pkt(new Pkt4(DHCPDISCOVER, dummyTransid));

        vector<uint8_t> vectorMacAddr(dummyMacAddr, dummyMacAddr
                                      + sizeof(dummyMacAddr));

        // hwType = 6(ETHERNET), hlen = 6(MAC address len)
        pkt->setHWAddr(dummyHtype, dummyHlen, vectorMacAddr);
        pkt->setHops(dummyHops); // 13 relays. Wow!
        // Transaction-id is already set.
        pkt->setSecs(dummySecs);
        pkt->setFlags(dummyFlags); // all flags set
        pkt->setCiaddr(dummyCiaddr);
        pkt->setYiaddr(dummyYiaddr);
        pkt->setSiaddr(dummySiaddr);
        pkt->setGiaddr(dummyGiaddr);
        // Chaddr already set with setHWAddr().
        pkt->setSname(dummySname, 64);
        pkt->setFile(dummyFile, 128);

        return (pkt);
    }

    /// @brief Generates test packet.
    ///
    /// Allocates and generates on-wire buffer that represents test packet, with all
    /// fixed fields set to non-zero values.  Content is not always reasonable.
    ///
    /// See generateTestPacket1() function that returns exactly the same packet as
    /// Pkt4 object.
    ///
    /// @return pointer to allocated Pkt4 object
    // Returns a vector containing a DHCPv4 packet header.
    vector<uint8_t> generateTestPacket2() {

        // That is only part of the header. It contains all "short" fields,
        // larger fields are constructed separately.
        uint8_t hdr[] = {
            1, 6, 6, 13,            // op, htype, hlen, hops,
            0x12, 0x34, 0x56, 0x78, // transaction-id
            0, 42, 0x80, 0x00,      // 42 secs, BROADCAST flags
            192, 0, 2, 1,           // ciaddr
            1, 2, 3, 4,             // yiaddr
            192, 0, 2, 255,         // siaddr
            255, 255, 255, 255,     // giaddr
        };

        // Initialize the vector with the header fields defined above.
        vector<uint8_t> buf(hdr, hdr + sizeof(hdr));

        // Append the large header fields.
        copy(dummyChaddr, dummyChaddr + Pkt4::MAX_CHADDR_LEN, back_inserter(buf));
        copy(dummySname, dummySname + Pkt4::MAX_SNAME_LEN, back_inserter(buf));
        copy(dummyFile, dummyFile + Pkt4::MAX_FILE_LEN, back_inserter(buf));

        // Should now have all the header, so check.  The "static_cast" is used
        // to get round an odd bug whereby the linker appears not to find the
        // definition of DHCPV4_PKT_HDR_LEN if it appears within an EXPECT_EQ().
        EXPECT_EQ(static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN), buf.size());

        return (buf);
    }

    /// @brief Verify that the options are correct after parsing.
    ///
    /// @param pkt A packet holding parsed options.
    void verifyParsedOptions(const Pkt4Ptr& pkt) {
        EXPECT_TRUE(pkt->getOption(12));
        EXPECT_TRUE(pkt->getOption(60));
        EXPECT_TRUE(pkt->getOption(14));
        EXPECT_TRUE(pkt->getOption(128));
        EXPECT_TRUE(pkt->getOption(254));

        // Verify the packet type is correct.
        ASSERT_EQ(DHCPOFFER, pkt->getType());

        // First option after message type starts at 3.
        uint8_t *opt_data_ptr = v4_opts + 3;

        // Option 12 is represented by the OptionString class so let's do
        // the appropriate conversion.
        boost::shared_ptr<Option> x = pkt->getOption(12);
        ASSERT_TRUE(x); // option 1 should exist
        OptionStringPtr option12 = boost::static_pointer_cast<OptionString>(x);

        ASSERT_TRUE(option12);
        EXPECT_EQ(12, option12->getType());  // this should be option 12
        ASSERT_EQ(3, option12->getValue().length()); // it should be of length 3
        EXPECT_EQ(5, option12->len()); // total option length 5
        EXPECT_EQ(0, memcmp(&option12->getValue()[0], opt_data_ptr + 2, 2)); // data len=3
        opt_data_ptr += x->len();

        x = pkt->getOption(14);
        ASSERT_TRUE(x); // option 14 should exist
        // Option 14 is represented by the OptionString class so let's do
        // the appropriate conversion.
        OptionStringPtr option14 = boost::static_pointer_cast<OptionString>(x);
        ASSERT_TRUE(option14);
        EXPECT_EQ(14, option14->getType());  // this should be option 14
        ASSERT_EQ(3, option14->getValue().length()); // it should be of length 3
        EXPECT_EQ(5, option14->len()); // total option length 5

        EXPECT_EQ(0, memcmp(&option14->getValue()[0], opt_data_ptr + 2, 3)); // data len=3
        opt_data_ptr += x->len();

        x = pkt->getOption(60);
        ASSERT_TRUE(x); // option 60 should exist
        EXPECT_EQ(60, x->getType());  // this should be option 60
        ASSERT_EQ(3, x->getData().size()); // it should be of length 3
        EXPECT_EQ(5, x->len()); // total option length 5
        EXPECT_EQ(0, memcmp(&x->getData()[0], opt_data_ptr + 2, 3)); // data len=3
        opt_data_ptr += x->len();

        x = pkt->getOption(128);
        ASSERT_TRUE(x); // option 3 should exist
        EXPECT_EQ(128, x->getType());  // this should be option 254
        ASSERT_EQ(3, x->getData().size()); // it should be of length 3
        EXPECT_EQ(5, x->len()); // total option length 5
        EXPECT_EQ(0, memcmp(&x->getData()[0], opt_data_ptr + 2, 3)); // data len=3
        opt_data_ptr += x->len();

        x = pkt->getOption(254);
        ASSERT_TRUE(x); // option 3 should exist
        EXPECT_EQ(254, x->getType());  // this should be option 254
        ASSERT_EQ(3, x->getData().size()); // it should be of length 3
        EXPECT_EQ(5, x->len()); // total option length 5
        EXPECT_EQ(0, memcmp(&x->getData()[0], opt_data_ptr + 2, 3)); // data len=3
    }

};


TEST_F(Pkt4Test, constructor) {

    ASSERT_EQ(236U, static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN) );
    scoped_ptr<Pkt4> pkt;

    // Just some dummy payload.
    uint8_t testData[250];
    for (uint8_t i = 0; i < 250; i++) {
        testData[i] = i;
    }

    // Positive case1. Normal received packet.
    EXPECT_NO_THROW(pkt.reset(new Pkt4(testData, Pkt4::DHCPV4_PKT_HDR_LEN)));

    EXPECT_EQ(static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN), pkt->len());

    EXPECT_NO_THROW(pkt.reset());

    // Positive case2. Normal outgoing packet.
    EXPECT_NO_THROW(pkt.reset(new Pkt4(DHCPDISCOVER, 0xffffffff)));

    // DHCPv4 packet must be at least 236 bytes long, with Message Type
    // Option taking extra 3 bytes it is 239
    EXPECT_EQ(static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN) + 3, pkt->len());
    EXPECT_EQ(DHCPDISCOVER, pkt->getType());
    EXPECT_EQ(0xffffffff, pkt->getTransid());
    EXPECT_NO_THROW(pkt.reset());

    // Negative case. Should drop truncated messages.
    EXPECT_THROW(
        pkt.reset(new Pkt4(testData, Pkt4::DHCPV4_PKT_HDR_LEN - 1)),
        OutOfRange
    );
}


TEST_F(Pkt4Test, fixedFields) {

    boost::shared_ptr<Pkt4> pkt = generateTestPacket1();

    // OK, let's check packet values
    EXPECT_EQ(dummyOp, pkt->getOp());
    EXPECT_EQ(dummyHtype, pkt->getHtype());
    EXPECT_EQ(dummyHlen, pkt->getHlen());
    EXPECT_EQ(dummyHops, pkt->getHops());
    EXPECT_EQ(dummyTransid, pkt->getTransid());
    EXPECT_EQ(dummySecs, pkt->getSecs());
    EXPECT_EQ(dummyFlags, pkt->getFlags());

    EXPECT_EQ(dummyCiaddr, pkt->getCiaddr());
    EXPECT_EQ(dummyYiaddr, pkt->getYiaddr());
    EXPECT_EQ(dummySiaddr, pkt->getSiaddr());
    EXPECT_EQ(dummyGiaddr, pkt->getGiaddr());

    // Chaddr contains link-layer addr (MAC). It is no longer always 16 bytes
    // long and its length depends on hlen value (it is up to 16 bytes now).
    ASSERT_EQ(pkt->getHWAddr()->hwaddr_.size(), dummyHlen);
    EXPECT_EQ(0, memcmp(dummyChaddr, &pkt->getHWAddr()->hwaddr_[0], dummyHlen));

    EXPECT_EQ(0, memcmp(dummySname, &pkt->getSname()[0], 64));

    EXPECT_EQ(0, memcmp(dummyFile, &pkt->getFile()[0], 128));

    EXPECT_EQ(DHCPDISCOVER, pkt->getType());
}

TEST_F(Pkt4Test, fixedFieldsPack) {
    boost::shared_ptr<Pkt4> pkt = generateTestPacket1();
    vector<uint8_t> expectedFormat = generateTestPacket2();

    EXPECT_NO_THROW(
        pkt->pack();
    );

    // Minimum packet size is 236 bytes + 3 bytes of mandatory
    // DHCP Message Type Option
    ASSERT_EQ(static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN) + 3, pkt->len());

    // Redundant but MUCH easier for debug in gdb
    const uint8_t* exp = &expectedFormat[0];
    const uint8_t* got = static_cast<const uint8_t*>(pkt->getBuffer().getData());

    EXPECT_EQ(0, memcmp(exp, got, Pkt4::DHCPV4_PKT_HDR_LEN));
}

/// TODO Uncomment when ticket #1226 is implemented
TEST_F(Pkt4Test, fixedFieldsUnpack) {
    vector<uint8_t> expectedFormat = generateTestPacket2();

    expectedFormat.push_back(0x63); // magic cookie
    expectedFormat.push_back(0x82);
    expectedFormat.push_back(0x53);
    expectedFormat.push_back(0x63);

    expectedFormat.push_back(0x35); // message-type
    expectedFormat.push_back(0x1);
    expectedFormat.push_back(0x1);

    boost::shared_ptr<Pkt4> pkt(new Pkt4(&expectedFormat[0],
                                         expectedFormat.size()));;


    EXPECT_NO_THROW(
        pkt->unpack()
    );

    // OK, let's check packet values
    EXPECT_EQ(dummyOp, pkt->getOp());
    EXPECT_EQ(dummyHtype, pkt->getHtype());
    EXPECT_EQ(dummyHlen, pkt->getHlen());
    EXPECT_EQ(dummyHops, pkt->getHops());
    EXPECT_EQ(dummyTransid, pkt->getTransid());
    EXPECT_EQ(dummySecs, pkt->getSecs());
    EXPECT_EQ(dummyFlags, pkt->getFlags());

    EXPECT_EQ(dummyCiaddr, pkt->getCiaddr());
    EXPECT_EQ("1.2.3.4", pkt->getYiaddr().toText());
    EXPECT_EQ("192.0.2.255", pkt->getSiaddr().toText());
    EXPECT_EQ("255.255.255.255", pkt->getGiaddr().toText());

    // chaddr is always 16 bytes long and contains link-layer addr (MAC)
    EXPECT_EQ(0, memcmp(dummyChaddr, &pkt->getHWAddr()->hwaddr_[0], dummyHlen));

    ASSERT_EQ(static_cast<size_t>(Pkt4::MAX_SNAME_LEN), pkt->getSname().size());
    EXPECT_EQ(0, memcmp(dummySname, &pkt->getSname()[0], Pkt4::MAX_SNAME_LEN));

    ASSERT_EQ(static_cast<size_t>(Pkt4::MAX_FILE_LEN), pkt->getFile().size());
    EXPECT_EQ(0, memcmp(dummyFile, &pkt->getFile()[0], Pkt4::MAX_FILE_LEN));

    EXPECT_EQ(DHCPDISCOVER, pkt->getType());
}

// This test is for hardware addresses (htype, hlen and chaddr fields)
TEST_F(Pkt4Test, hwAddr) {

    vector<uint8_t> mac;
    uint8_t expectedChaddr[Pkt4::MAX_CHADDR_LEN];

    // We resize vector to specified length. It is more natural for fixed-length
    // field, than clear it (shrink size to 0) and push_back each element
    // (growing length back to MAX_CHADDR_LEN).
    mac.resize(Pkt4::MAX_CHADDR_LEN);

    scoped_ptr<Pkt4> pkt;
    // let's test each hlen, from 0 till 16
    for (size_t macLen = 0; macLen < Pkt4::MAX_CHADDR_LEN; macLen++) {
        for (size_t i = 0; i < Pkt4::MAX_CHADDR_LEN; i++) {
            mac[i] = 0;
            expectedChaddr[i] = 0;
        }
        for (size_t i = 0; i < macLen; i++) {
            mac[i] = 128 + i;
            expectedChaddr[i] = 128 + i;
        }

        // type and transaction doesn't matter in this test
        pkt.reset(new Pkt4(DHCPOFFER, 1234));
        pkt->setHWAddr(255 - macLen * 10, // just weird htype
                       macLen,
                       mac);
        EXPECT_EQ(0, memcmp(expectedChaddr, &pkt->getHWAddr()->hwaddr_[0],
                            Pkt4::MAX_CHADDR_LEN));

        EXPECT_NO_THROW(
            pkt->pack();
        );

        // CHADDR starts at offset 28 in DHCP packet
        const uint8_t* ptr =
            static_cast<const uint8_t*>(pkt->getBuffer().getData()) + 28;

        EXPECT_EQ(0, memcmp(ptr, expectedChaddr, Pkt4::MAX_CHADDR_LEN));

        pkt.reset();
    }

    /// TODO: extend this test once options support is implemented. HW address
    /// longer than 16 bytes should be stored in client-identifier option
}

TEST_F(Pkt4Test, msgTypes) {

    struct msgType {
        uint8_t dhcp;
        uint8_t bootp;
    };

    msgType types[] = {
        {DHCPDISCOVER, BOOTREQUEST},
        {DHCPOFFER, BOOTREPLY},
        {DHCPREQUEST, BOOTREQUEST},
        {DHCPDECLINE, BOOTREQUEST},
        {DHCPACK, BOOTREPLY},
        {DHCPNAK, BOOTREPLY},
        {DHCPRELEASE, BOOTREQUEST},
        {DHCPINFORM, BOOTREQUEST},
        {DHCPLEASEQUERY, BOOTREQUEST},
        {DHCPLEASEUNASSIGNED, BOOTREPLY},
        {DHCPLEASEUNKNOWN, BOOTREPLY},
        {DHCPLEASEACTIVE, BOOTREPLY}
    };

    scoped_ptr<Pkt4> pkt;
    for (size_t i = 0; i < sizeof(types) / sizeof(msgType); i++) {
        pkt.reset(new Pkt4(types[i].dhcp, 0));
        EXPECT_EQ(types[i].dhcp, pkt->getType());
        EXPECT_EQ(types[i].bootp, pkt->getOp());
        pkt.reset();
    }

    EXPECT_THROW(
        pkt.reset(new Pkt4(100, 0)), // There's no message type 100
        OutOfRange
    );
}

// This test verifies handling of sname field
TEST_F(Pkt4Test, sname) {

    uint8_t sname[Pkt4::MAX_SNAME_LEN];

    scoped_ptr<Pkt4> pkt;
    // Let's test each sname length, from 0 till 64 (included)
    for (size_t snameLen = 0; snameLen <= Pkt4::MAX_SNAME_LEN; ++snameLen) {
        for (size_t i = 0; i < snameLen; ++i) {
            sname[i] = i + 1;
        }
        if (snameLen < Pkt4::MAX_SNAME_LEN) {
                for (size_t i = snameLen; i < Pkt4::MAX_SNAME_LEN; ++i) {
                        sname[i] = 0;
                }
        }

        // Type and transaction doesn't matter in this test
        pkt.reset(new Pkt4(DHCPOFFER, 1234));
        pkt->setSname(sname, snameLen);

        EXPECT_EQ(0, memcmp(sname, &pkt->getSname()[0], Pkt4::MAX_SNAME_LEN));

        EXPECT_NO_THROW(
            pkt->pack();
        );

        // SNAME starts at offset 44 in DHCP packet
        const uint8_t* ptr =
            static_cast<const uint8_t*>(pkt->getBuffer().getData()) + 44;
        EXPECT_EQ(0, memcmp(ptr, sname, Pkt4::MAX_SNAME_LEN));

        pkt.reset();
    }

    // Check that a null argument generates an exception.
    Pkt4 pkt4(DHCPOFFER, 1234);
    EXPECT_THROW(pkt4.setSname(NULL, Pkt4::MAX_SNAME_LEN), InvalidParameter);
    EXPECT_THROW(pkt4.setSname(NULL, 0), InvalidParameter);

    // Check that a too long argument generates an exception
    // (the actual content doesn't matter).
    uint8_t bigsname[Pkt4::MAX_SNAME_LEN + 1];
    EXPECT_THROW(pkt4.setSname(bigsname, Pkt4::MAX_SNAME_LEN + 1), OutOfRange);
}

TEST_F(Pkt4Test, file) {

    uint8_t file[Pkt4::MAX_FILE_LEN];

    scoped_ptr<Pkt4> pkt;
    // Let's test each file length, from 0 till 128 (included).
    for (size_t fileLen = 0; fileLen <= Pkt4::MAX_FILE_LEN; ++fileLen) {
        for (size_t i = 0; i < fileLen; ++i) {
            file[i] = i + 1;
        }
        if (fileLen < Pkt4::MAX_FILE_LEN) {
                for (size_t i = fileLen; i < Pkt4::MAX_FILE_LEN; ++i) {
                        file[i] = 0;
                }
        }

        // Type and transaction doesn't matter in this test.
        pkt.reset(new Pkt4(DHCPOFFER, 1234));
        pkt->setFile(file, fileLen);

        EXPECT_EQ(0, memcmp(file, &pkt->getFile()[0], Pkt4::MAX_FILE_LEN));

        EXPECT_NO_THROW(
            pkt->pack();
        );

        // FILE starts at offset 108 in DHCP packet.
        const uint8_t* ptr =
            static_cast<const uint8_t*>(pkt->getBuffer().getData()) + 108;
        EXPECT_EQ(0, memcmp(ptr, file, Pkt4::MAX_FILE_LEN));

        pkt.reset();
    }

    // Check that a null argument generates an exception.
    Pkt4 pkt4(DHCPOFFER, 1234);
    EXPECT_THROW(pkt4.setFile(NULL, Pkt4::MAX_FILE_LEN), InvalidParameter);
    EXPECT_THROW(pkt4.setFile(NULL, 0), InvalidParameter);

    // Check that a too long argument generates an exception
    // (the actual content doesn't matter).
    uint8_t bigfile[Pkt4::MAX_FILE_LEN + 1];
    EXPECT_THROW(pkt4.setFile(bigfile, Pkt4::MAX_FILE_LEN + 1), OutOfRange);
}

TEST_F(Pkt4Test, options) {
    scoped_ptr<Pkt4> pkt(new Pkt4(DHCPOFFER, 0));

    vector<uint8_t> payload[5];
    for (uint8_t i = 0; i < 5; i++) {
        payload[i].push_back(i * 10);
        payload[i].push_back(i * 10 + 1);
        payload[i].push_back(i * 10 + 2);
    }

    boost::shared_ptr<Option> opt1(new Option(Option::V4, 12, payload[0]));
    boost::shared_ptr<Option> opt3(new Option(Option::V4, 14, payload[1]));
    boost::shared_ptr<Option> opt2(new Option(Option::V4, 60, payload[2]));
    boost::shared_ptr<Option> opt5(new Option(Option::V4,128, payload[3]));
    boost::shared_ptr<Option> opt4(new Option(Option::V4,254, payload[4]));

    pkt->addOption(opt1);
    pkt->addOption(opt2);
    pkt->addOption(opt3);
    pkt->addOption(opt4);
    pkt->addOption(opt5);

    EXPECT_TRUE(pkt->getOption(12));
    EXPECT_TRUE(pkt->getOption(60));
    EXPECT_TRUE(pkt->getOption(14));
    EXPECT_TRUE(pkt->getOption(128));
    EXPECT_TRUE(pkt->getOption(254));
    EXPECT_FALSE(pkt->getOption(127)); //  no such option

    // Options are unique in DHCPv4. It should not be possible
    // to add more than one option of the same type.
    EXPECT_THROW(
        pkt->addOption(opt1),
        BadValue
    );

    EXPECT_NO_THROW(
        pkt->pack();
    );

    const OutputBuffer& buf = pkt->getBuffer();
    // Check that all options are stored, they should take sizeof(v4_opts),
    // DHCP magic cookie (4 bytes), and OPTION_END added (just one byte)
    ASSERT_EQ(static_cast<size_t>(Pkt4::DHCPV4_PKT_HDR_LEN) +
              sizeof(DHCP_OPTIONS_COOKIE) + sizeof(v4_opts) + 1,
              buf.getLength());

    // That that this extra data actually contain our options
    const uint8_t* ptr = static_cast<const uint8_t*>(buf.getData());

    // Rewind to end of fixed part.
    ptr += Pkt4::DHCPV4_PKT_HDR_LEN + sizeof(DHCP_OPTIONS_COOKIE);

    EXPECT_EQ(0, memcmp(ptr, v4_opts, sizeof(v4_opts)));
    EXPECT_EQ(DHO_END, static_cast<uint8_t>(*(ptr + sizeof(v4_opts))));

    // delOption() checks
    EXPECT_TRUE(pkt->getOption(12));  // Sanity check: option 12 is still there
    EXPECT_TRUE(pkt->delOption(12));  // We should be able to remove it
    EXPECT_FALSE(pkt->getOption(12)); // It should not be there anymore
    EXPECT_FALSE(pkt->delOption(12)); // And removal should fail

    EXPECT_NO_THROW(pkt.reset());
}

// This test verifies that it is possible to control whether a pointer
// to an option or a pointer to a copy of an option is returned by the
// packet object.
TEST_F(Pkt4Test, setCopyRetrievedOptions) {
    // Create option 1 with two sub options.
    OptionPtr option1(new Option(Option::V4, 1));
    OptionPtr sub1(new Option(Option::V4, 1));
    OptionPtr sub2(new Option(Option::V4, 2));

    option1->addOption(sub1);
    option1->addOption(sub2);

    // Create option 2 with two sub options.
    OptionPtr option2(new Option(Option::V4, 2));
    OptionPtr sub3(new Option(Option::V4, 1));
    OptionPtr sub4(new Option(Option::V4, 2));

    option2->addOption(sub3);
    option2->addOption(sub4);

    // Add both options to a packet.
    Pkt4Ptr pkt(new Pkt4(DHCPDISCOVER, 1234));
    pkt->addOption(option1);
    pkt->addOption(option2);

    // Retrieve options and make sure that the pointers to the original
    // option instances are returned.
    ASSERT_TRUE(option1 == pkt->getOption(1));
    ASSERT_TRUE(option2 == pkt->getOption(2));

    // Now force copying the options when they are retrieved.
    pkt->setCopyRetrievedOptions(true);
    EXPECT_TRUE(pkt->isCopyRetrievedOptions());

    // Option pointer returned must point to a new instance of option 2.
    OptionPtr option2_copy = pkt->getOption(2);
    EXPECT_FALSE(option2 == option2_copy);

    // Disable copying.
    pkt->setCopyRetrievedOptions(false);
    EXPECT_FALSE(pkt->isCopyRetrievedOptions());

    // Expect that the original pointer is returned. This guarantees that
    // option1 wasn't affected by copying option 2.
    OptionPtr option1_copy = pkt->getOption(1);
    EXPECT_TRUE(option1 == option1_copy);

    // Again, enable copying options.
    pkt->setCopyRetrievedOptions(true);

    // This time a pointer to new option instance should be returned.
    option1_copy = pkt->getOption(1);
    EXPECT_FALSE(option1 == option1_copy);
}

// This test verifies that the options are unpacked from the packet correctly.
TEST_F(Pkt4Test, unpackOptions) {

    vector<uint8_t> expectedFormat = generateTestPacket2();

    expectedFormat.push_back(0x63);
    expectedFormat.push_back(0x82);
    expectedFormat.push_back(0x53);
    expectedFormat.push_back(0x63);

    for (size_t i = 0; i < sizeof(v4_opts); i++) {
        expectedFormat.push_back(v4_opts[i]);
    }

    // now expectedFormat contains fixed format and 5 options

    boost::shared_ptr<Pkt4> pkt(new Pkt4(&expectedFormat[0],
                                expectedFormat.size()));

    EXPECT_NO_THROW(
        pkt->unpack()
    );

    verifyParsedOptions(pkt);
}

// Checks if the code is able to handle a malformed option
TEST_F(Pkt4Test, unpackMalformed) {

    vector<uint8_t> orig = generateTestPacket2();

    orig.push_back(0x63);
    orig.push_back(0x82);
    orig.push_back(0x53);
    orig.push_back(0x63);

    orig.push_back(53); // Message Type
    orig.push_back(1); // length=1
    orig.push_back(2); // type=2

    orig.push_back(12); // Hostname
    orig.push_back(3); // length=3
    orig.push_back(102); // data="foo"
    orig.push_back(111);
    orig.push_back(111);

    // That's our original content. It should be sane.
    Pkt4Ptr success(new Pkt4(&orig[0], orig.size()));
    EXPECT_NO_THROW(success->unpack());

    // With the exception of END and PAD an option must have a length byte
    vector<uint8_t> nolength = orig;
    nolength.resize(orig.size() - 4);
    Pkt4Ptr no_length_pkt(new Pkt4(&nolength[0], nolength.size()));
    EXPECT_NO_THROW(no_length_pkt->unpack());

    // The unpack() operation doesn't throw but there is no option 12
    EXPECT_FALSE(no_length_pkt->getOption(12));

    // Truncated data is not accepted too but doesn't throw
    vector<uint8_t> shorty = orig;
    shorty.resize(orig.size() - 1);
    Pkt4Ptr too_short_pkt(new Pkt4(&shorty[0], shorty.size()));
    EXPECT_NO_THROW(too_short_pkt->unpack());

    // The unpack() operation doesn't throw but there is no option 12
    EXPECT_FALSE(no_length_pkt->getOption(12));
}

// Checks if the code is able to handle a malformed vendor option
TEST_F(Pkt4Test, unpackVendorMalformed) {

    vector<uint8_t> orig = generateTestPacket2();

    orig.push_back(0x63);
    orig.push_back(0x82);
    orig.push_back(0x53);
    orig.push_back(0x63);

    orig.push_back(53); // Message Type
    orig.push_back(1); // length=1
    orig.push_back(2); // type=2

    orig.push_back(125); // vivso suboptions
    size_t full_len_index = orig.size();
    orig.push_back(15); // length=15
    orig.push_back(1); // vendor_id=0x1020304
    orig.push_back(2);
    orig.push_back(3);
    orig.push_back(4);
    size_t data_len_index = orig.size();
    orig.push_back(10); // data-len=10
    orig.push_back(128); // suboption type=128
    orig.push_back(3); // suboption length=3
    orig.push_back(102); // data="foo"
    orig.push_back(111);
    orig.push_back(111);
    orig.push_back(129); // suboption type=129
    orig.push_back(3); // suboption length=3
    orig.push_back(99); // data="bar"
    orig.push_back(98);
    orig.push_back(114);

    // That's our original content. It should be sane.
    Pkt4Ptr success(new Pkt4(&orig[0], orig.size()));
    EXPECT_NO_THROW(success->unpack());

    // Data-len must match
    vector<uint8_t> baddatalen = orig;
    baddatalen.resize(orig.size() - 5);
    baddatalen[full_len_index] = 10;
    Pkt4Ptr bad_data_len_pkt(new Pkt4(&baddatalen[0], baddatalen.size()));
    EXPECT_THROW(bad_data_len_pkt->unpack(), SkipRemainingOptionsError);

    // A suboption must have a length byte
    vector<uint8_t> nolength = orig;
    nolength.resize(orig.size() - 4);
    nolength[full_len_index] = 11;
    nolength[data_len_index] = 6;
    Pkt4Ptr no_length_pkt(new Pkt4(&nolength[0], nolength.size()));
    EXPECT_THROW(no_length_pkt->unpack(), SkipRemainingOptionsError);

    // Truncated data is not accepted either
    vector<uint8_t> shorty = orig;
    shorty.resize(orig.size() - 1);
    shorty[full_len_index] = 14;
    shorty[data_len_index] = 9;
    Pkt4Ptr too_short_pkt(new Pkt4(&shorty[0], shorty.size()));
    EXPECT_THROW(too_short_pkt->unpack(), SkipRemainingOptionsError);
}

// This test verifies methods that are used for manipulating meta fields
// i.e. fields that are not part of DHCPv4 (e.g. interface name).
TEST_F(Pkt4Test, metaFields) {

    scoped_ptr<Pkt4> pkt(new Pkt4(DHCPOFFER, 1234));
    pkt->setIface("loooopback");
    pkt->setIndex(42);
    pkt->setRemoteAddr(IOAddress("1.2.3.4"));
    pkt->setLocalAddr(IOAddress("4.3.2.1"));

    EXPECT_EQ("loooopback", pkt->getIface());
    EXPECT_EQ(42, pkt->getIndex());
    EXPECT_EQ("1.2.3.4", pkt->getRemoteAddr().toText());
    EXPECT_EQ("4.3.2.1", pkt->getLocalAddr().toText());
}

TEST_F(Pkt4Test, Timestamp) {
    scoped_ptr<Pkt4> pkt(new Pkt4(DHCPOFFER, 1234));

    // Just after construction timestamp is invalid
    ASSERT_TRUE(pkt->getTimestamp().is_not_a_date_time());

    // Update packet time.
    pkt->updateTimestamp();

    // Get updated packet time.
    boost::posix_time::ptime ts_packet = pkt->getTimestamp();

    // After timestamp is updated it should be date-time.
    ASSERT_FALSE(ts_packet.is_not_a_date_time());

    // Check current time.
    boost::posix_time::ptime ts_now =
        boost::posix_time::microsec_clock::universal_time();

    // Calculate period between packet time and now.
    boost::posix_time::time_period ts_period(ts_packet, ts_now);

    // Duration should be positive or zero.
    EXPECT_TRUE(ts_period.length().total_microseconds() >= 0);
}

TEST_F(Pkt4Test, hwaddr) {
    scoped_ptr<Pkt4> pkt(new Pkt4(DHCPOFFER, 1234));
    const uint8_t hw[] = { 2, 4, 6, 8, 10, 12 }; // MAC
    const uint8_t hw_type = 123; // hardware type

    HWAddrPtr hwaddr(new HWAddr(hw, sizeof(hw), hw_type));

    // setting NULL hardware address is not allowed
    EXPECT_THROW(pkt->setHWAddr(HWAddrPtr()), BadValue);

    pkt->setHWAddr(hwaddr);

    EXPECT_EQ(hw_type, pkt->getHtype());

    EXPECT_EQ(sizeof(hw), pkt->getHlen());

    EXPECT_TRUE(hwaddr == pkt->getHWAddr());
}

// This test verifies that the packet remote and local HW address can
// be set and returned.
TEST_F(Pkt4Test, hwaddrSrcRemote) {
    scoped_ptr<Pkt4> pkt(new Pkt4(DHCPOFFER, 1234));
    const uint8_t src_hw[] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t dst_hw[] = { 7, 8, 9, 10, 11, 12 };
    const uint8_t hw_type = 123;

    HWAddrPtr dst_hwaddr(new HWAddr(dst_hw, sizeof(src_hw), hw_type));
    HWAddrPtr src_hwaddr(new HWAddr(src_hw, sizeof(src_hw), hw_type));

    // Check that we can set the local address.
    EXPECT_NO_THROW(pkt->setLocalHWAddr(dst_hwaddr));
    EXPECT_TRUE(dst_hwaddr == pkt->getLocalHWAddr());

    // Check that we can set the remote address.
    EXPECT_NO_THROW(pkt->setRemoteHWAddr(src_hwaddr));
    EXPECT_TRUE(src_hwaddr == pkt->getRemoteHWAddr());

    // Can't set the NULL addres.
    EXPECT_THROW(pkt->setRemoteHWAddr(HWAddrPtr()), BadValue);
    EXPECT_THROW(pkt->setLocalHWAddr(HWAddrPtr()), BadValue);

    // Test alternative way to set local address.
    const uint8_t dst_hw2[] = { 19, 20, 21, 22, 23, 24 };
    std::vector<uint8_t> dst_hw_vec(dst_hw2, dst_hw2 + sizeof(dst_hw2));
    const uint8_t hw_type2 = 234;
    EXPECT_NO_THROW(pkt->setLocalHWAddr(hw_type2, sizeof(dst_hw2), dst_hw_vec));
    HWAddrPtr local_addr = pkt->getLocalHWAddr();
    ASSERT_TRUE(local_addr);
    EXPECT_EQ(hw_type2, local_addr->htype_);
    EXPECT_TRUE(std::equal(dst_hw_vec.begin(), dst_hw_vec.end(),
                           local_addr->hwaddr_.begin()));

    // Set remote address.
    const uint8_t src_hw2[] = { 25, 26, 27, 28, 29, 30 };
    std::vector<uint8_t> src_hw_vec(src_hw2, src_hw2 + sizeof(src_hw2));
    EXPECT_NO_THROW(pkt->setRemoteHWAddr(hw_type2, sizeof(src_hw2), src_hw_vec));
    HWAddrPtr remote_addr = pkt->getRemoteHWAddr();
    ASSERT_TRUE(remote_addr);
    EXPECT_EQ(hw_type2, remote_addr->htype_);
    EXPECT_TRUE(std::equal(src_hw_vec.begin(), src_hw_vec.end(),
                           remote_addr->hwaddr_.begin()));
}

// This test verifies that the check for a message being relayed is correct.
TEST_F(Pkt4Test, isRelayed) {
    Pkt4 pkt(DHCPDISCOVER, 1234);
    // By default, the hops and giaddr should be 0.
    ASSERT_TRUE(pkt.getGiaddr().isV4Zero());
    ASSERT_EQ(0, pkt.getHops());
    // For zero giaddr the packet is non-relayed.
    EXPECT_FALSE(pkt.isRelayed());
    // Set giaddr but leave hops = 0.
    pkt.setGiaddr(IOAddress("10.0.0.1"));
    EXPECT_TRUE(pkt.isRelayed());
    // After setting hops the message should still be relayed.
    pkt.setHops(10);
    EXPECT_TRUE(pkt.isRelayed());
    // Set giaddr to 0. The message is now not-relayed.
    pkt.setGiaddr(IOAddress(IOAddress::IPV4_ZERO_ADDRESS()));
    EXPECT_FALSE(pkt.isRelayed());
    // Setting the giaddr to 255.255.255.255 should not cause it to
    // be relayed message.
    pkt.setGiaddr(IOAddress(IOAddress::IPV4_BCAST_ADDRESS()));
    EXPECT_FALSE(pkt.isRelayed());
}

// Tests whether a packet can be assigned to a class and later
// checked if it belongs to a given class
TEST_F(Pkt4Test, clientClasses) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // Default values (do not belong to any class)
    EXPECT_FALSE(pkt.inClass(DOCSIS3_CLASS_EROUTER));
    EXPECT_FALSE(pkt.inClass(DOCSIS3_CLASS_MODEM));
    EXPECT_TRUE(pkt.getClasses().empty());

    // Add to the first class
    pkt.addClass(DOCSIS3_CLASS_EROUTER);
    EXPECT_TRUE(pkt.inClass(DOCSIS3_CLASS_EROUTER));
    EXPECT_FALSE(pkt.inClass(DOCSIS3_CLASS_MODEM));
    ASSERT_FALSE(pkt.getClasses().empty());

    // Add to a second class
    pkt.addClass(DOCSIS3_CLASS_MODEM);
    EXPECT_TRUE(pkt.inClass(DOCSIS3_CLASS_EROUTER));
    EXPECT_TRUE(pkt.inClass(DOCSIS3_CLASS_MODEM));

    // Check that it's ok to add to the same class repeatedly
    EXPECT_NO_THROW(pkt.addClass("foo"));
    EXPECT_NO_THROW(pkt.addClass("foo"));
    EXPECT_NO_THROW(pkt.addClass("foo"));

    // Check that the packet belongs to 'foo'
    EXPECT_TRUE(pkt.inClass("foo"));
}

// Tests whether a packet can be marked to evaluate later a class and
// after check if a given class is in the collection
TEST_F(Pkt4Test, deferredClientClasses) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // Default values (do not belong to any class)
    EXPECT_TRUE(pkt.getClasses(true).empty());

    // Add to the first class
    pkt.addClass(DOCSIS3_CLASS_EROUTER, true);
    EXPECT_EQ(1, pkt.getClasses(true).size());

    // Add to a second class
    pkt.addClass(DOCSIS3_CLASS_MODEM, true);
    EXPECT_EQ(2, pkt.getClasses(true).size());
    EXPECT_TRUE(pkt.getClasses(true).contains(DOCSIS3_CLASS_EROUTER));
    EXPECT_TRUE(pkt.getClasses(true).contains(DOCSIS3_CLASS_MODEM));
    EXPECT_FALSE(pkt.getClasses(true).contains("foo"));

    // Check that it's ok to add to the same class repeatedly
    EXPECT_NO_THROW(pkt.addClass("foo", true));
    EXPECT_NO_THROW(pkt.addClass("foo", true));
    EXPECT_NO_THROW(pkt.addClass("foo", true));

    // Check that the packet belongs to 'foo'
    EXPECT_TRUE(pkt.getClasses(true).contains("foo"));
}

// Tests whether a packet can be assigned to a subclass and later
// checked if it belongs to a given subclass
TEST_F(Pkt4Test, templateClasses) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // Default values (do not belong to any subclass)
    EXPECT_FALSE(pkt.inClass("SPAWN_template-interface-name_eth0"));
    EXPECT_FALSE(pkt.inClass("SPAWN_template-interface-id_interface-id0"));
    EXPECT_TRUE(pkt.getClasses().empty());

    // Add to the first subclass
    pkt.addSubClass("template-interface-name", "SPAWN_template-interface-name_eth0");
    EXPECT_TRUE(pkt.inClass("SPAWN_template-interface-name_eth0"));
    EXPECT_FALSE(pkt.inClass("SPAWN_template-interface-id_interface-id0"));
    ASSERT_FALSE(pkt.getClasses().empty());

    // Add to a second subclass
    pkt.addSubClass("template-interface-id", "SPAWN_template-interface-id_interface-id0");
    EXPECT_TRUE(pkt.inClass("SPAWN_template-interface-name_eth0"));
    EXPECT_TRUE(pkt.inClass("SPAWN_template-interface-id_interface-id0"));

    // Check that it's ok to add to the same subclass repeatedly
    EXPECT_NO_THROW(pkt.addSubClass("template-foo", "SPAWN_template-foo_bar"));
    EXPECT_NO_THROW(pkt.addSubClass("template-foo", "SPAWN_template-foo_bar"));
    EXPECT_NO_THROW(pkt.addSubClass("template-bar", "SPAWN_template-bar_bar"));

    // Check that the packet belongs to 'SPAWN_template-foo_bar'
    EXPECT_TRUE(pkt.inClass("SPAWN_template-foo_bar"));

    // Check that the packet belongs to 'SPAWN_template-bar_bar'
    EXPECT_TRUE(pkt.inClass("SPAWN_template-bar_bar"));
}

// Tests whether MAC can be obtained and that MAC sources are not
// confused.
TEST_F(Pkt4Test, getMAC) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // DHCPv4 packet by default doesn't have MAC address specified.
    EXPECT_FALSE(pkt.getMAC(HWAddr::HWADDR_SOURCE_ANY));
    EXPECT_FALSE(pkt.getMAC(HWAddr::HWADDR_SOURCE_RAW));

    // Let's invent a MAC
    const uint8_t hw[] = { 2, 4, 6, 8, 10, 12 }; // MAC
    const uint8_t hw_type = 123; // hardware type
    HWAddrPtr dummy_hwaddr(new HWAddr(hw, sizeof(hw), hw_type));

    // Now let's pretend that we obtained it from raw sockets
    pkt.setRemoteHWAddr(dummy_hwaddr);

    // Now we should be able to get something
    ASSERT_TRUE(pkt.getMAC(HWAddr::HWADDR_SOURCE_ANY));
    ASSERT_TRUE(pkt.getMAC(HWAddr::HWADDR_SOURCE_RAW));

    // Check that the returned MAC is indeed the expected one
    ASSERT_TRUE(*dummy_hwaddr == *pkt.getMAC(HWAddr::HWADDR_SOURCE_ANY));
    ASSERT_TRUE(*dummy_hwaddr == *pkt.getMAC(HWAddr::HWADDR_SOURCE_RAW));
}

// Tests that getLabel/makeLabel methods produces the expected strings based on
// packet content.
TEST_F(Pkt4Test, getLabel) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // Verify makeLabel() handles empty values
    EXPECT_EQ ("[no hwaddr info], cid=[no info], tid=0x0",
               Pkt4::makeLabel(HWAddrPtr(), ClientIdPtr(), 0));

    // Verify an "empty" packet label is as we expect
    EXPECT_EQ ("[hwtype=1 ], cid=[no info], tid=0x4d2",
               pkt.getLabel());

    // Set that packet hardware address, then verify getLabel
    const uint8_t hw[] = { 2, 4, 6, 8, 10, 12 }; // MAC
    const uint8_t hw_type = 123; // hardware type
    HWAddrPtr dummy_hwaddr(new HWAddr(hw, sizeof(hw), hw_type));
    pkt.setHWAddr(dummy_hwaddr);

    EXPECT_EQ ("[hwtype=123 02:04:06:08:0a:0c],"
               " cid=[no info], tid=0x4d2", pkt.getLabel());

    // Add a client id to the packet then verify getLabel
    OptionBuffer clnt_id(4);
    for (uint8_t i = 0; i < 4; i++) {
        clnt_id[i] = 100 + i;
    }

    OptionPtr opt(new Option(Option::V4, DHO_DHCP_CLIENT_IDENTIFIER,
                             clnt_id.begin(), clnt_id.begin() + 4));
    pkt.addOption(opt);

    EXPECT_EQ ("[hwtype=123 02:04:06:08:0a:0c],"
               " cid=[64:65:66:67], tid=0x4d2",
               pkt.getLabel());

}

// Test that empty client identifier option doesn't cause an exception from
// Pkt4::getLabel.
TEST_F(Pkt4Test, getLabelEmptyClientId) {
    Pkt4 pkt(DHCPOFFER, 1234);

    // Create empty client identifier option.
    OptionPtr empty_opt(new Option(Option::V4, DHO_DHCP_CLIENT_IDENTIFIER));
    pkt.addOption(empty_opt);

    EXPECT_EQ("[hwtype=1 ], cid=[no info], tid=0x4d2"
              " (malformed client-id)", pkt.getLabel());
}

// Tests that the variant of makeLabel which doesn't include transaction
// id produces expected output.
TEST_F(Pkt4Test, makeLabelWithoutTransactionId) {
    EXPECT_EQ("[no hwaddr info], cid=[no info]",
              Pkt4::makeLabel(HWAddrPtr(), ClientIdPtr()));

    // Test non-null hardware address.
    HWAddrPtr hwaddr(new HWAddr(HWAddr::fromText("01:02:03:04:05:06", 123)));
    EXPECT_EQ("[hwtype=123 01:02:03:04:05:06], cid=[no info]",
              Pkt4::makeLabel(hwaddr, ClientIdPtr()));

    // Test non-null client identifier and non-null hardware address.
    ClientIdPtr cid = ClientId::fromText("01:02:03:04");
    EXPECT_EQ("[hwtype=123 01:02:03:04:05:06], cid=[01:02:03:04]",
              Pkt4::makeLabel(hwaddr, cid));

    // Test non-nnull client identifier and null hardware address.
    EXPECT_EQ("[no hwaddr info], cid=[01:02:03:04]",
              Pkt4::makeLabel(HWAddrPtr(), cid));
}

// Tests that the correct DHCPv4 message name is returned for various
// message types.
TEST_F(Pkt4Test, getName) {
    // Check all possible packet types
    for (int itype = 0; itype < 256; ++itype) {
        uint8_t type = itype;

        switch (type) {
        case DHCPDISCOVER:
            EXPECT_STREQ("DHCPDISCOVER", Pkt4::getName(type));
            break;

        case DHCPOFFER:
            EXPECT_STREQ("DHCPOFFER", Pkt4::getName(type));
            break;

        case DHCPREQUEST:
            EXPECT_STREQ("DHCPREQUEST", Pkt4::getName(type));
            break;

        case DHCPDECLINE:
            EXPECT_STREQ("DHCPDECLINE", Pkt4::getName(type));
            break;

        case DHCPACK:
            EXPECT_STREQ("DHCPACK", Pkt4::getName(type));
            break;

        case DHCPNAK:
            EXPECT_STREQ("DHCPNAK", Pkt4::getName(type));
            break;

        case DHCPRELEASE:
            EXPECT_STREQ("DHCPRELEASE", Pkt4::getName(type));
            break;

        case DHCPINFORM:
            EXPECT_STREQ("DHCPINFORM", Pkt4::getName(type));
            break;

        case DHCPLEASEQUERY:
            EXPECT_STREQ("DHCPLEASEQUERY", Pkt4::getName(type));
            break;

        case DHCPLEASEUNASSIGNED:
            EXPECT_STREQ("DHCPLEASEUNASSIGNED", Pkt4::getName(type));
            break;

        case DHCPLEASEUNKNOWN:
            EXPECT_STREQ("DHCPLEASEUNKNOWN", Pkt4::getName(type));
            break;

        case DHCPLEASEACTIVE:
            EXPECT_STREQ("DHCPLEASEACTIVE", Pkt4::getName(type));
            break;

        case DHCPBULKLEASEQUERY:
            EXPECT_STREQ("DHCPBULKLEASEQUERY", Pkt4::getName(type));
            break;

        case DHCPLEASEQUERYDONE:
            EXPECT_STREQ("DHCPLEASEQUERYDONE", Pkt4::getName(type));
            break;

        case DHCPLEASEQUERYSTATUS:
            EXPECT_STREQ("DHCPLEASEQUERYSTATUS", Pkt4::getName(type));
            break;

        case DHCPTLS:
            EXPECT_STREQ("DHCPTLS", Pkt4::getName(type));
            break;

        default:
            EXPECT_STREQ("UNKNOWN", Pkt4::getName(type));
        }
    }
}

// This test checks that the packet data are correctly converted to the
// textual format.
TEST_F(Pkt4Test, toText) {
    Pkt4 pkt(DHCPDISCOVER, 2543);
    pkt.setLocalAddr(IOAddress("192.0.2.34"));
    pkt.setRemoteAddr(IOAddress("192.10.33.4"));

    pkt.addOption(OptionPtr(new Option4AddrLst(123, IOAddress("192.0.2.3"))));
    pkt.addOption(OptionPtr(new OptionUint32(Option::V4, 156, 123456)));
    pkt.addOption(OptionPtr(new OptionString(Option::V4, 87, "lorem ipsum")));

    EXPECT_EQ("local_address=192.0.2.34:67, remote_address=192.10.33.4:68, "
              "msg_type=DHCPDISCOVER (1), transid=0x9ef,\n"
              "options:\n"
              "  type=053, len=001: 1 (uint8)\n"
              "  type=087, len=011: \"lorem ipsum\" (string)\n"
              "  type=123, len=004: 192.0.2.3\n"
              "  type=156, len=004: 123456 (uint32)",
              pkt.toText());

    // Now remove all options, including Message Type and check if the
    // information about lack of any options is displayed properly.
    pkt.delOption(123);
    pkt.delOption(156);
    pkt.delOption(87);
    pkt.delOption(53);

    EXPECT_EQ("local_address=192.0.2.34:67, remote_address=192.10.33.4:68, "
              "msg_type=(missing), transid=0x9ef, "
              "message contains no options",
              pkt.toText());

}

// Sanity check. Verifies that the getName() and getType()
// don't throw.
TEST_F(Pkt4Test, getType) {

    Pkt4 pkt(DHCPDISCOVER, 2543);
    pkt.delOption(DHO_DHCP_MESSAGE_TYPE);

    ASSERT_NO_THROW(pkt.getType());
    ASSERT_NO_THROW(pkt.getName());

    // The method has to return something that is not NULL,
    // even if the packet doesn't have Message Type option.
    EXPECT_TRUE(pkt.getName());
}

// Verifies that when the VIVSO option 125 has length that is too
// short (i.e. less than sizeof(uint8_t), unpack throws a
// SkipRemainingOptionsError exception
TEST_F(Pkt4Test, truncatedVendorLength) {

    // Build a good discover packet
    Pkt4Ptr pkt = dhcp::test::PktCaptures::discoverWithValidVIVSO();

    // Unpacking should not throw
    ASSERT_NO_THROW(pkt->unpack());
    ASSERT_EQ(DHCPDISCOVER, pkt->getType());

    // VIVSO option should be there
    OptionPtr x = pkt->getOption(DHO_VIVSO_SUBOPTIONS);
    ASSERT_TRUE(x);
    ASSERT_EQ(DHO_VIVSO_SUBOPTIONS, x->getType());
    OptionVendorPtr vivso = boost::dynamic_pointer_cast<OptionVendor>(x);
    ASSERT_TRUE(vivso);
    EXPECT_EQ(133+2, vivso->len()); // data + opt code + len

    // Build a bad discover packet
    pkt = dhcp::test::PktCaptures::discoverWithTruncatedVIVSO();

    // Unpack should throw Skip exception
    ASSERT_THROW(pkt->unpack(), SkipRemainingOptionsError);
    ASSERT_EQ(DHCPDISCOVER, pkt->getType());

    // VIVSO option should not be there
    x = pkt->getOption(DHO_VIVSO_SUBOPTIONS);
    ASSERT_FALSE(x);
}

// Verifies that we handle text options that contain trailing
// and embedded NULLs correctly.  Per RFC 2132, Sec 2 we should
// be stripping trailing NULLs.  We've agreed to permit
// embedded NULLs (for now).
TEST_F(Pkt4Test, nullTerminatedOptions) {
    // Construct the onwire packet.
    vector<uint8_t> base_msg = generateTestPacket2();
    base_msg.push_back(0x63); // magic cookie
    base_msg.push_back(0x82);
    base_msg.push_back(0x53);
    base_msg.push_back(0x63);

    base_msg.push_back(0x35); // message-type
    base_msg.push_back(0x1);
    base_msg.push_back(0x1);

    int base_size = base_msg.size();

    // We'll create four text options, with various combinations of NULLs.
    vector<uint8_t> hostname = { DHO_HOST_NAME, 5, 't', 'w', 'o', 0, 0 };
    vector<uint8_t> merit_dump = { DHO_MERIT_DUMP, 4, 'o', 'n', 'e', 0 };
    vector<uint8_t> root_path = { DHO_ROOT_PATH, 4, 'n', 'o', 'n', 'e' };
    vector<uint8_t> domain_name = { DHO_DOMAIN_NAME, 6, 'e', 'm', 0, 'b', 'e', 'd' };

    // Add the options to the onwire packet.
    vector<uint8_t> test_msg = base_msg;
    test_msg.insert(test_msg.end(), hostname.begin(), hostname.end());
    test_msg.insert(test_msg.end(), root_path.begin(), root_path.end());
    test_msg.insert(test_msg.end(), merit_dump.begin(), merit_dump.end());
    test_msg.insert(test_msg.end(), domain_name.begin(), domain_name.end());
    test_msg.push_back(DHO_END);

    boost::shared_ptr<Pkt4> pkt(new Pkt4(&test_msg[0], test_msg.size()));

    // Unpack the onwire packet.
    EXPECT_NO_THROW(
        pkt->unpack()
    );

    EXPECT_EQ(DHCPDISCOVER, pkt->getType());

    OptionPtr opt;
    OptionStringPtr opstr;

    // Now let's verify that each text option is as expected.
    ASSERT_TRUE(opt = pkt->getOption(DHO_HOST_NAME));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(3, opstr->getValue().length());
    EXPECT_EQ("two", opstr->getValue());

    ASSERT_TRUE(opt = pkt->getOption(DHO_MERIT_DUMP));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(3, opstr->getValue().length());
    EXPECT_EQ("one", opstr->getValue());

    ASSERT_TRUE(opt = pkt->getOption(DHO_ROOT_PATH));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(4, opstr->getValue().length());
    EXPECT_EQ("none", opstr->getValue());

    ASSERT_TRUE(opt = pkt->getOption(DHO_DOMAIN_NAME));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(6, opstr->getValue().length());
    std::string embed{"em\0bed", 6};
    EXPECT_EQ(embed, opstr->getValue());


    // Next we pack the packet, to make sure trailing NULLs have
    // been eliminated, embedded NULLs are intact.
    EXPECT_NO_THROW(
        pkt->pack()
    );

    // Create a vector of our expected packed option data.
    vector<uint8_t> packed_opts =
        {
          DHO_HOST_NAME, 3, 't', 'w', 'o',
          DHO_MERIT_DUMP, 3, 'o', 'n', 'e',
          DHO_DOMAIN_NAME, 6, 'e', 'm', 0, 'b', 'e', 'd',
          DHO_ROOT_PATH, 4, 'n', 'o', 'n', 'e',
        };

    const uint8_t* packed = static_cast<const uint8_t*>(pkt->getBuffer().getData());
    int packed_len = pkt->getBuffer().getLength();

    // Packed message options should be 3 bytes smaller than original onwire data.
    int dif = packed_len - test_msg.size();
    ASSERT_EQ(-3, dif);

    // Make sure the packed content is as expected.
    EXPECT_EQ(0, memcmp(&packed[base_size], &packed_opts[0], packed_opts.size()));
}

// Checks that unpacking correctly handles SkipThisOptionError by
// omitting the offending option from the unpacked options.
TEST_F(Pkt4Test, testSkipThisOptionError) {
    vector<uint8_t> orig = generateTestPacket2();

    orig.push_back(0x63);
    orig.push_back(0x82);
    orig.push_back(0x53);
    orig.push_back(0x63);

    orig.push_back(53);   // Message Type
    orig.push_back(1);    // length=1
    orig.push_back(2);    // type=2

    orig.push_back(14);   // merit-dump
    orig.push_back(3);    // length=3
    orig.push_back(0x61); // data="abc"
    orig.push_back(0x62);
    orig.push_back(0x63);

    orig.push_back(12);   // Hostname
    orig.push_back(3);    // length=3
    orig.push_back(0);    // data= all nulls
    orig.push_back(0);
    orig.push_back(0);

    orig.push_back(17);   // root-path
    orig.push_back(3);    // length=3
    orig.push_back(0x64); // data="def"
    orig.push_back(0x65);
    orig.push_back(0x66);

    // Unpacking should not throw.
    Pkt4Ptr pkt(new Pkt4(&orig[0], orig.size()));
    ASSERT_NO_THROW_LOG(pkt->unpack());

    // We should have option 14 = "abc".
    OptionPtr opt;
    OptionStringPtr opstr;
    ASSERT_TRUE(opt = pkt->getOption(14));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(3, opstr->getValue().length());
    EXPECT_EQ("abc", opstr->getValue());

    // We should not have option 12.
    EXPECT_FALSE(opt = pkt->getOption(12));

    // We should have option 17 = "def".
    ASSERT_TRUE(opt = pkt->getOption(17));
    ASSERT_TRUE(opstr = boost::dynamic_pointer_cast<OptionString>(opt));
    EXPECT_EQ(3, opstr->getValue().length());
    EXPECT_EQ("def", opstr->getValue());
}

} // end of anonymous namespace

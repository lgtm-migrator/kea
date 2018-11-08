// Copyright (C) 2018 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>
#include <dhcp/packet_queue_mgr4.h>

#include <boost/scoped_ptr.hpp>

namespace isc {
namespace dhcp {

PacketQueueMgr4::PacketQueueMgr4() {
    // @todo Please forgive magic strings and constants.  The default values,
    // mechanisms will soon be reworked.

    // Register default queue factory
    registerPacketQueueFactory("kea-ring4", [](data::ConstElementPtr parameters)
                                          -> PacketQueue4Ptr {
            size_t capacity;
            try {
                capacity = data::SimpleParser::getInteger(parameters, "capacity");
            } catch (const std::exception& ex) {
                isc_throw(InvalidQueueParameter, "kea-ring4 factory:"
                          " 'capacity' parameter is missing/invalid: " << ex.what());
            }

            PacketQueue4Ptr queue(new PacketQueueRing4("kea-ring4", capacity));
            return (queue);
        });

    data::ElementPtr parameters = data::Element::createMap();
    parameters->set("queue-type", data::Element::create("kea-ring4"));
    parameters->set("capacity", data::Element::create(static_cast<long int>(500)));
    createPacketQueue(parameters);
}

boost::scoped_ptr<PacketQueueMgr4>&
PacketQueueMgr4::getPacketQueueMgr4Ptr() {
    static boost::scoped_ptr<PacketQueueMgr4> packet_mgr;
    return (packet_mgr);
}

void
PacketQueueMgr4::create() {
    getPacketQueueMgr4Ptr().reset(new PacketQueueMgr4());
}

void
PacketQueueMgr4::destroy() {
    getPacketQueueMgr4Ptr().reset();
}

PacketQueueMgr4&
PacketQueueMgr4::instance() {
    boost::scoped_ptr<PacketQueueMgr4>& packet_mgr = getPacketQueueMgr4Ptr();
    if (!packet_mgr) {
        create();
    }
    return (*packet_mgr);
}

} // end of isc::dhcp namespace
} // end of isc namespace

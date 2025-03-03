// Copyright (C) 2022 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef ALLOCATION_STATE_H
#define ALLOCATION_STATE_H

#include <dhcpsrv/lease.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <mutex>

namespace isc {
namespace dhcp {

/// @brief Base class for representing allocation state in pools and subnets.
///
/// Allocators are used in Kea to implement different lease selection
/// algorithms. They are stateful (i.e., they remember various information
/// about the previous allocations) to work efficiently. For example, an
/// iterative allocator must remember the last allocated address to pick
/// the consecutive address when the new allocation request is issued.
/// Allocation states differ between the allocators. State classes used
/// by different allocators derive from this class.
///
/// The allocation states can be associated with pools and/or subnets.
/// Both pool-specific and subnet-specific states derive from this class.
class AllocationState {
public:

    /// @brief Virtual destructor.
    virtual ~AllocationState() = default;
};

/// @brief Type of the pointer to the @c AllocationState.
typedef boost::shared_ptr<AllocationState> AllocationStatePtr;

/// @brief Common base class for subnet-specific allocation states.
///
/// All subnet-specific allocation states should derive from this class.
/// It provides a mutex for thread-safe access to the class members.
/// It maintains last allocation times for various lease types. These
/// times are used by the shared networks to find the "preferred" subnet
/// (i.e., a subnet from which the latest lease was assigned).
class SubnetAllocationState : public AllocationState {
public:

    /// @brief Constructor.
    ///
    /// Initializes the mutex.
    SubnetAllocationState();

    /// @brief Returns last allocation time for the specified lease type.
    ///
    /// @param type specifies a lease type for which the last allocation
    /// time should be returned.
    /// @return Last allocation time for the lease type or
    /// @c boost::posix_time::neg_infin when no leases have been allocated
    /// from this subnet yet.
    boost::posix_time::ptime
    getLastAllocatedTime(Lease::Type type) const;

protected:

    /// @brief Sets the last allocation time to current for a lease type.
    ///
    /// This function should be called by derived classes. It should be
    /// called in the thread-safe context.
    ///
    /// @param type specifies a lease type for which the last allocation
    /// time should be set to the current time.
    void setCurrentAllocatedTimeInternal(Lease::Type type);

    /// @brief Returns the last allocation time of a specified lease type.
    ///
    /// It should be called in a thread safe context.
    ///
    /// @param lease_type Lease type for which last allocation timestamp should
    /// be returned.
    ///
    /// @return time when a lease of a specified type has been allocated from
    /// this subnet. The negative infinity time is returned if a lease type is
    /// not recognized (which is unlikely).
    boost::posix_time::ptime
    getLastAllocatedTimeInternal(Lease::Type type) const;

    /// @brief Mutex used for thread-safe access to the state members.
    boost::scoped_ptr<std::mutex> mutex_;

    /// @brief Timestamp indicating when a lease of a specified type has been
    /// last allocated from the subnet.
    ///
    /// @note: This map is protected by the mutex.
    std::map<Lease::Type, boost::posix_time::ptime> last_allocated_time_;
};

typedef boost::shared_ptr<SubnetAllocationState> SubnetAllocationStatePtr;

} // end of namespace isc::dhcp
} // end of namespace isc

#endif // ALLOCATION_STATE_H

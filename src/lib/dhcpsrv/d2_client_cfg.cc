// Copyright (C) 2013-2017 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>

#include <dhcp_ddns/ncr_udp.h>
#include <dhcpsrv/d2_client_cfg.h>
#include <dhcpsrv/dhcpsrv_log.h>

#include <boost/algorithm/string/predicate.hpp>

#include <string>

using namespace std;
using namespace isc::asiolink;
using namespace isc::data;

namespace isc {
namespace dhcp {

const char* D2ClientConfig::DFT_SERVER_IP = "127.0.0.1";
const size_t D2ClientConfig::DFT_SERVER_PORT = 53001;
const char* D2ClientConfig::DFT_V4_SENDER_IP = "0.0.0.0";
const char* D2ClientConfig::DFT_V6_SENDER_IP = "::";
const size_t D2ClientConfig::DFT_SENDER_PORT = 0;
const size_t D2ClientConfig::DFT_MAX_QUEUE_SIZE = 1024;
const char* D2ClientConfig::DFT_NCR_PROTOCOL = "UDP";
const char* D2ClientConfig::DFT_NCR_FORMAT = "JSON";
const bool D2ClientConfig::DFT_ALWAYS_INCLUDE_FQDN = false;
const bool D2ClientConfig::DFT_OVERRIDE_NO_UPDATE = false;
const bool D2ClientConfig::DFT_OVERRIDE_CLIENT_UPDATE = false;
const char* D2ClientConfig::DFT_REPLACE_CLIENT_NAME_MODE = "NEVER";
const char* D2ClientConfig::DFT_GENERATED_PREFIX = "myhost";


D2ClientConfig::ReplaceClientNameMode
D2ClientConfig::stringToReplaceClientNameMode(const std::string& mode_str) {
    if (boost::iequals(mode_str, "never")) {
        return (D2ClientConfig::RCM_NEVER);
    }

    if (boost::iequals(mode_str, "always")) {
        return (D2ClientConfig::RCM_ALWAYS);
    }

    if (boost::iequals(mode_str, "when-present")) {
        return (D2ClientConfig::RCM_WHEN_PRESENT);
    }

    if (boost::iequals(mode_str, "when-not-present")) {
        return (D2ClientConfig::RCM_WHEN_NOT_PRESENT);
    }

    isc_throw(BadValue,
              "Invalid ReplaceClientNameMode: " << mode_str);
}

std::string
D2ClientConfig::replaceClientNameModeToString(const ReplaceClientNameMode& mode) {
    switch (mode) {
    case D2ClientConfig::RCM_NEVER:
        return ("never");
    case D2ClientConfig::RCM_ALWAYS:
        return ("always");
    case D2ClientConfig::RCM_WHEN_PRESENT:
        return ("when-present");
    case D2ClientConfig::RCM_WHEN_NOT_PRESENT:
        return ("when-not-present");
    default:
        break;
    }

    std::ostringstream stream;
    stream  << "unknown(" << mode << ")";
    return (stream.str());
}

D2ClientConfig::D2ClientConfig(const  bool enable_updates,
                               const isc::asiolink::IOAddress& server_ip,
                               const size_t server_port,
                               const isc::asiolink::IOAddress& sender_ip,
                               const size_t sender_port,
                               const size_t max_queue_size,
                               const dhcp_ddns::
                                     NameChangeProtocol& ncr_protocol,
                               const dhcp_ddns::
                                     NameChangeFormat& ncr_format,
                               const bool always_include_fqdn,
                               const bool override_no_update,
                               const bool override_client_update,
                               const ReplaceClientNameMode replace_client_name_mode,
                               const std::string& generated_prefix,
                               const std::string& qualifying_suffix)
    : enable_updates_(enable_updates),
      server_ip_(server_ip),
      server_port_(server_port),
      sender_ip_(sender_ip),
      sender_port_(sender_port),
      max_queue_size_(max_queue_size),
      ncr_protocol_(ncr_protocol),
      ncr_format_(ncr_format),
      always_include_fqdn_(always_include_fqdn),
      override_no_update_(override_no_update),
      override_client_update_(override_client_update),
      replace_client_name_mode_(replace_client_name_mode),
      generated_prefix_(generated_prefix),
      qualifying_suffix_(qualifying_suffix) {
    validateContents();
}

D2ClientConfig::D2ClientConfig()
    : enable_updates_(false),
      server_ip_(isc::asiolink::IOAddress(DFT_SERVER_IP)),
      server_port_(DFT_SERVER_PORT),
      sender_ip_(isc::asiolink::IOAddress(DFT_V4_SENDER_IP)),
      sender_port_(DFT_SENDER_PORT),
      max_queue_size_(DFT_MAX_QUEUE_SIZE),
      ncr_protocol_(dhcp_ddns::stringToNcrProtocol(DFT_NCR_PROTOCOL)),
      ncr_format_(dhcp_ddns::stringToNcrFormat(DFT_NCR_FORMAT)),
      always_include_fqdn_(DFT_ALWAYS_INCLUDE_FQDN),
      override_no_update_(DFT_OVERRIDE_NO_UPDATE),
      override_client_update_(DFT_OVERRIDE_CLIENT_UPDATE),
      replace_client_name_mode_(stringToReplaceClientNameMode(DFT_REPLACE_CLIENT_NAME_MODE)),
      generated_prefix_(DFT_GENERATED_PREFIX),
      qualifying_suffix_("") {
    validateContents();
}

D2ClientConfig::~D2ClientConfig(){};

void
D2ClientConfig::enableUpdates(bool enable) {
    enable_updates_ = enable;
}

void
D2ClientConfig::validateContents() {
    if (ncr_format_ != dhcp_ddns::FMT_JSON) {
        isc_throw(D2ClientError, "D2ClientConfig: NCR Format: "
                    << dhcp_ddns::ncrFormatToString(ncr_format_)
                    << " is not yet supported");
    }

    if (ncr_protocol_ != dhcp_ddns::NCR_UDP) {
        isc_throw(D2ClientError, "D2ClientConfig: NCR Protocol: "
                  << dhcp_ddns::ncrProtocolToString(ncr_protocol_)
                  << " is not yet supported");
    }

    if (sender_ip_.getFamily() != server_ip_.getFamily()) {
        isc_throw(D2ClientError, "D2ClientConfig: address family mismatch: "
                  << "server-ip: " << server_ip_.toText()
                  << " is: " << (server_ip_.isV4() ? "IPv4" : "IPv6")
                  << " while sender-ip: "  << sender_ip_.toText()
                  << " is: " << (sender_ip_.isV4() ? "IPv4" : "IPv6"));
    }

    if (server_ip_ == sender_ip_ && server_port_ == sender_port_) {
        isc_throw(D2ClientError, "D2ClientConfig: server and sender cannot"
                  " share the exact same IP address/port: "
                  << server_ip_.toText() << "/" << server_port_);
    }

    /// @todo perhaps more validation we should do yet?
    /// Are there any invalid combinations of options we need to test against?
}

bool
D2ClientConfig::operator == (const D2ClientConfig& other) const {
    return ((enable_updates_ == other.enable_updates_) &&
            (server_ip_ == other.server_ip_) &&
            (server_port_ == other.server_port_) &&
            (sender_ip_ == other.sender_ip_) &&
            (sender_port_ == other.sender_port_) &&
            (max_queue_size_ == other.max_queue_size_) &&
            (ncr_protocol_ == other.ncr_protocol_) &&
            (ncr_format_ == other.ncr_format_) &&
            (always_include_fqdn_ == other.always_include_fqdn_) &&
            (override_no_update_ == other.override_no_update_) &&
            (override_client_update_ == other.override_client_update_) &&
            (replace_client_name_mode_ == other.replace_client_name_mode_) &&
            (generated_prefix_ == other.generated_prefix_) &&
            (qualifying_suffix_ == other.qualifying_suffix_));
}

bool
D2ClientConfig::operator != (const D2ClientConfig& other) const {
    return (!(*this == other));
}

std::string
D2ClientConfig::toText() const {
    std::ostringstream stream;

    stream << "enable_updates: " << (enable_updates_ ? "yes" : "no");
    if (enable_updates_) {
        stream << ", server_ip: " << server_ip_.toText()
               << ", server_port: " << server_port_
               << ", sender_ip: " << sender_ip_.toText()
               << ", sender_port: " << sender_port_
               << ", max_queue_size: " << max_queue_size_
               << ", ncr_protocol: " << ncrProtocolToString(ncr_protocol_)
               << ", ncr_format: " << ncrFormatToString(ncr_format_)
               << ", always_include_fqdn: " << (always_include_fqdn_ ?
                                                "yes" : "no")
               << ", override_no_update: " << (override_no_update_ ?
                                               "yes" : "no")
               << ", override_client_update: " << (override_client_update_ ?
                                                   "yes" : "no")
               << ", replace_client_name: "
               << replaceClientNameModeToString(replace_client_name_mode_)
               << ", generated_prefix: [" << generated_prefix_ << "]"
               << ", qualifying_suffix: [" << qualifying_suffix_ << "]";
    }

    return (stream.str());
}

ElementPtr
D2ClientConfig::toElement() const {
    ElementPtr result = Element::createMap();
    // Set enable-updates
    result->set("enable-updates", Element::create(enable_updates_));
    // Set qualifying-suffix
    result->set("qualifying-suffix", Element::create(qualifying_suffix_));
    // Set server-ip
    result->set("server-ip", Element::create(server_ip_.toText()));
    // Set server-port
    result->set("server-port",
                Element::create(static_cast<long long>(server_port_)));
    // Set sender-ip
    result->set("sender-ip", Element::create(sender_ip_.toText()));
    // Set sender-port
    result->set("sender-port",
                Element::create(static_cast<long long>(sender_port_)));
    // Set max-queue-size
    result->set("max-queue-size",
                Element::create(static_cast<long long>(max_queue_size_)));
    // Set ncr-protocol
    result->set("ncr-protocol",
                Element::create(dhcp_ddns::
                                ncrProtocolToString(ncr_protocol_)));
    // Set ncr-format
    result->set("ncr-format",
                Element::create(dhcp_ddns::ncrFormatToString(ncr_format_)));
    // Set always-include-fqdn
    result->set("always-include-fqdn", Element::create(always_include_fqdn_));
    // Set override-no-update
    result->set("override-no-update", Element::create(override_no_update_));
    // Set override-client-update
    result->set("override-client-update",
                Element::create(override_client_update_));
    // Set replace-client-name
    result->set("replace-client-name",
                Element::create(replaceClientNameModeToString
                                (replace_client_name_mode_)));
    // Set generated-prefix
    result->set("generated-prefix", Element::create(generated_prefix_));
    return (result);
}

std::ostream&
operator<<(std::ostream& os, const D2ClientConfig& config) {
    os << config.toText();
    return (os);
}

};  // namespace dhcp

};  // namespace isc

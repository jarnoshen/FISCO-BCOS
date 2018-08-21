/*
	This file is part of cpp-ethereum.
	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Session.h
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#pragma once

#include <mutex>
#include <array>
#include <deque>
#include <set>
#include <memory>
#include <utility>

#include <libdevcore/Common.h>
#include <libdevcore/RLP.h>
#include <libdevcore/Guards.h>
#include "RLPXFrameCoder.h"
#include "RLPXSocket.h"
#include "RLPXSocketSSL.h"
#include "Common.h"
#include "RLPXFrameWriter.h"
#include "RLPXFrameReader.h"
#include "SessionCAData.h"
#include "libstatistics/InterfaceStatistics.h"


namespace dev
{

namespace p2p
{

class Peer;
class ReputationManager;


class SessionFace
{
public:
	virtual ~SessionFace() {}

	virtual void start() = 0;
	virtual void disconnect(DisconnectReason _reason) = 0;

	virtual void ping() = 0;
	virtual void announcement(h256 const& _allPeerHash) = 0;

	virtual bool isConnected() const = 0;

	virtual NodeID id() const = 0;

	virtual void sealAndSend(RLPStream& _s, uint16_t _protocolID) = 0;

	virtual int rating() const = 0;
	virtual void addRating(int _r) = 0;

	virtual void addNote(std::string const& _k, std::string const& _v) = 0;

	virtual PeerSessionInfo info() const = 0;
	virtual std::chrono::steady_clock::time_point connectionTime() = 0;

	virtual void registerCapability(CapDesc const& _desc, std::shared_ptr<Capability> _p) = 0;
	virtual void registerFraming(uint16_t _id) = 0;

	virtual std::map<CapDesc, std::shared_ptr<Capability>> const&  capabilities() const = 0;

	virtual std::shared_ptr<Peer> peer() const = 0;

	virtual std::chrono::steady_clock::time_point lastReceived() const = 0;

	virtual ReputationManager& repMan() = 0;
	virtual CABaseData* getCABaseData() = 0;
	virtual void saveCABaseData(CABaseData*) = 0;
};

/**
 * @brief The Session class
 * @todo Document fully.
 */
class Session: public SessionFace, public std::enable_shared_from_this<Session>
{
public:
	static bool isFramingAllowedForVersion(unsigned _version) { return _version > 4; }

	Session(HostApi* _server, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocketApi> const& _s, std::shared_ptr<Peer> const& _n, PeerSessionInfo _info);
	
	virtual ~Session();

	void start() override;
	void disconnect(DisconnectReason _reason) override;

	void ping() override;
	void announcement(h256 const& _allPeerHash) override;


	bool isConnected() const override { return m_socket->isConnected(); }

	NodeID id() const override;

	void sealAndSend(RLPStream& _s, uint16_t _protocolID) override;

	int rating() const override;
	void addRating(int _r) override;

	void addNote(std::string const& _k, std::string const& _v) override { Guard l(x_info); m_info.notes[_k] = _v; }

	PeerSessionInfo info() const override { Guard l(x_info); return m_info; }
	std::chrono::steady_clock::time_point connectionTime() override { return m_connect; }

	void registerCapability(CapDesc const& _desc, std::shared_ptr<Capability> _p) override;
	void registerFraming(uint16_t _id) override;

	std::map<CapDesc, std::shared_ptr<Capability>> const& capabilities() const override { return m_capabilities; }

	std::shared_ptr<Peer> peer() const override { return m_peer; }

	std::chrono::steady_clock::time_point lastReceived() const override { return m_lastReceived; }

	ReputationManager& repMan() override;

	CABaseData* getCABaseData();
	void saveCABaseData(CABaseData*);
	bool setStatistics(dev::InterfaceStatistics *stats);

private:
	static RLPStream& prep(RLPStream& _s, PacketType _t, unsigned _args = 0);

	void send(bytes&& _msg, uint16_t _protocolID);

	/// Drop the connection for the reason @a _r.
	void drop(DisconnectReason _r);

	/// Perform a read on the socket.
	void doRead();
	void doReadFrames();

	/// Check error code after reading and drop peer if error code.
	bool checkRead(std::size_t _expected, boost::system::error_code _ec, std::size_t _length);

	/// Perform a single round of the write operation. This could end up calling itself asynchronously.
	void onWrite(boost::system::error_code ec, std::size_t length);
	void write();
	void writeFrames();

	/// Deliver RLPX packet to Session or Capability for interpretation.
	bool readPacket(uint16_t _capId, PacketType _t, RLP const& _r);

	/// Interpret an incoming Session packet.
	bool interpret(PacketType _t, RLP const& _r);

	/// @returns true iff the _msg forms a valid message for sending or receiving on the network.
	static bool checkPacket(bytesConstRef _msg);

	HostApi* m_server;							///< The host that owns us. Never null.

	std::unique_ptr<RLPXFrameCoder> m_io;	///< Transport over which packets are sent.
	std::shared_ptr<RLPXSocketApi> m_socket;		///< Socket of peer's connection.
	Mutex x_framing;						///< Mutex for the write queue.
	std::deque<bytes> m_writeQueue;			///< The write queue.
	std::deque<u256> m_writeTimeQueue; ///< to stat queue time
	std::vector<byte> m_data;			    ///< Buffer for ingress packet data.
	bytes m_incoming;						///< Read buffer for ingress bytes.

	std::shared_ptr<Peer> m_peer;			///< The Peer object.
	bool m_dropped = false;					///< If true, we've already divested ourselves of this peer. We're just waiting for the reads & writes to fail before the shared_ptr goes OOS and the destructor kicks in.

	mutable Mutex x_info;
	PeerSessionInfo m_info;						///< Dynamic information about this peer.

	std::chrono::steady_clock::time_point m_connect;		///< Time point of connection.
	std::chrono::steady_clock::time_point m_ping;			///< Time point of last ping.
	std::chrono::steady_clock::time_point m_lastReceived;	///< Time point of last message.

	std::map<CapDesc, std::shared_ptr<Capability>> m_capabilities;	///< The peer's capability set.
	std::shared_ptr<dev::InterfaceStatistics> m_statistics = nullptr;

	// framing-related stuff (protected by x_writeQueue mutex)
	struct Framing
	{
		Framing() = delete;
		Framing(uint16_t _protocolID): writer(_protocolID), reader(_protocolID) {}
		RLPXFrameWriter writer;
		RLPXFrameReader reader;
	};

	std::map<uint16_t, std::shared_ptr<Framing> > m_framing;
	std::deque<bytes> m_encFrames;

	bool isFramingEnabled() const { return isFramingAllowedForVersion(m_info.protocolVersion); }
	unsigned maxFrameSize() const { return 1024; }
	std::shared_ptr<Framing> getFraming(uint16_t _protocolID);
	void multiplexAll();

	CABaseData *m_CABaseData = nullptr;
	unsigned m_start_t;

	boost::asio::io_service::strand* m_strand;
};

template <class PeerCap>
std::shared_ptr<PeerCap> capabilityFromSession(SessionFace const& _session, u256 const& _version = PeerCap::version())
{
	try
	{
		return std::static_pointer_cast<PeerCap>(_session.capabilities().at(std::make_pair(PeerCap::name(), _version)));
	}
	catch (...)
	{
		return nullptr;
	}
}

}
}

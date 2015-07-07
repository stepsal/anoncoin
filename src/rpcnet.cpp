// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2013-2014 The Anoncoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"

#include "main.h"
#include "net.h"
#include "netbase.h"
#include "protocol.h"
#include "sync.h"
#include "util.h"
#include "alert.h"
#include "base58.h"

#include <boost/foreach.hpp>
#include "json/json_spirit_value.h"

using namespace json_spirit;
using namespace std;

Value getconnectioncount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getconnectioncount\n"
            "\nReturns the number of connections to other nodes.\n"
            "\nbResult:\n"
            "n          (numeric) The connection count\n"
            "\nExamples:\n"
            + HelpExampleCli("getconnectioncount", "")
            + HelpExampleRpc("getconnectioncount", "")
        );

    LOCK(cs_vNodes);
    return (int)vNodes.size();
}

Value ping(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "ping\n"
            "\nRequests that a ping be sent to all other nodes, to measure ping time.\n"
            "Results provided in getpeerinfo, pingtime and pingwait fields are decimal seconds.\n"
            "Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.\n"
            "\nExamples:\n"
            + HelpExampleCli("ping", "")
            + HelpExampleRpc("ping", "")
        );

    // Request that each node send a ping during next message processing pass
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pNode, vNodes) {
        pNode->fPingQueued = true;
    }

    return Value::null;
}

static void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    BOOST_FOREACH(CNode* pnode, vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats);
        vstats.push_back(stats);
    }
}

Value getpeerinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getpeerinfo\n"
            "\nReturns data about each connected network node as a json array of objects.\n"
            "\nbResult:\n"
            "[\n"
            "  {\n"
            "    \"addr\":\"host:port\",      (string) The ip address and port of the peer\n"
            "    \"addrlocal\":\"ip:port\",   (string) local address\n"
            "    \"services\":\"xxxxxxxxxxxxxxxx\",   (string) The services offered\n"
            "    \"lastsend\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last send\n"
            "    \"lastrecv\": ttt,           (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last receive\n"
            "    \"bytessent\": n,            (numeric) The total bytes sent\n"
            "    \"bytesrecv\": n,            (numeric) The total bytes received\n"
            "    \"conntime\": ttt,           (numeric) The connection time in seconds since epoch (Jan 1 1970 GMT)\n"
            "    \"pingtime\": n,             (numeric) ping time\n"
            "    \"pingwait\": n,             (numeric) ping wait\n"
            "    \"version\": v,              (numeric) The peer version, such as 70008\n"
            "    \"subver\": \"/s:n.n.n.n/\", (string) The subversion string\n"
            "    \"inbound\": true|false,     (boolean) Inbound (true) or Outbound (false)\n"
            "    \"startingheight\": n,       (numeric) The starting height (block) of the peer\n"
            "    \"banscore\": n,             (numeric) The ban score\n"
            "    \"syncnode\": true|false     (boolean) if sync node\n"
            "  }\n"
            "  ,...\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getpeerinfo", "")
            + HelpExampleRpc("getpeerinfo", "")
        );

    vector<CNodeStats> vstats;
    CopyNodeStats(vstats);

    Array ret;

    BOOST_FOREACH(const CNodeStats& stats, vstats) {
        Object obj;
        CNodeStateStats statestats;
        bool fStateStats = GetNodeStateStats(stats.nodeid, statestats);
        obj.push_back(Pair("addr", stats.addrName));
        if (!(stats.addrLocal.empty()))
            obj.push_back(Pair("addrlocal", stats.addrLocal));
        obj.push_back(Pair("services", strprintf("%016x", stats.nServices)));
        obj.push_back(Pair("lastsend", stats.nLastSend));
        obj.push_back(Pair("lastrecv", stats.nLastRecv));
        obj.push_back(Pair("bytessent", stats.nSendBytes));
        obj.push_back(Pair("bytesrecv", stats.nRecvBytes));
        obj.push_back(Pair("conntime", stats.nTimeConnected));
        obj.push_back(Pair("pingtime", stats.dPingTime));
        if (stats.dPingWait > 0.0)
            obj.push_back(Pair("pingwait", stats.dPingWait));
        obj.push_back(Pair("version", stats.nVersion));
        // Use the sanitized form of subver here, to avoid tricksy remote peers from
        // corrupting or modifiying the JSON output by putting special characters in
        // their ver message.
        obj.push_back(Pair("subver", stats.cleanSubVer));
        obj.push_back(Pair("inbound", stats.fInbound));
        obj.push_back(Pair("startingheight", stats.nStartingHeight));
        if (fStateStats) {
            obj.push_back(Pair("banscore", statestats.nMisbehavior));
        }
        obj.push_back(Pair("syncnode", stats.fSyncNode));

        ret.push_back(obj);
    }

    return ret;
}

Value addnode(const Array& params, bool fHelp)
{
    string strCommand;
    if (params.size() == 2)
        strCommand = params[1].get_str();
    if (fHelp || params.size() != 2 ||
        (strCommand != "onetry" && strCommand != "add" && strCommand != "remove"))
        throw runtime_error(
            "addnode \"node\" \"add|remove|onetry\"\n"
            "\nAttempts add or remove a node from the addnode list.\n"
            "Or try a connection to a node once.\n"
            "\nArguments:\n"
            "1. \"node\"     (string, required) The node (see getpeerinfo for nodes)\n"
            "2. \"command\"  (string, required) 'add' to add a node to the list, 'remove' to remove a node from the list, 'onetry' to try a connection to the node once\n"
            "\nExamples:\n"
            + HelpExampleCli("addnode", "\"192.168.0.6:9377\" \"onetry\"")
            + HelpExampleRpc("addnode", "\"192.168.0.6:9377\", \"onetry\"")
        );

    string strNode = params[0].get_str();

    if (strCommand == "onetry")
    {
        CAddress addr;
        ConnectNode(addr, strNode.c_str());
        return Value::null;
    }

    LOCK(cs_vAddedNodes);
    vector<string>::iterator it = vAddedNodes.begin();
    for(; it != vAddedNodes.end(); it++)
        if (strNode == *it)
            break;

    if (strCommand == "add")
    {
        if (it != vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_ALREADY_ADDED, "Error: Node already added");
        vAddedNodes.push_back(strNode);
    }
    else if(strCommand == "remove")
    {
        if (it == vAddedNodes.end())
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
        vAddedNodes.erase(it);
    }

    return Value::null;
}

Value getaddednodeinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getaddednodeinfo dns ( \"node\" )\n"
            "\nReturns information about the given added node, or all added nodes\n"
            "(note that onetry addnodes are not listed here)\n"
            "If dns is false, only a list of added nodes will be provided,\n"
            "otherwise connected information will also be available.\n"
            "\nArguments:\n"
            "1. dns        (boolean, required) If false, only a list of added nodes will be provided, otherwise connected information will also be available.\n"
            "2. \"node\"   (string, optional) If provided, return information about this specific node, otherwise all nodes are returned.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"addednode\" : \"192.168.0.201\",   (string) The node ip address\n"
            "    \"connected\" : true|false,          (boolean) If connected\n"
            "    \"addresses\" : [\n"
            "       {\n"
            "         \"address\" : \"192.168.0.201:9377\",  (string) The anoncoin server host and port\n"
            "         \"connected\" : \"outbound\"           (string) connection, inbound or outbound\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddednodeinfo", "true")
            + HelpExampleCli("getaddednodeinfo", "true \"192.168.0.201\"")
            + HelpExampleRpc("getaddednodeinfo", "true, \"192.168.0.201\"")
        );

    bool fDns = params[0].get_bool();

    list<string> laddedNodes(0);
    if (params.size() == 1)
    {
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(string& strAddNode, vAddedNodes)
            laddedNodes.push_back(strAddNode);
    }
    else
    {
        string strNode = params[1].get_str();
        LOCK(cs_vAddedNodes);
        BOOST_FOREACH(string& strAddNode, vAddedNodes)
            if (strAddNode == strNode)
            {
                laddedNodes.push_back(strAddNode);
                break;
            }
        if (laddedNodes.size() == 0)
            throw JSONRPCError(RPC_CLIENT_NODE_NOT_ADDED, "Error: Node has not been added.");
    }

    Array ret;
    if (!fDns)
    {
        BOOST_FOREACH(string& strAddNode, laddedNodes)
        {
            Object obj;
            obj.push_back(Pair("addednode", strAddNode));
            ret.push_back(obj);
        }
        return ret;
    }

    list<pair<string, vector<CService> > > laddedAddreses(0);
    BOOST_FOREACH(string& strAddNode, laddedNodes)
    {
        vector<CService> vservNode(0);
        if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
            laddedAddreses.push_back(make_pair(strAddNode, vservNode));
        else
        {
            Object obj;
            obj.push_back(Pair("addednode", strAddNode));
            obj.push_back(Pair("connected", false));
            Array addresses;
            obj.push_back(Pair("addresses", addresses));
        }
    }

    LOCK(cs_vNodes);
    for (list<pair<string, vector<CService> > >::iterator it = laddedAddreses.begin(); it != laddedAddreses.end(); it++)
    {
        Object obj;
        obj.push_back(Pair("addednode", it->first));

        Array addresses;
        bool fConnected = false;
        BOOST_FOREACH(CService& addrNode, it->second)
        {
            bool fFound = false;
            Object node;
            node.push_back(Pair("address", addrNode.ToString()));
            BOOST_FOREACH(CNode* pnode, vNodes)
                if (pnode->addr == addrNode)
                {
                    fFound = true;
                    fConnected = true;
                    node.push_back(Pair("connected", pnode->fInbound ? "inbound" : "outbound"));
                    break;
                }
            if (!fFound)
                node.push_back(Pair("connected", "false"));
            addresses.push_back(node);
        }
        obj.push_back(Pair("connected", fConnected));
        obj.push_back(Pair("addresses", addresses));
        ret.push_back(obj);
    }

    return ret;
}

Value getnettotals(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
            "getnettotals\n"
            "\nReturns information about network traffic, including bytes in, bytes out,\n"
            "and current time.\n"
            "\nResult:\n"
            "{\n"
            "  \"totalbytesrecv\": n,   (numeric) Total bytes received\n"
            "  \"totalbytessent\": n,   (numeric) Total bytes sent\n"
            "  \"timemillis\": t        (numeric) Total cpu time\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getnettotals", "")
            + HelpExampleRpc("getnettotals", "")
       );

    Object obj;
    obj.push_back(Pair("totalbytesrecv", CNode::GetTotalBytesRecv()));
    obj.push_back(Pair("totalbytessent", CNode::GetTotalBytesSent()));
    obj.push_back(Pair("timemillis", GetTimeMillis()));
    return obj;
}

static Array GetNetworksInfo()
{
    Array networks;
    for(int n=0; n<NET_MAX; ++n)
    {
        enum Network network = static_cast<enum Network>(n);
        if(network == NET_UNROUTABLE)
            continue;
        proxyType proxy;
        Object obj;
        GetProxy(network, proxy);
        obj.push_back(Pair("name", GetNetworkName(network)));
        obj.push_back(Pair("limited", IsLimited(network)));
        obj.push_back(Pair("reachable", IsReachable(network)));
        obj.push_back(Pair("proxy", proxy.IsValid() ? proxy.ToStringIPPort() : string()));
        networks.push_back(obj);
    }
    return networks;
}

static const string SingleAlertSubVersionsString( const std::set<std::string>& setVersions )
{
    std::string strSetSubVer;
    BOOST_FOREACH(std::string str, setVersions) {
        if(strSetSubVer.size())                 // Must be more than one
            strSetSubVer += " or ";
        strSetSubVer += str;
    }
    return strSetSubVer;
}

Value getnetworkinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getnetworkinfo\n"
            "Returns an object containing various state info regarding P2P networking.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"subver\": \"/s:n.n.n.n/\",  (string)  this clients subversion string\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"localservices\": xxxxxxxx,  (numeric) in Hex, the local service bits\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in ixc/kb\n"
            "  \"networkconnections\": [,    (array)  the state of each possible network connection type\n"
            "    \"name\": \"xxx\",          (string) network name\n"
            "    \"limited\" : true|false,   (boolean) if service is limited\n"
            "    \"reachable\" : true|false, (boolean) if service is reachable\n"
            "    \"proxy\": \"host:port\",   (string, optional) the proxy used by the server\n"
            "  ]\n"
            "  \"localaddresses\": [,        (array) list of local addresses\n"
            "    \"address\": \"xxxx\",      (string) network address\n"
            "    \"port\": xxx,              (numeric) network port\n"
            "    \"score\": xxx              (numeric) relative score\n"
            "  ]\n"
            "  \"alerts\": [,                (array) list of alerts on network\n"
            "    \"alertid\": \"xxx\",       (numeric) the ID number for this alert\n"
            "    \"priority\": xxx,          (numeric) the alert priority\n"
            "    \"minver\": xxx             (numeric) the minimum protocol version this effects\n"
            "    \"maxver\": xxx             (numeric) the maximum protocol version this effects\n"
            "    \"subvers\": \"/s:n.n.n.n/\",(string) null=all or the client version(s) this effects\n"
            "    \"relayuntil\": xxx         (numeric) relay this alert to other nodes until this time\n"
            "    \"expiration\": xxx         (numeric) when this alert will expire\n"
            "    \"statusbar\": \"xxxx\",    (string) status bar & tooltip string displayed\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getnetworkinfo", "")
            + HelpExampleRpc("getnetworkinfo", "")
        );

    Object obj;
    obj.push_back(Pair("version",        (int)CLIENT_VERSION));
    obj.push_back(Pair("subversion",     FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>())));
    obj.push_back(Pair("protocolversion",(int)PROTOCOL_VERSION));
    obj.push_back(Pair("localservices",  strprintf("%08x", nLocalServices)));
    obj.push_back(Pair("timeoffset",     GetTimeOffset()));
    obj.push_back(Pair("connections",    (int)vNodes.size()));
    obj.push_back(Pair("relayfee",       ValueFromAmount(CTransaction::nMinRelayTxFee)));
    obj.push_back(Pair("networkconnections",GetNetworksInfo()));
    Array localAddresses;
    {
        LOCK(cs_mapLocalHost);
        BOOST_FOREACH(const PAIRTYPE(CNetAddr, LocalServiceInfo) &item, mapLocalHost)
        {
            Object rec;
            rec.push_back(Pair("address", item.first.ToString()));
            rec.push_back(Pair("port", item.second.nPort));
            rec.push_back(Pair("score", item.second.nScore));
            localAddresses.push_back(rec);
        }
    }
    obj.push_back(Pair("localaddresses", localAddresses));

    // Add in the list of alerts currently on the network
    Array localAlerts;
    if( !mapAlerts.empty() ) {
          // Parse all the alerts, and prepare a JSON response list
          LOCK(cs_mapAlerts);
          for( map<uint256, CAlert>::iterator mi = mapAlerts.begin(); mi != mapAlerts.end(); mi++ ) {
               const CAlert& alert = (*mi).second;
               Object rec;
               rec.push_back( Pair("AlertID", alert.nID) );
               rec.push_back( Pair("Priority", alert.nPriority) );
               rec.push_back( Pair("MinVer", alert.nMinVer) );
               rec.push_back( Pair("MaxVer", alert.nMaxVer) );
               rec.push_back( Pair("SubVer", SingleAlertSubVersionsString(alert.setSubVer)) );
               rec.push_back( Pair("RelayUntil", alert.nRelayUntil) );
               rec.push_back( Pair("Expiration", alert.nExpiration) );
               rec.push_back( Pair("StatusBar", alert.strStatusBar) );
               localAlerts.push_back(rec);
          }
    }
    obj.push_back(Pair("alerts", localAlerts));

    return obj;
}

// Only build this code in preleases or test builds
#if CLIENT_VERSION_IS_RELEASE != true
//
// This allows our developers to notify all nodes of any issues on the Anoncoin network
//
Value sendalert(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 7)
        throw runtime_error(
            "sendalert <message> <privatekey> <minver> <maxver> <subvers> <priority> <id> [relaydays] [expiredays] [cancelupto]\n"
            "<message> is the alert text message\n"
            "<privatekey> is hex string of alert private key\n"
            "<minver> is the minimum applicable protocol version\n"
            "<maxver> is the maximum applicable protocol version\n"
            "<subvers> if not null, a specific set of client version(s) (see BIP14 for specs)\n"
            "<priority> is integer priority number\n"
            "<id> is the alert id you have assigned\n"
            "[relaydays]  relay this alert for this many days\n"
            "[expiredays] expire this alert in this many days\n"
            "[cancelupto] cancels all alert id's up to this number\n"
            "Returns JSON result if successful.");

    CAlert alert;
    CKey key;

    alert.strStatusBar = params[0].get_str();
    alert.nMinVer = params[2].get_int();
    alert.nMaxVer = params[3].get_int();

    // We need to parse out the subversion strings as per BIP14 and create a
    // set of matchable versions this alert is targeted for.  A null string
    // indicates all version, 1st we do a small bit of user input verification.
    // So this should work if the string is null, if there is one version string,
    // or more, separated by '/' as per the specification.
    string strSetSubVers = params[4].get_str();
    if( strSetSubVers.size() ) {
        // The 1st and last chars need to be a '/' or it was not entered correctly
        if( strSetSubVers[0] == '/'  && strSetSubVers[ strSetSubVers.size() - 1 ] == '/' ) {
            std::string::size_type iPos = 1, iLast;     // We have at least one likely good string
            while( ( iLast = strSetSubVers.find('/',iPos--) ) != std::string::npos ) {
                alert.setSubVer.insert( strSetSubVers.substr(iPos,++iLast-iPos));
                iPos = iLast;
            }
        } else
            throw runtime_error( "Invalid client subversion(s) string, see BIP14 for specifications\n");
    }
    // else We're done, the set of subver strings defaults to null on creation.
    alert.nPriority = params[5].get_int();
    alert.nID = params[6].get_int();
    if (params.size() > 9)
        alert.nCancel = params[9].get_int();
    alert.nVersion = PROTOCOL_VERSION;

    // Relay and don't expire this alert for one year, or the number of days given
    const int64_t i64AlertNow = GetAdjustedTime();

    alert.nRelayUntil = ( params.size() > 7 ) ? params[7].get_int() : 365;
    alert.nRelayUntil *= 24*60*60;       // One day in seconds
    alert.nRelayUntil += i64AlertNow;

    alert.nExpiration = ( params.size() > 8 ) ? params[8].get_int() : 365;
    alert.nExpiration *= 24*60*60;       // One day in seconds
    alert.nExpiration += i64AlertNow;

    CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
    sMsg << (CUnsignedAlert)alert;
    alert.vchMsg = vector<unsigned char>(sMsg.begin(), sMsg.end());

    // From https://bitcointalk.org/index.php?topic=50330.40:
    //
    // CKey::GetPrivKey and CKey::SetPrivKey are accessor methods for the 279-byte DES private key.
    // CKey::GetSecret and CKey::SetSecret are accessor methods for the 32-byte private key.
    //
    // Those of you who are interested in the OpenSSL calls needed, it's all spelled out in key.h
    //
    //  If the SendAlert user is giving us the 32 byte secret code & we already know the public key, so...
    //  Setup an object with the correct value for the private key, otherwise we'll assume they are giving us the
    //  private key as the string value, and use that to create the key object.
    //  Either of the above must be given, before this code can sign and then process the alert.
    //
    // There are various code fragments below that are commented out for release builds. Helpful however, for debugging
    // changes to the code and/or keys being used.  Otherwise not needed
    //
    //  Move the SendAlert 2nd parmeter chars into a vector, whichever it is, can assume it's being given to us as hex pairs
    vector<unsigned char> vchPrivKey = ParseHex(params[1].get_str());

    if( vchPrivKey.size() == 32 ) {         // Then we're given only a 32-byte private key multipler
        key.Set( vchPrivKey.begin(), vchPrivKey.end(), false );
        CPrivKey nPK = key.GetPrivKey();    // This calls openssl & sets the key structure up correctly.
        // Print out the private key here, from being set by SecretBytes...
        std::string strKey;
        for( size_t i=0; i<nPK.size(); i++ )
            strKey += strprintf( "%02x", nPK[i] );
        LogPrintf("SendAlert pass is the SecretBytes, Private Key Value is:\n%s\n", strKey);
    }
    else if( !key.SetPrivKey( CPrivKey(vchPrivKey.begin(), vchPrivKey.end()), false ) )
        throw runtime_error( "Unable to verify alert Private key, check private key?\n");
    else
        LogPrintf("SendAlert pass is the PrivateKey.\n");

    // Sign the message & set the alert vchSig string...
    if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
        throw runtime_error( "Unable to sign alert, check private key?\n");
    else if(!alert.ProcessAlert())
        throw runtime_error( "Failed to process alert.\n");
    //
    // If you need to, Print out the CAlert structure, into the log file here:
    alert.print();
    //
    // After we've called alert.ProcessAlert(), the public key will have been used to verify the
    // signature of the (now) signed alert message, or a runtime_error would have been returned.

    // Relay alert to the other nodes
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH(CNode* pnode, vNodes)
            alert.RelayTo(pnode);
    }
    // At this point, the Anoncoin network will be flooded with the alert message before very much time has passed.

    Object res;
    res.push_back( Pair("AlertID", alert.nID) );
    res.push_back( Pair("Priority", alert.nPriority) );
    res.push_back( Pair("Version", alert.nVersion) );
    res.push_back( Pair("MinVer", alert.nMinVer) );
    res.push_back( Pair("MaxVer", alert.nMaxVer) );
    res.push_back( Pair("SubVer", SingleAlertSubVersionsString(alert.setSubVer)) );
    res.push_back( Pair("RelayUntil", alert.nRelayUntil) );
    res.push_back( Pair("Expiration", alert.nExpiration) );
    res.push_back( Pair("StatusBar", alert.strStatusBar) );
    if (alert.nCancel > 0)
        res.push_back( Pair("Cancel", alert.nCancel) );
    return res;
}

Value makekeypair(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "makekeypair [prefix]\n"
            "Make a public/private key pair.\n"
            "[prefix] is optional preferred prefix for the public key.\n");

    string strPrefix = "";
    if (params.size() > 0)
        strPrefix = params[0].get_str();

    CKey key;
    CPubKey pubkey;
    int nCount = 0;
    do
    {
        key.MakeNewKey(false);
        pubkey = key.GetPubKey();
        nCount++;
    } while (nCount < 10000 && strPrefix != HexStr(pubkey.begin(), pubkey.end()).substr(0, strPrefix.size()));

    if (strPrefix != HexStr(pubkey.begin(), pubkey.end()).substr(0, strPrefix.size()))
        return Value::null;

    Object result;
    result.push_back(Pair("PublicKey", HexStr(pubkey.begin(), pubkey.end())));
    result.push_back(Pair("PrivateKey", CAnoncoinSecret(key).ToString()));
    return result;
}

#endif  // !CLIENT_VERSION_IS_RELEASE

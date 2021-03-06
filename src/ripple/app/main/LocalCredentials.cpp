//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/app/data/DatabaseCon.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/main/LocalCredentials.h>
#include <ripple/app/peers/UniqueNodeList.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/core/Config.h>
#include <boost/optional.hpp>
#include <iostream>

namespace ripple {

void LocalCredentials::start ()
{
    // We need our node identity before we begin networking.
    // - Allows others to identify if they have connected multiple times.
    // - Determines our CAS routing and responsibilities.
    // - This is not our validation identity.
    if (!nodeIdentityLoad ())
    {
        nodeIdentityCreate ();

        if (!nodeIdentityLoad ())
            throw std::runtime_error ("unable to retrieve new node identity.");
    }

    if (!getConfig ().QUIET)
        std::cerr << "NodeIdentity: " << mNodePublicKey.humanNodePublic () << std::endl;

    getApp().getUNL ().start ();
}

// Retrieve network identity.
bool LocalCredentials::nodeIdentityLoad ()
{
    auto db = getApp().getWalletDB ().checkoutDb ();
    bool        bSuccess    = false;

    boost::optional<std::string> pubKO, priKO;
    soci::statement st = (db->prepare <<
                          "SELECT PublicKey, PrivateKey "
                          "FROM NodeIdentity;",
                          soci::into(pubKO),
                          soci::into(priKO));
    st.execute ();
    while (st.fetch ())
    {
        mNodePublicKey.setNodePublic (pubKO.value_or(""));
        mNodePrivateKey.setNodePrivate (priKO.value_or(""));

        bSuccess    = true;
    }

    if (getConfig ().NODE_PUB.isValid () && getConfig ().NODE_PRIV.isValid ())
    {
        mNodePublicKey = getConfig ().NODE_PUB;
        mNodePrivateKey = getConfig ().NODE_PRIV;
    }

    return bSuccess;
}

// Create and store a network identity.
bool LocalCredentials::nodeIdentityCreate ()
{
    if (!getConfig ().QUIET)
        std::cerr << "NodeIdentity: Creating." << std::endl;

    //
    // Generate the public and private key
    //
    RippleAddress   naSeed          = RippleAddress::createSeedRandom ();
    RippleAddress   naNodePublic    = RippleAddress::createNodePublic (naSeed);
    RippleAddress   naNodePrivate   = RippleAddress::createNodePrivate (naSeed);

    // Make new key.
    std::string strDh512 (getRawDHParams (512));

    std::string strDh1024 = strDh512;

    //
    // Store the node information
    //
    auto db = getApp().getWalletDB ().checkoutDb ();

    *db << str (boost::format ("INSERT INTO NodeIdentity (PublicKey,PrivateKey,Dh512,Dh1024) VALUES ('%s','%s',%s,%s);")
                         % naNodePublic.humanNodePublic ()
                         % naNodePrivate.humanNodePrivate ()
                         % sqlEscape (strDh512)
                         % sqlEscape (strDh1024));

    if (!getConfig ().QUIET)
        std::cerr << "NodeIdentity: Created." << std::endl;

    return true;
}

bool LocalCredentials::dataDelete (std::string const& strKey)
{
    auto db = getApp().getRpcDB ().checkoutDb ();

    *db << (str (boost::format ("DELETE FROM RPCData WHERE Key=%s;")
                 % sqlEscape (strKey)));
    return true;
}

bool LocalCredentials::dataFetch (std::string const& strKey, std::string& strValue)
{
    auto db = getApp().getRpcDB ().checkoutDb ();

    bool        bSuccess    = false;

    soci::blob value (*db);
    soci::indicator vi;
    *db << str (boost::format ("SELECT Value FROM RPCData WHERE Key=%s;")
                % sqlEscape (strKey)),
            soci::into(value, vi);

    if (soci::i_ok == vi)
    {
        convert (value, strValue);
        bSuccess    = true;
    }

    return bSuccess;
}

bool LocalCredentials::dataStore (std::string const& strKey, std::string const& strValue)
{
    auto db = getApp().getRpcDB ().checkoutDb ();

    *db << (str (boost::format ("REPLACE INTO RPCData (Key, Value) VALUES (%s,%s);")
                 % sqlEscape (strKey)
                 % sqlEscape (strValue)));
    return true;
}

} // ripple

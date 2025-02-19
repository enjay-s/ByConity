
#include <Access/BeikeSaslClient.h>

#include <sstream>

#include <boost/algorithm/string.hpp>

using boost::algorithm::is_any_of;
using boost::algorithm::join;
using boost::algorithm::split;
using boost::algorithm::to_lower;
using namespace std;

namespace sasl
{

TSasl::TSasl(const string & service_, const string & serverFQDN_, sasl_callback_t * callbacks_)
    : service(service_), serverFQDN(serverFQDN_), authComplete(false), callbacks(callbacks_), conn(nullptr)
{
}

uint8_t * TSasl::unwrap(const uint8_t * incoming, const int /*offset*/, const uint32_t len, uint32_t * outLen)
{
    uint32_t outputlen;
    uint8_t * output;
    int result;

    result = sasl_decode(
        conn, reinterpret_cast<const char *>(incoming), len, const_cast<const char **>(reinterpret_cast<char **>(&output)), &outputlen);
    if (result != SASL_OK)
    {
        throw SaslException(sasl_errdetail(conn));
    }
    *outLen = outputlen;
    return output;
}

uint8_t * TSasl::wrap(const uint8_t * outgoing, const int offset, const uint32_t len, uint32_t * outLen)
{
    uint32_t outputlen;
    uint8_t * output;
    int result;

    result = sasl_encode(
        conn,
        reinterpret_cast<const char *>(outgoing) + offset,
        len,
        const_cast<const char **>(reinterpret_cast<char **>(&output)),
        &outputlen);
    if (result != SASL_OK)
    {
        throw SaslException(sasl_errdetail(conn));
    }
    *outLen = outputlen;
    return output;
}

string TSasl::getUsername()
{
    const char * username;
    int result = sasl_getprop(conn, SASL_USERNAME, reinterpret_cast<const void **>(&username));
    if (result != SASL_OK)
    {
        stringstream ss;
        ss << "Error getting SASL_USERNAME property: " << sasl_errstring(result, nullptr, nullptr);
        throw SaslException(ss.str().c_str());
    }
    // Copy the username and return it to the caller. There is no cleanup/delete call for
    // calls to sasl_getprops, the sasl layer handles the cleanup internally.
    string ret(username);
    return ret;
}

TSaslClient::TSaslClient(
    const string & mechanisms,
    const string & /*authenticationId*/,
    const string & service_,
    const string & serverFQDN_,
    const map<string, string> & props,
    sasl_callback_t * callbacks_)
    : TSasl(service_, serverFQDN_, callbacks_), clientStarted(false), mechList(mechanisms)
{
    if (!props.empty())
    {
        throw SaslServerImplException("Properties not yet supported");
    }

    sasl_client_init(callbacks);

    int result = sasl_client_init(callbacks);
    if (result != SASL_OK)
        throw SaslServerImplException("sasl client init error");
}

void TSaslClient::setupSaslContext()
{
    //DCHECK(conn == nullptr);
    int result = sasl_client_new(service.c_str(), serverFQDN.c_str(), nullptr, nullptr, callbacks, 0, &conn);
    if (result != SASL_OK)
    {
        if (conn)
        {
            throw SaslServerImplException(sasl_errdetail(conn));
        }
        else
        {
            throw SaslServerImplException(sasl_errstring(result, nullptr, nullptr));
        }
    }
}

void TSaslClient::resetSaslContext()
{
    clientStarted = false;
    authComplete = false;
    disposeSaslContext();
}

uint8_t * TSaslClient::evaluateChallengeOrResponse(const uint8_t * challenge, const uint32_t len, uint32_t * resLen)
{
    sasl_interact_t * client_interact = nullptr;
    uint8_t * out = nullptr;
    uint32_t outlen = 0;
    uint32_t result;
    char * mechUsing;

    if (!clientStarted)
    {
        result = sasl_client_start(
            conn,
            mechList.c_str(),
            &client_interact,
            const_cast<const char **>(reinterpret_cast<char **>(&out)),
            &outlen,
            const_cast<const char **>(&mechUsing));
        clientStarted = true;
        if (result == SASL_OK || result == SASL_CONTINUE)
        {
            chosenMech = mechUsing;
        }
    }
    else
    {
        if (len > 0)
        {
            result = sasl_client_step(
                conn,
                reinterpret_cast<const char *>(challenge),
                len,
                &client_interact,
                const_cast<const char **>(reinterpret_cast<char **>(&out)),
                &outlen);
        }
        else
        {
            result = SASL_CONTINUE;
        }
    }

    if (result == SASL_OK)
    {
        authComplete = true;
    }
    else if (result != SASL_CONTINUE)
    {
        throw SaslClientImplException(sasl_errdetail(conn));
    }
    *resLen = outlen;
    return out;
}

string TSaslClient::getMechanismName()
{
    return chosenMech;
}

string TSaslClient::getNegotiatedProperty(const string & /*propName*/)
{
    return nullptr;
}

bool TSaslClient::hasInitialResponse()
{
    // TODO: Need to return a value based on the mechanism.
    return true;
}

/*
TSaslServer::TSaslServer(
    const string & service, const string & serverFQDN, const string & userRealm, unsigned flags, sasl_callback_t * callbacks)
    : TSasl(service, serverFQDN, callbacks), userRealm(userRealm), flags(flags), serverStarted(false)
{
}

void TSaslServer::setupSaslContext()
{
    int result = sasl_server_new(
        service.c_str(),
        serverFQDN.size() == 0 ? NULL : serverFQDN.c_str(),
        userRealm.size() == 0 ? NULL : userRealm.c_str(),
        NULL,
        NULL,
        callbacks,
        flags,
        &conn);
    if (result != SASL_OK)
    {
        if (conn)
        {
            throw SaslServerImplException(sasl_errdetail(conn));
        }
        else
        {
            throw SaslServerImplException(sasl_errstring(result, NULL, NULL));
        }
    }
}

void TSaslServer::resetSaslContext()
{
    serverStarted = false;
    authComplete = false;
    disposeSaslContext();
}

uint8_t * TSaslServer::evaluateChallengeOrResponse(const uint8_t * response, const uint32_t len, uint32_t * resLen)
{
    uint8_t * out = NULL;
    uint32_t outlen = 0;
    uint32_t result;

    if (!serverStarted)
    {
        result = sasl_server_start(
            conn, reinterpret_cast<const char *>(response), NULL, 0, const_cast<const char **>(reinterpret_cast<char **>(&out)), &outlen);
    }
    else
    {
        result = sasl_server_step(
            conn, reinterpret_cast<const char *>(response), len, const_cast<const char **>(reinterpret_cast<char **>(&out)), &outlen);
    }

    if (result == SASL_OK)
    {
        authComplete = true;
    }
    else if (result != SASL_CONTINUE)
    {
        throw SaslServerImplException(sasl_errdetail(conn));
    }
    serverStarted = true;

    *resLen = outlen;
    return out;
}*/
};


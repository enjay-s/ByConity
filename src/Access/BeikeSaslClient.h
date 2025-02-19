#pragma once


#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include <sasl/sasl.h>

#include <thrift/transport/TTransportException.h>


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wheader-hygiene"
using namespace apache::thrift::transport;

#pragma clang diagnostic pop

namespace sasl
{

class SaslException : public TTransportException
{
public:
    SaslException(const char * msg) : TTransportException(msg) { }
};

class TSasl
{
public:
    virtual void setupSaslContext() = 0;

    virtual void resetSaslContext() = 0;

    virtual ~TSasl() { disposeSaslContext(); }


    static void SaslDone() { sasl_done(); }

    virtual uint8_t * evaluateChallengeOrResponse(const uint8_t * challenge, uint32_t len, uint32_t * resLen) = 0;

    bool isComplete() const { return authComplete; }


    uint8_t * unwrap(const uint8_t * incoming, const int offset, const uint32_t len, uint32_t * outLen);


    uint8_t * wrap(const uint8_t * outgoing, int offset, const uint32_t len, uint32_t * outLen);

    virtual std::string getMechanismName() { return nullptr; }

    virtual bool hasInitialResponse() { return false; }

    std::string getUsername();

protected:
    std::string service;

    std::string serverFQDN;

    bool authComplete;

    sasl_callback_t * callbacks;

    sasl_conn_t * conn;

    TSasl(const std::string & service, const std::string & serverFQDN, sasl_callback_t * callbacks);

    void disposeSaslContext()
    {
        if (conn != nullptr)
        {
            sasl_dispose(&conn);
            conn = nullptr;
        }
    }
};

class SaslClientImplException : public SaslException
{
public:
    SaslClientImplException(const char * errMsg) : SaslException(errMsg) { }
};

class TSaslClient : public sasl::TSasl
{
public:
    TSaslClient(
        const std::string & mechanisms,
        const std::string & authorizationId,
        const std::string & protocol,
        const std::string & serverName,
        const std::map<std::string, std::string> & props,
        sasl_callback_t * callbacks);

    static void SaslInit(const sasl_callback_t * callbacks_)
    {
        int result = sasl_client_init(callbacks_);
        if (result != SASL_OK)
            throw SaslClientImplException(sasl_errstring(result, nullptr, nullptr));
    }

    uint8_t * evaluateChallengeOrResponse(const uint8_t * challenge, const uint32_t len, uint32_t * outLen) override;

    virtual std::string getMechanismName() override;

    std::string getNegotiatedProperty(const std::string & propName);

    virtual void setupSaslContext() override;

    virtual void resetSaslContext() override;

    virtual bool hasInitialResponse() override;

private:
    bool clientStarted;

    std::string chosenMech;

    std::string mechList;
};

class SaslServerImplException : public SaslException
{
public:
    SaslServerImplException(const char * errMsg) : SaslException(errMsg) { }
};

/*
class TSaslServer : public sasl::TSasl
{
public:
    TSaslServer(
        const std::string & service,
        const std::string & serverFQDN,
        const std::string & userRealm,
        unsigned flags,
        sasl_callback_t * callbacks);


    static void SaslInit(const sasl_callback_t * callbacks, const char * appname)
    {
        int result = sasl_server_init(callbacks, appname);
        if (result != SASL_OK)
        {
            throw SaslServerImplException(sasl_errstring(result, nullptr, nullptr));
        }
    }

    virtual void setupSaslContext() override;

    virtual void resetSaslContext() override;

    virtual uint8_t * evaluateChallengeOrResponse(const uint8_t * challenge, const uint32_t len, uint32_t * resLen) override;

private:
    std::string userRealm;

    unsigned flags;

    bool serverStarted;
};*/

}


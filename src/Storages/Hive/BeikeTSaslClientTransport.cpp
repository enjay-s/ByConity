//
// Created by root on 25-2-19.
//

#include "BeikeTSaslClientTransport.h"

#include "Access/BeikeTSaslTransport.h"

using namespace sasl;

namespace apache
{
namespace thrift
{
    namespace transport
    {

        BeikeTSaslClientTransport::BeikeTSaslClientTransport(std::shared_ptr<sasl::TSasl> saslClient, std::shared_ptr<TTransport> transport)
            : TSaslTransport(saslClient, transport)
        {
        }

        void BeikeTSaslClientTransport::setupSaslNegotiationState()
        {
            if (!sasl_)
            {
                throw SaslClientImplException("Invalid state: setupSaslNegotiationState() failed. TSaslClient not created");
            }
            sasl_->setupSaslContext();
        }

        void BeikeTSaslClientTransport::resetSaslNegotiationState()
        {
            if (!sasl_)
            {
                throw SaslClientImplException("Invalid state: resetSaslNegotiationState() failed. TSaslClient not created");
            }
            sasl_->resetSaslContext();
        }

        void BeikeTSaslClientTransport::handleSaslStartMessage()
        {
            uint32_t resLength = 0;
            uint8_t dummy = 0;
            uint8_t * initialResponse = &dummy;

            if (sasl_->hasInitialResponse())
            {
                initialResponse = sasl_->evaluateChallengeOrResponse(nullptr, 0, &resLength);
            }

            sendSaslMessage(
                TSASL_START,
                const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(sasl_->getMechanismName().c_str())),
                sasl_->getMechanismName().length(),
                false);
            sendSaslMessage(TSASL_OK, initialResponse, resLength);

            transport_->flush();
        }
    }
}
}


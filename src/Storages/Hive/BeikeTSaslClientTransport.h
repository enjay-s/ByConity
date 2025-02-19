
#pragma once

#include <string>

#include <boost/shared_ptr.hpp>
#include <thrift/transport/TTransport.h>
#include <thrift/transport/TVirtualTransport.h>
#include "Access/BeikeSaslClient.h"
#include "Access/BeikeTSaslTransport.h"

namespace apache
{
namespace thrift
{
    namespace transport
    {
        class BeikeTSaslClientTransport : public TSaslTransport
        {
        public:

            BeikeTSaslClientTransport(std::shared_ptr<sasl::TSasl> saslClient, std::shared_ptr<TTransport> transport);

        protected:
            virtual void setupSaslNegotiationState() override;


            virtual void resetSaslNegotiationState() override;

            /// Handle any startup messages.
            virtual void handleSaslStartMessage() override;
        };

    }
}
} // apache::thrift::transport


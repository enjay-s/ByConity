#pragma once

#include <string>

#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransport.h>
#include <thrift/transport/TVirtualTransport.h>

#include "Access/BeikeSaslClient.h"
#include <Storages/Hive/TSaslClientTransport.h>
namespace apache
{
namespace thrift
{
    namespace transport
    {

        /*enum NegotiationStatus
        {
            TSASL_INVALID = -1,
            TSASL_START = 1,
            TSASL_OK = 2,
            TSASL_BAD = 3,
            TSASL_ERROR = 4,
            TSASL_COMPLETE = 5
        };*/

        /*static const int MECHANISM_NAME_BYTES = 1;
        static const int STATUS_BYTES = 1;
        static const int PAYLOAD_LENGTH_BYTES = 4;
        static const int HEADER_LENGTH = STATUS_BYTES + PAYLOAD_LENGTH_BYTES;

        */
        class TSaslTransport : public TVirtualTransport<TSaslTransport>
        {
        public:
            TSaslTransport(std::shared_ptr<TTransport> transport);


            TSaslTransport(std::shared_ptr<sasl::TSasl> saslClient, std::shared_ptr<TTransport> transport);


            virtual ~TSaslTransport() override;


            virtual bool isOpen() const override;


            virtual bool peek() override;


            virtual void open() override;
            virtual void close() override;


            uint32_t read(uint8_t * buf, uint32_t len);


            void write(const uint8_t * buf, uint32_t len);


            virtual void flush() override;


            std::shared_ptr<TTransport> getUnderlyingTransport() { return transport_; }


            std::string getUsername();

        protected:
            /// Underlying transport
            std::shared_ptr<TTransport> transport_;

            /// Buffer for reading and writing.
            TMemoryBuffer * memBuf_;

            /// Sasl implementation class. This is passed in to the transport constructor
            /// initialized for either a client or a server.
            std::shared_ptr<sasl::TSasl> sasl_;

            /// IF true we wrap data in encryption.
            bool shouldWrap_;

            /// True if this is a client.
            bool isClient_;

            /// Buffer to hold protocol info.
            boost::scoped_array<uint8_t> protoBuf_;

            static void encodeInt(uint32_t x, uint8_t * buf, uint32_t offset) { *(reinterpret_cast<uint32_t *>(buf + offset)) = htonl(x); }

            static uint32_t decodeInt(uint8_t * buf, uint32_t offset) { return ntohl(*(reinterpret_cast<uint32_t *>(buf + offset))); }


            void doSaslNegotiation();


            virtual void setupSaslNegotiationState() = 0;

            virtual void resetSaslNegotiationState() = 0;


            uint8_t * receiveSaslMessage(NegotiationStatus * status, uint32_t * length);


            void sendSaslMessage(const NegotiationStatus status, const uint8_t * payload, const uint32_t length, bool flush = true);

            uint32_t readLength();


            void writeLength(uint32_t length);
            virtual void handleSaslStartMessage() = 0;

            /// If memBuf_ is filled with bytes that are already read, and has crossed a size
            /// threshold (see implementation for exact value), resize the buffer to a default value.
            void shrinkBuffer();
        };

    }
}
} // apache::thrift::transport

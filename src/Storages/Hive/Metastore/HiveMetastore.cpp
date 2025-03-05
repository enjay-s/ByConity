#include <functional>
#include <numeric>
#include <sstream>

#include <Storages/Hive/Metastore/HiveMetastore.h>
#include <boost/fiber/algo/algorithm.hpp>
#include <fmt/format.h>
#include <Common/Exception.h>
#if USE_HIVE

#include <hive_metastore_types.h>
#include <Access/KerberosInit.h>
#include <Storages/Hive/CnchHiveSettings.h>
#include <Storages/Hive/TSaslClientTransport.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TSocket.h>

#include <random>
#include <Parsers/formatTenantDatabaseName.h>
#include <Storages/Hive/Metastore/MetastoreConvertUtils.h>
#include <Storages/Hive/BeikeTSaslClientTransport.h>

#include <boost/algorithm/string.hpp>
namespace DB
{
namespace ErrorCodes
{
    extern const int NO_HIVEMETASTORE;
    extern const int BAD_ARGUMENTS;
    extern const int NETWORK_ERROR;
} // namespace ErrorCodes

static const unsigned max_hive_metastore_client_connections = 16;
static const int max_hive_metastore_client_retry = 3;
static const UInt64 get_hive_metastore_client_timeout = 1000000;
static const int hive_metastore_client_conn_timeout_ms = 10000;
static const int hive_metastore_client_recv_timeout_ms = 10000;
static const int hive_metastore_client_send_timeout_ms = 10000;
using namespace std;
ThriftHiveMetastoreClientPool::ThriftHiveMetastoreClientPool(ThriftHiveMetastoreClientBuilder builder_)
    : PoolBase<Object>(max_hive_metastore_client_connections, getLogger("ThriftHiveMetastoreClientPool")), builder(builder_)
{
}

HiveMetastoreClient::HiveMetastoreClient(ThriftHiveMetastoreClientBuilder builder) : client_pool(builder)
{
}

void HiveMetastoreClient::tryCallHiveClient(std::function<void(ThriftHiveMetastoreClientPool::Entry &)> func)
{
    int i = 0;
    String err_msg;
    for (; i < max_hive_metastore_client_retry; ++i)
    {
        auto client = client_pool.get(get_hive_metastore_client_timeout);
        try
        {
            func(client);
        }
        catch (apache::thrift::transport::TTransportException & e)
        {
            client.expire();
            err_msg = e.what();
            continue;
        }
        break;
    }
    if (i >= max_hive_metastore_client_retry)
        throw Exception(ErrorCodes::NO_HIVEMETASTORE, "Hive Metastore expired because {}", err_msg);
}

void HiveMetastoreClient::getConfigValue(std::string & value, const std::string & name, const std::string & defaultValue)
{
    tryCallHiveClient([&](auto & client) { client->get_config_value(value, name, defaultValue); });
}

Strings HiveMetastoreClient::getAllDatabases()
{
    Strings databases;
    tryCallHiveClient([&](auto & client) { client->get_all_databases(databases); });
    return databases;
}

std::shared_ptr<ApacheHive::Database> HiveMetastoreClient::getDatabase(const String & db_name)
{
    auto database = std::make_shared<Apache::Hadoop::Hive::Database>();
    auto client_call = [&](auto & client) { client->get_database(*database, db_name); };
    tryCallHiveClient(client_call);
    return database;
}

Strings HiveMetastoreClient::getAllTables(const String & db_name)
{
    Strings tables;
    tryCallHiveClient([&](auto & client) {
        client->get_all_tables(tables, getOriginalDatabaseName(db_name));
    });
    return tables;
}

std::shared_ptr<ApacheHive::Table> HiveMetastoreClient::getTable(const String & db_name, const String & table_name)
{
    auto table = std::make_shared<ApacheHive::Table>();
    tryCallHiveClient([&] (auto & client)
    {
        client->get_table(*table, getOriginalDatabaseName(db_name), table_name);
    });
    table->dbName =db_name;

    return table;
}

bool HiveMetastoreClient::isTableExist(const String & db_name, const String & table_name)
{
    bool found = true;
    auto table = std::make_shared<ApacheHive::Table>();
    tryCallHiveClient([&](auto & client) {
        try
        {
            client->get_table(*table, getOriginalDatabaseName(db_name), table_name);
        }
        catch (ApacheHive::NoSuchObjectException)
        {
            found = false;
        }
    });
    table->dbName = db_name;
    return found;
}


std::optional<TableStatistics> HiveMetastoreClient::getTableStats(
    const String & db_name_may_with_tenant_id, const String & table_name, const Strings & col_names, const bool merge_all_partition )
{
    auto db_name = getOriginalDatabaseName(db_name_may_with_tenant_id);
    auto table = getTable(db_name, table_name);
    if (table->partitionKeys.empty() || !merge_all_partition)
    {
        ApacheHive::TableStatsRequest req;
        ApacheHive::TableStatsResult result;
        req.colNames = col_names;
        req.dbName = db_name;
        req.tblName = table_name;
        tryCallHiveClient([&](auto & client) { client->get_table_statistics_req(result, req); });
        if (table->parameters.contains("numRows"))
        {
            return MetastoreConvertUtils::convertHiveStats({std::stol(table->parameters.at("numRows")), result});
        }
        else
        {
            return MetastoreConvertUtils::convertHiveStats({0, {}});
        }
    }

    auto partitions = getPartitionsByFilter(db_name, table_name, "");
    Strings partition_keys;
    std::vector<Strings> partition_values;

    partition_keys.reserve(table->partitionKeys.size());
    for (const auto & field : table->partitionKeys)
    {
        partition_keys.emplace_back(field.name);
    }

    partition_values.reserve(partitions.size());
    for (const auto & p : partitions)
    {
        partition_values.emplace_back(p.values);
    }

    auto partition_stats = getPartitionStats(db_name, table_name, col_names, partition_keys, partition_values);

    auto stats = MetastoreConvertUtils::merge_partition_stats(*table, partitions, partition_stats);

    return MetastoreConvertUtils::convertHiveStats(stats);
}




ApacheHive::PartitionsStatsResult HiveMetastoreClient::getPartitionStats(
    const String & db_name,
    const String & table_name,
    const Strings & col_names,
    const Strings & partition_keys,
    const std::vector<Strings> & partition_vals)
{
    ApacheHive::PartitionsStatsRequest req;
    ApacheHive::PartitionsStatsResult result;
    req.dbName = db_name;
    req.tblName = table_name;
    req.colNames = col_names;
    Strings partitions;
    for (const auto & v : partition_vals)
    {
        partitions.emplace_back(MetastoreConvertUtils::concatPartitionValues(partition_keys, v));

    }
    req.partNames = partitions;
    tryCallHiveClient([&](auto & client) { client->get_partitions_statistics_req(result, req); });
    return result;
}

// ApacheHive::TableStatsResult HiveMetastoreClient::getPartitionedTableStats(
//     const String & db_name, const String & table_name, const Strings &
//     col_names, const std::vector<ApacheHive::Partition> & partitions)
// {
//     // Strings partitions_str;
//     // for(const auto & p : partitions)
//     // {
//     //     partitions_str.emplace_back(fmt::join(partitions.))
//     // }
//     // getPartitionStats(db_name, table_name, col_names, )
//     return {};
// }

std::vector<ApacheHive::Partition>
HiveMetastoreClient::getPartitionsByFilter(const String & db_name, const String & table_name, const String & filter)
{
    std::vector<ApacheHive::Partition> partitions;
    tryCallHiveClient([&](auto & client) {
        if (filter.empty())
            client->get_partitions(partitions, getOriginalDatabaseName(db_name), table_name, -1);
        else
            client->get_partitions_by_filter(partitions, getOriginalDatabaseName(db_name), table_name, filter, -1);
    });

    return partitions;
}

HiveMetastoreClientFactory & HiveMetastoreClientFactory::instance()
{
    static HiveMetastoreClientFactory factory;
    return factory;
}

HiveMetastoreClientPtr HiveMetastoreClientFactory::getOrCreate(const String & name, const std::shared_ptr<CnchHiveSettings> & settings)
{
    auto settings_hash =  std::hash<std::string>{}(settings->toString());
    auto key = name + "#" + std::to_string(settings_hash);
    std::lock_guard lock(mutex);
    auto it = clients.find(key);

    if (it == clients.end())
    {
        auto builder = [name, settings]() { return createThriftHiveMetastoreClient(name, settings); };

        auto client = std::make_shared<HiveMetastoreClient>(builder);
        clients.emplace(key, client);
        return client;
    }

    return it->second;
}


static int SaslLogCallback(void* context, int level, const char* message) {
    if (message == nullptr) return SASL_BADPARAM;
    const char* authctx = (context == nullptr) ? "Unknown" :
        reinterpret_cast<const char*>(context);

    switch (level) {
        case SASL_LOG_NONE:  // "Don't log anything"
        case SASL_LOG_PASS:  // "Traces... including passwords" - don't log!
          break;
        case SASL_LOG_ERR: // "Unusual errors"
        case SASL_LOG_FAIL: // "Authentication failures"
            LOG(ERROR) << "SASL message (" << authctx << "): " << message;
            break;
        case SASL_LOG_WARN: // "Non-fatal warnings"
            LOG(WARNING) << "SASL message (" << authctx << "): " << message;
            break;
        case SASL_LOG_NOTE: // "More verbose than WARN"
            LOG(INFO) << "SASL message (" << authctx << "): " << message;
            break;
        case SASL_LOG_DEBUG: // "More verbose than NOTE"
            LOG(DEBUG) << "SASL message (" << authctx << "): " << message;
            break;
        case SASL_LOG_TRACE: // "Traces of internal protocols"
        default:
            LOG(TRACE) << "SASL message (" << authctx << "): " << message;
            break;
    }

    return SASL_OK;
}


using boost::algorithm::is_any_of;
using boost::algorithm::split;
using boost::algorithm::trim;

int SaslAuthorizeInternal(sasl_conn_t* /*conn*/, void* /*context*/,
    const char* requested_user, unsigned rlen,
    const char* /*auth_identity*/, unsigned /*alen*/,
    const char* /*def_realm*/, unsigned /*urlen*/,
    struct propctx* /*propctx*/) {
    string requested_principal(requested_user, rlen);
    vector<string> names;

    split(names, requested_principal, is_any_of("/@"));

    if (names.size() != 3) {
        LOG(INFO) << "Kerberos principal should be of the form: "
                  << "<service>/<hostname>@<realm> - got: " << requested_user;
        return SASL_BADAUTH;
    }
   /* SecureAuthProvider* internal_auth_provider;
    if (context == NULL) {
        internal_auth_provider = static_cast<SecureAuthProvider*>(
            AuthManager::GetInstance()->GetInternalAuthProvider());
    } else {
        // Branch should only be taken for testing, where context is used to inject an auth
        // provider.
        internal_auth_provider = static_cast<SecureAuthProvider*>(context);
    }*/

    vector<string> whitelist;
    split(whitelist,"hdfs,bigdata" , is_any_of(","));
    //whitelist.push_back(internal_auth_provider->service_name());
    for (string& s: whitelist) {
        trim(s);
        if (s.empty()) continue;
        if (names[0] == s) {
            // We say "principal" here becase this is for internal communication, and hence
            // ought always be --principal or --be_principal
            LOG(INFO) << "Successfully authenticated principal \"" << requested_principal
                    << "\" on an internal connection";
            return SASL_OK;
        }
    }
    LOG(INFO) << "Principal \"" << requested_principal << "\" not authenticated. "
              << "Reason: 'service' does not match from <service>/<hostname>@<realm>.\n";
    return SASL_BADAUTH;
}


void SetMaxMessageSize(TTransport* transport) {
    static int message_size = 1024 * 1024 * 1024;
    // TODO: Find way to assign TConfiguration through TTransportFactory instead.
    transport->getConfiguration()->setMaxMessageSize(message_size);
    transport->updateKnownMessageSize(-1);
    transport->checkReadBytesAvailable(message_size);
}

std::shared_ptr<Apache::Hadoop::Hive::ThriftHiveMetastoreClient>
HiveMetastoreClientFactory::createThriftHiveMetastoreClient(const String & name, const std::shared_ptr<CnchHiveSettings> & settings)
{
    using namespace apache::thrift;
    using namespace apache::thrift::protocol;
    using namespace apache::thrift::transport;
    using namespace Apache::Hadoop::Hive;

    Poco::URI hive_metastore_url(name);
    auto host = hive_metastore_url.getHost();
    auto port = hive_metastore_url.getPort();

    std::shared_ptr<TSocket> socket = std::make_shared<TSocket>(host, port);
    socket->setKeepAlive(true);
    socket->setConnTimeout(hive_metastore_client_conn_timeout_ms);
    socket->setRecvTimeout(hive_metastore_client_recv_timeout_ms);
    socket->setSendTimeout(hive_metastore_client_send_timeout_ms);
    std::shared_ptr<TTransport> transport = std::make_shared<TBufferedTransport>(socket);


    if (settings && settings->hive_metastore_client_kerberos_auth)
    {
        if (settings->hive_metastore_client_auth_beike)
        {
            kerberosInit(settings->hive_metastore_client_keytab_path, settings->hive_metastore_client_principal);
            if (settings->hive_metastore_client_auth_method)
            {
                std::shared_ptr<sasl::TSasl> sasl_client;
                const map<string, string> props; // Empty; unused by thrift
                const string auth_id; // Empty; unused by thrift

                static const string KERBEROS_MECHANISM = "GSSAPI";
                static vector<sasl_callback_t> KERB_INT_CALLBACKS; // Internal kerberos connections

                KERB_INT_CALLBACKS.resize(3);

                KERB_INT_CALLBACKS[0].id = SASL_CB_LOG;
                KERB_INT_CALLBACKS[0].proc = reinterpret_cast<int (*)()>(&SaslLogCallback);
                static string kerberos_in = "Kerberos (internal)";
                KERB_INT_CALLBACKS[0].context = reinterpret_cast<void *>(kerberos_in.data());

                KERB_INT_CALLBACKS[1].id = SASL_CB_PROXY_POLICY;
                KERB_INT_CALLBACKS[1].proc = reinterpret_cast<int (*)()>(&SaslAuthorizeInternal);
                KERB_INT_CALLBACKS[1].context = nullptr;
                KERB_INT_CALLBACKS[2].id = SASL_CB_LIST_END;
                try
                {
                    sasl_client.reset(new sasl::TSaslClient(
                        KERBEROS_MECHANISM,
                        auth_id,
                        settings->hive_metastore_client_service_name,
                        settings->hive_metastore_client_service_fqdn,
                        props,
                        KERB_INT_CALLBACKS.data()));
                }
                catch (sasl::SaslClientImplException & e)
                {
                    LOG(ERROR) << "Failed to create a GSSAPI/SASL client: " << e.what();
                    throw Exception(ErrorCodes::BAD_ARGUMENTS, "Failed to create a GSSAPI/SASL ", name, e.what());
                }
                (&transport)->reset(new BeikeTSaslClientTransport(sasl_client, transport));
                SetMaxMessageSize(transport.get());
                LOG(INFO) << "Initiating client connection using principal ";
            }
            else
            {
                transport = TSaslClientTransport::wrapClientTransports(
                    settings->hive_metastore_client_service_fqdn, settings->hive_metastore_client_service_name, transport);
            }
        }
        else
        {
            String hadoop_kerberos_principal = fmt::format(
                "{}/{}", settings->hive_metastore_client_principal.toString(), settings->hive_metastore_client_service_fqdn.toString());
            kerberosInit(settings->hive_metastore_client_keytab_path, hadoop_kerberos_principal);
            transport = TSaslClientTransport::wrapClientTransports(
                settings->hive_metastore_client_service_fqdn, settings->hive_metastore_client_principal, transport);
        }
    }
    std::shared_ptr<TProtocol> protocol = std::make_shared<TBinaryProtocol>(transport);
    std::shared_ptr<ThriftHiveMetastoreClient> thrift_client = std::make_shared<ThriftHiveMetastoreClient>(protocol);
    try
    {
        transport->open();
    }
    catch (TException & tx)
    {
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "connect to hive metastore: {} failed. {}", name, tx.what());
    }

    return thrift_client;
}



} // namespace DB

#endif

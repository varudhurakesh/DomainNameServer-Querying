/**********************************
* FILE NAME: domainnamequery.cpp
* Author:  Rakesh Varudu
*
* DESCRIPTION: DNS Query to top 10 ranked websites.
**********************************/


#if 0
g++ -Wall -Werror --std=c++11 $0 -o $0.bin -g -lmysqlpp -lldns -lrt && exec $0.bin $@
exit $?
#else

#define MYSQLPP_MYSQL_HEADERS_BURIED
#include <mysql++/mysql++.h>
#include <ldns/ldns.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <string>
#include <iostream>
#include <math.h>
#include <time.h>
#include <unistd.h>

/**********************************
* Class NAME: DomainNameQuery
*
*
* DESCRIPTION:  Definition of DomainNameQuery class
* 				Random strings are appended to domain names
* 				New domain names are queried
* 				All metrics are measured here
*
**********************************/

class DomainNameQuery {
public:
    DomainNameQuery(mysqlpp::Connection &conn, const std::string domain);
    ~DomainNameQuery();
    DomainNameQuery(const DomainNameQuery &) = delete;
    DomainNameQuery & operator=(const DomainNameQuery&) = delete;
    void init(mysqlpp::Row &row);
    void query();
    float avg() const { return ((float)sum_) / ntimes; }
    float stddev() const {
        float mean = avg();
        return sqrtf((float(sumsq_) / ntimes) - (mean * mean));
    }

    uint32_t numberoftimes() const { return ntimes; }
    uint32_t sum() const { return sum_; }
    uint64_t sumsq() const { return sumsq_; }
    const std::string &domain() const { return domain_; }
    time_t last_time() const { return last_time_; }

    std::string get_query_domain() const;

private:
    mysqlpp::Connection &conn_;
    std::string domain_;
    uint32_t sum_;
    uint64_t sumsq_;
    uint32_t ntimes;
    time_t last_time_;

    ldns_resolver *resolver_;
};

DomainNameQuery::DomainNameQuery(mysqlpp::Connection &conn, const std::string domain) :
    conn_(conn), domain_(domain), sum_(0), sumsq_(0), ntimes(0),
    last_time_(0), resolver_(NULL)
{
    ldns_status s = ldns_resolver_new_frm_file(&resolver_, NULL);
    if (s != LDNS_STATUS_OK) throw s;
}

DomainNameQuery::~DomainNameQuery()
{
    if (resolver_) {
        ldns_resolver_deep_free(resolver_);
    }
}

void DomainNameQuery::init(mysqlpp::Row &row)
{
    sum_ = row["sum_in_ms"];
    sumsq_ = row["sum_sqrt_in_ms"];
    ntimes = row["num_queries"];
}

std::string DomainNameQuery::get_query_domain() const
{
    char buf[256];
    int i;
    for (i = 0; i < 8; i++) {
        buf[i] = random() % 26 + 'A';
    }
    buf[i++] = '.';
    buf[i] = '\0';

    return std::string(buf) + domain_;
}

//The actual work of this program, i.e, Querying the domain names and calculating the latencies.
void DomainNameQuery::query()
{
    struct timespec start;
    // UTC time - Fetched from Procesor - EPOC
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);

    std::string domain_to_query = get_query_domain();
    // creates a new dname rdf from a string. Returns ldns_rdf* or NULL in case of an error (Eg: Invalid domanin)
    ldns_rdf *rdf = ldns_dname_new_frm_str(domain_to_query.c_str());
    if (rdf == NULL) {
        std::cerr << domain_to_query << " is not a valid domain" << std::endl;
        return;
    }
    //LDNS_RR_TYPE_A is a host address
    //RR Type LDNS_RR_CLASS_IN is the Internet
    //LDNS_RD- Recursion Desired - query flag
    ldns_pkt *p = ldns_resolver_query(resolver_, rdf, LDNS_RR_TYPE_A,
        LDNS_RR_CLASS_IN, LDNS_RD);
    if (p != NULL) {
    	// Timestamp after the DNS query is resolved
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC_RAW, &end);
        //Measured in milliseconds
        uint32_t ms = (end.tv_sec - start.tv_sec) * 1000 +
            (end.tv_nsec - start.tv_nsec) / 1000000;
        sum_ += ms;
        sumsq_ += ms * ms;
        ++ntimes;
        //Latest timestamp - On NULL, time() will return UTC
        last_time_ = time(NULL);
        // Free the pointer p as the Querying is already completed
        ldns_pkt_free(p);
    } else {
        std::cerr << domain_to_query << " query failed" << std::endl;
    }

    ldns_rdf_deep_free(rdf);
}

/**********************************
* Class NAME: MultithreadQuery
*
*
* DESCRIPTION:  Definition of MultithreadQuery class
* 				10 websites exists and they can queried parallely
* 				One thread per domain name query
*
**********************************/
class MultithreadQuery
{
public:
    MultithreadQuery(const std::list< std::shared_ptr < DomainNameQuery > > &domains,
                  int threads);
    ~MultithreadQuery();
    void run();

private:
    std::list< std::shared_ptr < DomainNameQuery > > domains_;
    std::vector< std::shared_ptr<std::thread> > threads_;
    std::mutex lock_;
};

MultithreadQuery::MultithreadQuery(const std::list< std::shared_ptr < DomainNameQuery > > &domains, int threads) :
    domains_(domains)
{
    for (int i = 0; i < threads; i++) {
        std::shared_ptr<std::thread> t(new std::thread(std::bind(&MultithreadQuery::run, this)));
        threads_.push_back(t);
    }
}

MultithreadQuery::~MultithreadQuery()
{
    for (auto t : threads_) {
    	// Wait till all you thread joins the parent
        t->join();
    }
}

void MultithreadQuery::run()
{
    while (true) {
        std::shared_ptr < DomainNameQuery > d;
        {
            std::lock_guard<std::mutex> _(lock_);

            if (domains_.empty()) {
                return;
            }
            d = domains_.front();
            domains_.pop_front();
        }
        d->query();
    }
}

/**********************************
* Class NAME: MysqlQuery
*
*
* DESCRIPTION:  Definition of MysqlQuery class
* 				Run the DNS query and store the results in tables
*
**********************************/
struct mysql_options {
    std::string database;
    std::string user;
    std::string server;
    std::string password;
    std::string table;
};

class MysqlQuery {
public:
    MysqlQuery(const struct mysql_options &dbopt);
    ~MysqlQuery();

    void init();

    void queryAll(int parallel);

private:
    bool init_;
    mysqlpp::Connection conn_;
    struct mysql_options dbopt_;
    std::list< std::shared_ptr<DomainNameQuery> > domains_;
};

MysqlQuery::MysqlQuery(const struct mysql_options &dbopt) :
    init_(false), dbopt_(dbopt)
{
}

MysqlQuery::~MysqlQuery()
{
    if (init_) {
        try {
            conn_.disconnect();
        } catch (...) {}
    }
    mysql_library_end();
}


void MysqlQuery::init()
{
    uint32_t count=0;

    if (init_) return;

    conn_.connect(dbopt_.database.c_str(), dbopt_.server.c_str(),
        dbopt_.user.c_str(), dbopt_.password.c_str());

    init_ = true;

    try {
    	// Check if the domain table has already been created, if not throw an exception
        mysqlpp::Query query = conn_.query();
        query << "select * from domains";
        mysqlpp::UseQueryResult res = query.use();

    } catch  (const std::exception &e) {
        // If received an exception, create the domains table
        mysqlpp::Query query = conn_.query();
        query << "create table domains ("
        "rank INT NOT NULL AUTO_INCREMENT,"
        "name VARCHAR(255) NOT NULL,"
        "PRIMARY KEY (rank) );";
        query.execute();

        std::string domainNames[10] = {"google.com", "facebook.com", "youtube.com", "yahoo.com", "live.com",
                                    "wikipedia.org", "baidu.com", "blogger.com", "msn.com", "qq.com"};

        for (count = 0; count < 10; count++)
        {
            query << "insert into domains (name) values (\"" << domainNames[count] << "\");";
            query.execute();
        }

    }

    try {
    	// Check if the queries_result table has already been created, if not throw an exception
        mysqlpp::Query query = conn_.query();
        query << "select * from " << dbopt_.table;
        mysqlpp::UseQueryResult res = query.use();
        while (mysqlpp::Row row = res.fetch_row()) {
            std::string name(row["name"]);
            std::shared_ptr<DomainNameQuery> d(new DomainNameQuery(conn_, name));
            domains_.push_back(d);
            domains_.back()->init(row);
        }

    } catch (const std::exception &e) { 
    	// If received an exception, then create the queries_result table
        mysqlpp::Query query = conn_.query();
        query << "create table " << dbopt_.table << "("
            "name VARCHAR(255) PRIMARY KEY,"
            "avg_in_ms FLOAT UNSIGNED,"
            "stddev_in_ms FLOAT UNSIGNED,"
            "sum_in_ms INT UNSIGNED,"
            "sum_sqrt_in_ms BIGINT,"
            "num_queries INT UNSIGNED,"
            "first_ts TIMESTAMP default 0,"
            "last_ts TIMESTAMP default 0);";
        query.execute();

        // load from list of domains to query
        query = conn_.query("select * from domains");
        mysqlpp::UseQueryResult res = query.use();
        while (mysqlpp::Row row = res.fetch_row()) {
            std::string name(row["name"]);
            std::shared_ptr<DomainNameQuery> d(new DomainNameQuery(conn_, name));
            domains_.push_back(d);
        }
    }
    if (domains_.empty()) {
        std::cerr << "no domains to query" << std::endl;
        exit(1);
    }
}

void MysqlQuery::queryAll(int parallel)
{
    {
        // Parallel execution of query domains
        MultithreadQuery(domains_, parallel);
    }

    for (auto d : domains_) {
        if (d->numberoftimes() == 0) {
            continue;
        }

        try {
            mysqlpp::Query query = conn_.query();
            static char timebuf[sizeof("2015-21-03 01:43:00")];
            struct tm t;
            time_t query_time = d->last_time();
            localtime_r(&query_time, &t);
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %T", &t);

            if (d->numberoftimes() > 1) {
                query << "update " << dbopt_.table << " set"
                      << " avg_in_ms=" << d->avg()
                      << " ,stddev_in_ms=" << d->stddev()
                      << " ,sum_in_ms=" << d->sum()
                      << " ,sum_sqrt_in_ms=" << d->sumsq()
                      << " ,num_queries=" << d->numberoftimes()
                      << " ,last_ts=" << mysqlpp::quote << timebuf
                      << " where name=" << mysqlpp::quote << d->domain();
            } else if (d->numberoftimes() == 1) {
                query << "insert into " << dbopt_.table << " values("
                      << mysqlpp::quote << d->domain()
                      << "," << d->avg()
                      << "," << d->stddev()
                      << "," << d->sum()
                      << "," << d->sumsq()
                      << "," << d->numberoftimes()
                      << "," << mysqlpp::quote << timebuf
                      << "," << mysqlpp::quote << timebuf
                      << ")";
            }

            query.execute();
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }
    }
}

static void strtoint(const std::string &s, int *out_val)
{
    std::stringstream ss(s);
    int val;

    ss >> val;
    if (!ss.fail()) {
        *out_val = val;
    }
}

static std::string read_password(const std::string &file)
{
    std::ifstream ifs(file);
    std::string res;
    getline(ifs, res);
    ifs.close();
    return res;
}

static void usage(const char *name)
{
    std::cerr << name
              << " -d <mysql database>"
              << " -u <mysql user>"
              << " -s <mysql server>"
              << " -p <passwd file>"
              << " -f <frequency in seconds>"
              << " -i <iterations, 0 for infinite>"
              << " -P <number of parallel queries>"
              << " -t <table to store result into>"
              << std::endl;
    exit(1);
}

/**********************************
* Function name: main
*
*
* DESCRIPTION:  Queries are performed based on the input parameters received
*
**********************************/
int main(int argc, char *argv[])
{
    int i = 1;
    int frequency = 10;
    int iterations = 0;
    int parallel = 10;
    struct mysql_options dbopt = {
        "dnsquerydb", "root", "localhost", "", "queries_result"
    };
    std::string password_file = "passwd";

    for (; i < argc; i++) {
        if (argv[i][0] == '-') {
            char opt = argv[i][1];

            if (opt == '\0') break;

            if (++i < argc) {
                switch (opt) {
                // mysql options
                case 'd':
                    dbopt.database = argv[i];
                    break;
                case 'u':
                    dbopt.user = argv[i];
                    break;
                case 's':
                    dbopt.server = argv[i];
                    break;
                case 'p':
                    password_file = argv[i];
                    break;
                case 't':
                    dbopt.table = argv[i];
                    break;

                // program options
                case 'f':
                    strtoint(argv[i], &frequency);
                    break;
                case 'i':
                    strtoint(argv[i], &iterations);
                    break;
                case 'P':
                    strtoint(argv[i], &parallel);
                    break;
                default:
                    std::cerr << "unrecognized option " << opt << std::endl;
                    usage(argv[0]);
                };
            } else {
                usage(argv[0]);
            }
        }
    }

    std::cout << argv[0]
              << " -d " << dbopt.database
              << " -u " << dbopt.user
              << " -s " << dbopt.server
              << " -p " << password_file
              << " -f " << frequency
              << " -i " << iterations
              << " -P " << parallel
              << " -t " << dbopt.table
              << std::endl;

    dbopt.password = read_password(password_file);
    if (dbopt.password.empty()) {
        std::cerr << "password not provided" << std::endl;
        return 1;
    }

    MysqlQuery query(dbopt);
    try {
        query.init();
    } catch (const std::exception &e) {
        std::cerr << "unable to connect to database: " << e.what()
                  << std::endl;
        return 1;
    }

    for (int i = 0; iterations == 0 || i < iterations; i++) {
        if (i != 0) {
            sleep(frequency);
        }
        query.queryAll(parallel);
    }

    return 0;
}

#endif


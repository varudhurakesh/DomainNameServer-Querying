C++ Coding Challenge
=================================
Title: Tracking DNS performance to top sites

Importance: Such performance numbers to top sites can be used as benchmarks for others to compare to.

Description:
Write a C++ (not C) program on Linux/BSD(Mac) that periodically sends DNS queries to the nameservers of the top 10 Alexa domains and stores the latency values in a mysql table. The frequency of queries should be specified by the user on command line. The program needs to make sure it doesn't hit the DNS cache while trying to query for a site and should use a random string prepended to a domain. E.g. to query foo.bar, make sure to prepend a random string, e.g. 1234xpto.foo.bar.

Besides the timeseries values, the code needs to keep track in db stats per domain about:
+ the average query times
+ standard deviation of DNS query times
+ number of queries made so far
+ time stamp of first query made per domain and last query made


Refs:
a. Mysql lib, use mysql++:
http://tangentsoft.net/mysql++/
b. DNS lib, use ldns:
http://www.nlnetlabs.nl/projects/ldns/


Top 10 domains to query:
+------+---------------+
| rank | name      	|
+------+---------------+
|	1 | google.com	|
|	2 | facebook.com  |
|	3 | youtube.com   |
|	4 | yahoo.com 	|
|	5 | live.com  	|
|	6 | wikipedia.org |
|	7 | baidu.com 	|
|	8 | blogger.com   |
|	9 | msn.com   	|
|   10 | qq.com    	|
+------+---------------+

=================================


Solution:
========
# DomainNameServer-Querying

This application will run DNS queries to top 10 alexa websites (predefined list) in configurable time intervals and iterations

I. Softwares to be installed:
Ubuntu 14.04 LTS version is used
sudo apt-get install mySQL-server
sudo apt-get install libmysqlclient15-dev
sudo apt-get install libmysqlc++-dev
sudo apt-get install libmysql++
sudo apt-get install libmysql++-dev
sudo apt-get install gcc
sudo apt-get install g++

II. My SQL commands:

1)
Login to MYSQL as below:

mysql -u root -p

Enter the password as set during the mysql++ installation

2)

Before running the application, a database has to be created in mysql as below:

create database dnsquerydb;

3)

We can verify if the database wtih the name 'dnsquerydb' has been created or not with the following command:

show databases;

4)

Use the database dnsquerydb as below:

use dnsquerydb;


III. Compiling and running the application


1)

Compile the application file 'dns_application.cpp' present in the downloaded repository 'DomainNameServer-Querying':

cd DomainNameServer-Querying

g++ -o test dns_application.cpp -L/usr/include/mysql -lmysqlclient -I/usr/include/mysql -Wall -Werror --std=c++11 -g -lmysqlpp -lldns -lrt


2)

Different options to run the application are as below:
-d <mysql database>

 -u <mysql user>

-s <mysql server>

-p <passwd file>

-f <frequency in seconds>

-i <iterations, 0 for infinite>

-P <number of parallel queries>

-t <table to store result into>

Application can be run as below:

i. Without specifying any parameters, in which case, it will take the default parameters

./test

Default parameters are as below:

./test -d dnsquerydb -u root -s localhost -p passwd -f 10 -i 0 -P 10 -t queries_result

To stop the application (if run in default mode - continuous iteration), press ctrl-c

ii. With a different parameters for each field as below:

Eg:

/test -d dnsquerydb -u root -s localhost -p passwd -f 5 -i 5 -P 10 -t queries_result 


IV. Verifying the results:

To check results:
select * from queries_result;

An example of the result looks like below:

mysql> select * from queries_result;

+---------------+-----------+--------------+-----------+----------------+-------------+---------------------+---------------------+
| name          | avg_in_ms | stddev_in_ms | sum_in_ms | sum_sqrt_in_ms | num_queries | first_ts            | last_ts             |
+---------------+-----------+--------------+-----------+----------------+-------------+---------------------+---------------------+
| baidu.com     |   115.173 |      549.065 |      9329 |       25493719 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| blogger.com   |   143.395 |      547.875 |     11615 |       25979047 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| facebook.com  |   66.4938 |      33.3404 |      5386 |         448174 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| google.com    |   60.1481 |      31.9958 |      4872 |         375964 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| live.com      |    117.79 |      550.389 |      9541 |       25660995 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| msn.com       |   75.6049 |      32.4843 |      6124 |         548478 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| qq.com        |   623.704 |      890.518 |     50520 |       95744340 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:50 |
| wikipedia.org |   87.8642 |      34.9694 |      7117 |         724381 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
| yahoo.com     |        94 |      34.7787 |      7614 |         813690 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:50 |
| youtube.com   |   72.1358 |       32.736 |      5843 |         508293 |          81 | 2015-03-21 06:08:39 | 2015-03-21 06:22:49 |
+---------------+-----------+--------------+-----------+----------------+-------------+---------------------+---------------------+
10 rows in set (0.00 sec)


V. Remove the tables after finishing with the running of the application

After verifying the results drop the created tables:

drop table domains;

drop table queries_result;


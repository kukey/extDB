/*
* Copyright (C) 2014 Declan Ireland <http://github.com/torndeco/extDB>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


#include "Poco/Data/MySQL/Connector.h"
//#include "Poco/Data/MySQL/MySQLException.h"

#include "Poco/Data/SQLite/Connector.h"
//#include "Poco/Data/SQLite/SQLiteException.h"

#include "Poco/Data/SQLite/Connector.h"
//#include "Poco/Data/SQLite/SQLiteException.h"

#include "Poco/Data/ODBC/Connector.h"
//#include "Poco/Data/ODBC/ODBCException.h"

#include "ext.h"

#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/scoped_ptr.hpp>

#include <Poco/Data/SessionPool.h>
#include <Poco/Exception.h>
#include <Poco/NumberFormatter.h>
#include <Poco/NumberParser.h>
#include <Poco/Util/IniFileConfiguration.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "uniqueid.h"

#include "protocols/abstract_protocol.h"
#include "protocols/db_raw.h"
#include "protocols/misc.h"



Ext::Ext(void) {
	mgr.reset (new IdManager);
    extDB_lock = false;
    if ( !boost::filesystem::exists( "conf-main.ini" ))
    {
        std::cout << "extDB: Unable to find conf-main.ini" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    else
    {
        std::cout << "extDB: Loading conf-main.ini" << std::endl;
        Poco::AutoPtr<Poco::Util::IniFileConfiguration> pConf(new Poco::Util::IniFileConfiguration("conf-main.ini"));
        std::cout << "extDB: Read conf-main.ini" << std::endl;

        max_threads = pConf->getInt("Main.Threads", 0);
        if (max_threads <= 0)
        {
            max_threads = boost::thread::hardware_concurrency();
        }
		io_work_ptr.reset(new boost::asio::io_service::work(io_service));
        for (std::size_t i = 0; i < max_threads; ++i)
        {
            threads.create_thread(boost::bind(&boost::asio::io_service::run, &io_service));
            std::cout << "+1 Thread" << std::endl ;
        }
    }
}

Ext::~Ext(void)
{
    std::cout << "extDB: Stopping Please Wait.." << std::endl ;
    io_service.stop();
    threads.join_all();
    unordered_map_protocol.clear();

    if (boost::iequals(db_type, std::string("MySQL")) == 1)
        Poco::Data::MySQL::Connector::unregisterConnector();
    else if (boost::iequals(db_type, std::string ("ODBC")) == 1)
        Poco::Data::ODBC::Connector::unregisterConnector();
    else if (boost::iequals(db_type, "SQLite") == 1)
        Poco::Data::SQLite::Connector::unregisterConnector();

    std::cout << "extDB: Stopped" << std::endl ;
}

std::string Ext::connectDatabase(const std::string &conf_option)
{
    try
    {
        Poco::AutoPtr<Poco::Util::IniFileConfiguration> pConf(new Poco::Util::IniFileConfiguration("conf-main.ini"));
        if (pConf->hasOption(conf_option + ".Type"))
        {
            std::cout << "extDB: Thread Pool Started" << std::endl;

            // Database
            db_type = pConf->getString(conf_option + ".Type");
            std::string db_name = pConf->getString(conf_option + ".Name");

            int min_sessions = pConf->getInt(conf_option + ".minSessions", 1);
            if (min_sessions <= 0)
            {
                min_sessions = 1;
            }
            const int idle_time = pConf->getInt(conf_option + ".idleTime");

            std::cout << "extDB: " << db_type << std::endl;

            std::string connection_str;

            if ( (boost::iequals(db_type, std::string("MySQL")) == 1) || (boost::iequals(db_type, std::string("ODBC")) == 1) )
            {
                if (boost::iequals(db_type, std::string("MySQL")) == 1)
                {
                    Poco::Data::MySQL::Connector::registerConnector();
                }
                else
                {
                    Poco::Data::ODBC::Connector::registerConnector();
	        }

                std::string username = pConf->getString(conf_option + ".Username");
                std::string password = pConf->getString(conf_option + ".Password");

                std::string ip = pConf->getString(conf_option + ".IP");
                std::string port = pConf->getString(conf_option + ".Port");

                connection_str = "host=" + ip + ";port=" + port + ";user=" + username + ",password=" + password + ",dbname=" + db_name;

                db_pool.reset(new Poco::Data::SessionPool(db_type, connection_str, min_sessions, max_threads, idle_time));

                if (db_pool->get().isConnected())
                {
                    std::cout << "extDB: Database Session Pool Started" << std::endl;
                    return "[\"OK\"]";
                }
                else
                {
                    std::cout << "extDB: Database Session Pool Failed" << std::endl;
                    return "[\"ERROR\",\"Database Session Pool Failed\"]";
                }

            }
            else if (boost::iequals(db_type, "SQLite") == 1)
            {
                Poco::Data::SQLite::Connector::registerConnector();
                connection_str = db_name;

                db_pool.reset(new Poco::Data::SessionPool(db_type, connection_str, min_sessions, max_threads, idle_time));

                if (db_pool->get().isConnected())
                {
                    std::cout << "extDB: Database Session Pool Started" << std::endl;
                    return "[\"OK\"]";
                }
                else
                {
                    std::cout << "extDB: Database Session Pool Failed" << std::endl;
                    return "[\"ERROR\",\"Database Session Pool Failed\"]";
                }
            }
            else
            {
                std::cout << "extDB: No Database Engine Found for " << db_name << "." << std::endl;
                return "[\"ERROR\",\"Unknown Database Type\"]";
            }
        }
        else
        {
            std::cout << "extDB: No Config Option Found: " << conf_option << "." << std::endl;
            return "[\"ERROR\",\"No Config Option Found\"]";
        }
    }
    catch (Poco::Exception& e)
    {
        std::cout << "extDB: Database Setup Failed: " << e.displayText() << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

std::string Ext::version() const
{
    return "0";
}

int Ext::getUniqueID_mutexlock()
{
    boost::lock_guard<boost::mutex> lock(mutex_unique_id);
    return mgr.get()->AllocateId();
}


void Ext::freeUniqueID_mutexlock(const int &unique_id)
{
    boost::lock_guard<boost::mutex> lock(mutex_unique_id);
    mgr.get()->FreeId(unique_id);
}

Poco::Data::Session Ext::getDBSession_mutexlock()
// Gets available DB Session (mutex lock)
{
	boost::lock_guard<boost::mutex> lock(mutex_db_pool);
	return db_pool->get();
}

void Ext::getResult_mutexlock(const int &unique_id, char *output, const int &output_size)
// Gets Result String from unordered map array
//   If length of String = 0, sends arma "", and removes entry from unordered map array
//   If <=, then sends output to arma
//   If >, then sends 1 part to arma + stores rest.
{
    {
        boost::lock_guard<boost::mutex> lock(mutex_unordered_map_results); // TODO Make Lock Smaller
        boost::unordered_map<int, std::string>::const_iterator it = unordered_map_results.find(unique_id);
        if (it == unordered_map_results.end()) // NO UNIQUE ID or WAIT
        {
            if (unordered_map_wait.count(unique_id) == 0)
            {
                //std::snprintf(output, output_size, "[\"ERROR\",\"NO ID FOUND\"]");
                std::strcpy(output, (""));
            }
            else
            {
                //std::snprintf(output, output_size, "[\"WAIT\"]");
                std::strcpy(output, ("[\"WAIT\"]"));
            }
        }
        else if (it->second.length() == 0) // END of MSG
        {
            unordered_map_results.erase(unique_id);
            freeUniqueID_mutexlock(unique_id);
            std::strcpy(output, (""));
        }
        else // SEND MSG (Part)
        {
            std::string msg = it->second.substr(0, output_size-9);
            std::strcpy(output, msg.c_str());
            if (it->second.length() >= (output_size-8))
            {
                unordered_map_results[unique_id] = it->second.substr(output_size-9);
            }
            else
            {
                unordered_map_results[unique_id] = std::string("");
            }
        }
    }
}


void Ext::saveResult_mutexlock(const std::string &result, const int &unique_id)
// Stores Result String  in a unordered map array.
//   Used when string > arma output char
{
    {
        boost::lock_guard<boost::mutex> lock(mutex_unordered_map_results);
        unordered_map_results[unique_id] = "[\"OK\"," + result + "]";
        unordered_map_wait.erase(unique_id);
    }
}


void Ext::sendResult_mutexlock(const std::string &result, char *output, const int &output_size)
// Checks if Result String will fit into arma output char
//   If <=, then sends output to arma
//   if >, then sends ID Message arma + stores rest. (mutex locks)
{
    std::string msg;
    if (result.length() <= (output_size-9))
    {
        msg = "[\"OK\", " + result + "]";
        std::strcpy(output, msg.c_str());
    }
    else
    {
        const int unique_id = getUniqueID_mutexlock();
        saveResult_mutexlock(result, unique_id);
        msg = "[\"ID\",\"" + Poco::NumberFormatter::format(unique_id) + "\"]";
        std::strcpy(output, msg.c_str());
    }
}


std::string Ext::addProtocol(const std::string &protocol, const std::string &protocol_name)
{
	{
		boost::lock_guard<boost::mutex> lock(mutex_unordered_map_protocol);
		if (boost::iequals(protocol, std::string("MISC")) == 1)
		{
			unordered_map_protocol[protocol_name] = boost::shared_ptr<AbstractPlugin> (new MISC());
			return "[\"OK\"]";
		}
		else if (boost::iequals(protocol, std::string("DB_RAW")) == 1)
		{
			unordered_map_protocol[protocol_name] = boost::shared_ptr<AbstractPlugin> (new DB_RAW());
			return "[\"OK\"]";
		}
		else
		{
			return ("[\"ERROR\",\"Error Unknown Protocol\"]");
		}
	}
}


std::string Ext::syncCallPlugin(const std::string protocol, const std::string data)
// Sync callPlugin
{
    if (unordered_map_protocol.find(protocol) == unordered_map_protocol.end())
    {
        return ("[\"ERROR\",\"Error Unknown Protocol\"]");
    }
    else
    {
        return (unordered_map_protocol[protocol].get()->callPlugin(this, data));
    }
}

void Ext::onewayCallPlugin(const std::string protocol, const std::string data)
// ASync callPlugin
{
    if (unordered_map_protocol.find(protocol) != unordered_map_protocol.end())
    {
        unordered_map_protocol[protocol].get()->callPlugin(this, data);
    }
}


void Ext::asyncCallPlugin(const std::string protocol, const std::string data, const int unique_id)
// ASync + Save callPlugin
// We check if Protocol exists here, since its a thread (less time spent blocking arma) and it shouldnt happen anyways
{
    if (unordered_map_protocol.find(protocol) == unordered_map_protocol.end())
    {
        saveResult_mutexlock(std::string("[\"ERROR\",\"Error Unknown Protocol\"]"), unique_id);
    }
    else
    {
        saveResult_mutexlock((unordered_map_protocol[protocol].get()->callPlugin(this, data)), unique_id);
    }
}


void Ext::callExtenion(char *output, const int &output_size, const char *function)
{
    try
    {
        std::string str_input(function);
        const std::string sep_char (":");  // TODO Make Global Value
        std::string msg;

        // Async / Sync
        const int async = Poco::NumberParser::parse(str_input.substr(0,1));

        switch (async)  // TODO Profile using Numberparser versus comparsion of char[0] + if statement checking length of *function
        {
            case 2: //ASYNC + SAVE
            {
                // Protocol
                const std::string::size_type found = str_input.find(sep_char,2);
                const std::string protocol = str_input.substr(2,(found-2));
                // Data
                std::string data = str_input.substr(found+1);

                if (found==std::string::npos)  // Check Invalid Format
                {
                    std::strcpy(output, ("[\"ERROR\",\"Error Invalid Format\"]"));
                    //std::snprintf(output, output_size, "[\"ERROR\",\"Error Invalid Format\"]");
                }
                else
                {
                    int unique_id = getUniqueID_mutexlock();
                    {
                        boost::lock_guard<boost::mutex> lock(mutex_unordered_map_results);
                        unordered_map_wait[unique_id] = true;
                    }
					io_service.post(boost::bind(&Ext::asyncCallPlugin, this, protocol, data, unique_id));                   
                    msg = "[\"ID\",\"" + Poco::NumberFormatter::format(unique_id) + "\"]";
                    std::strcpy(output, msg.c_str());
                }
                break;
            }
            case 5: // GET
            {
                if (str_input.length() >= 2)
                {
                    const int unique_id = Poco::NumberParser::parse(str_input.substr(2));
                    getResult_mutexlock(unique_id, output, output_size);
                }
                break;
            }
            case 1: //ASYNC
            {
                // Protocol
                const std::string::size_type found = str_input.find(sep_char,2);
                const std::string protocol = str_input.substr(2,(found-2));
                // Data
                std::string data = str_input.substr(found+1);

                if (found==std::string::npos)  // Check Invalid Format
                {
                    std::strcpy(output, ("[\"ERROR\",\"Error Invalid Format\"]"));
                    //std::snprintf(output, output_size, "[\"ERROR\",\"Error Invalid Format\"]");
                }
                else
                {
					io_service.post(boost::bind(&Ext::onewayCallPlugin, this, protocol, data));
                    msg = std::string("[\"OK\"]");
                    std::strcpy(output, msg.c_str());
                }
                break;
            }
            case 0: //SYNC
            {
                // Protocol
                const std::string::size_type found = str_input.find(sep_char,2);
                const std::string protocol = str_input.substr(2,(found-2));
                // Data
                std::string data = str_input.substr(found+1);

                if (found==std::string::npos)  // Check Invalid Format
                {
                    std::strcpy(output, ("[\"ERROR\",\"Error Invalid Format\"]"));
                    //std::snprintf(output, output_size, "[\"ERROR\",\"Error Invalid Format\"]");
                }
                else
                {
                    msg = syncCallPlugin(protocol, data);
                    sendResult_mutexlock(msg, output, output_size); // Checks Result length (i.e multipart msgs) + sends to Arma
                }
                break;
            }
            case 9:
            {
                if (!extDB_lock)
                {
                    // Protocol
                    std::string::size_type found = str_input.find(sep_char,2);
                    std::string command;
                    std::string data;
                    if (found==std::string::npos)  // Check Invalid Format
                    {
                        command = str_input.substr(2);
                    }
                    else
                    {
                        command = str_input.substr(2,(found-2));
                        data = str_input.substr(found+1);
                    }
                    if (command == "VERSION")
                    {
                        std::strcpy(output, version().c_str());
                    }
                    else if (command == "DATABASE")
                    {
                        msg = connectDatabase(data);
                        std::strcpy(output, msg.c_str());
                    }
                    else if (command == "ADD")
                    {
                        found = data.find(sep_char);
                        if (found==std::string::npos)  // Check Invalid Format
                        {
                            //std::snprintf(output, output_size, "[\"ERROR\",\"Error Missing Protocol Name\"]");
                            std::strcpy(output, ("[\"ERROR\",\"Error Missing Protocol Name\"]"));
                        }
                        else
                        {
                            msg = addProtocol(data.substr(0,found), data.substr(found+1));
							std::strcpy(output, msg.c_str());
                        }
                    }
                    else if (command == "LOCK")
                    {
                        extDB_lock = true;
                    }
                    else
                    {
                        std::strcpy(output, ("[\"ERROR\",\"Error Invalid extDB Command\"]"));
                        //std::snprintf(output, output_size, "[\"ERROR\",\"Error Invalid extDB Command\"]");
                    }
                    break;
                }
                default:
                {
                    std::strcpy(output, ("[\"ERROR\",\"Error Invalid Message\"]"));
                    //std::snprintf(output, output_size, "[\"ERROR\",\"Error Invalid Message Type\"]");
                }
            }
        }
    }
    catch (Poco::Exception& e)
    {
        std::strcpy(output, ("[\"ERROR\",\"Error Invalid Message\"]"));
        //std::snprintf(output, output_size, "[\"ERROR\",\"ERROR\"]");
        std::cout << "extDB: Error: " << e.displayText() << std::endl;
    }
}

#ifdef TESTING
int main(int nNumberofArgs, char* pszArgs[])
{
    Ext extension;
    char result[255];
    for (;;) {
        char input_str[100];
		std::cin.getline(input_str, sizeof(input_str));
        if (std::string(input_str) == "quit")
        {
            break;
        }
        else
        {
            extension.callExtenion(result, 80, input_str);
            std::cout << "extDB: " << result << std::endl;
        }
    }
    return 0;
}
#endif

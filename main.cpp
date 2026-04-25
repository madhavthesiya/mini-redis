#include <iostream>
#include "InMemoryStorage.h"
#include "RedisLite.h"
#include <sstream>
#include <vector>

void printResult(const std::string& operation, const Result& res) {
    std::cout<<operation<<" -> ";
    if(res.success)
    {
        std::cout<<"SUCCESS";
        if(!res.value.empty())
        {
            std::cout<<", value = "<<res.value;
        }
    }
    else
    {
        std::cout<<"FAILED";
        if(res.error==ErrorCode::KEY_NOT_FOUND)
        {
            std::cout<<" (key not found)";
        }
        else if(res.error==ErrorCode::INVALID_KEY)
        {
            std::cout<<" (invalid key)";
        }
        else if(res.error==ErrorCode::FILE_ERROR)
        {
            std::cout<<" (file error)";
        }
    }
    std::cout<<std::endl;
}

std::vector<std::string> split(const std::string &line) {
    std::stringstream ss(line);
    std::vector<std::string> tokens;
    std::string word;
    while(ss>>word)
    {
        tokens.push_back(word);
    }
    return tokens;
}

int main() {
    InMemoryStorage storage(100);
    RedisLite redis(storage);

    std::cout<<"Mini-Redis CLI (type EXIT to quit)\n";
    std::cout<<"Commands: SET, GET, DEL, EXISTS, SETEX, EXPIRE, TTL, SAVE, LOAD\n\n";

    while(true)
    {
        std::cout<<"> ";
        std::string line;
        if(!std::getline(std::cin,line)) break;
        if(line=="EXIT")
        {
            break;
        }
        auto token=split(line);
        if(token.empty())
        {
            continue;
        }
        std::string command=token[0];

        if(command=="SET")
        {
            if(token.size()!=3)
            {
                std::cout<<"Usage: SET key value\n";
                continue;
            }
            printResult(command, redis.set(token[1],token[2]));
        }
        else if(command=="SETEX")
        {
            if(token.size()!=4)
            {
                std::cout<<"Usage: SETEX key seconds value\n";
                continue;
            }
            try {
                int ttl = std::stoi(token[2]);
                printResult(command, redis.setex(token[1], ttl, token[3]));
            } catch(...) {
                std::cout<<"Error: seconds must be a valid integer\n";
            }
        }
        else if(command=="GET")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: GET key\n";
                continue;
            }
            printResult(command, redis.get(token[1]));
        }
        else if(command=="DEL")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: DEL key\n";
                continue;
            }
            printResult(command, redis.del(token[1]));
        }
        else if(command=="EXISTS")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: EXISTS key\n";
                continue;
            }
            printResult(command, redis.exists(token[1]));
        }
        else if(command=="EXPIRE")
        {
            if(token.size()!=3)
            {
                std::cout<<"Usage: EXPIRE key seconds\n";
                continue;
            }
            try {
                int ttl = std::stoi(token[2]);
                printResult(command, redis.expire(token[1], ttl));
            } catch(...) {
                std::cout<<"Error: seconds must be a valid integer\n";
            }
        }
        else if(command=="TTL")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: TTL key\n";
                continue;
            }
            printResult(command, redis.ttl(token[1]));
        }
        else if(command=="SAVE")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: SAVE filename\n";
                continue;
            }
            printResult(command, redis.save(token[1]));
        }
        else if(command=="LOAD")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: LOAD filename\n";
                continue;
            }
            printResult(command, redis.load(token[1]));
        }
        else
        {
            std::cout<<"Unknown command\n";
        }
    }
    return 0;
}

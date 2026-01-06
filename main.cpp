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
    InMemoryStorage storage(2);
    RedisLite redis(storage);

    while(true)
    {
        std::cout<<"> ";
        std::string line;
        std::getline(std::cin,line);
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

        //set
        if(command=="SET")
        {
            if(token.size()!=3)
            {
                std::cout<<"Usage: SET key value\n";
                continue;
            }
            else
            {
                printResult(command, redis.set(token[1],token[2]));
            }
        }
        else if(command=="GET")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: GET key\n";
                continue;
            }
            else
            {
                printResult(command, redis.get(token[1]));
            }
        }
        else if(command=="DEL")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: DEL key\n";
                continue;
            }
            else
            {
                printResult(command,redis.del(token[1]));
            }
        }
        else if(command=="EXISTS")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: EXISTS key\n";
                continue;
            }
            else
            {
                printResult(command, redis.exists(token[1]));
            }
        }
        else if(command=="SAVE")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: SAVE filename\n";
                continue;
            }
            else
            {
                printResult(line, redis.save(token[1]));
            }
        }
        else if(command=="LOAD")
        {
            if(token.size()!=2)
            {
                std::cout<<"Usage: LOAD filename\n";
                continue;
            }
            else
            {
                printResult(line,redis.load(token[1]));
            }
        }
        else
        {
            std::cout<<"Unknown command\n";
        }
    }
    return 0;
}

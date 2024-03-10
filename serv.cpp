#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <string.h>
#include "rapidxml-1.13/rapidxml.hpp"
#include <fstream>
#include <algorithm>

using namespace std;
/* portul folosit */
#define PORT 2908

/* codul de eroare returnat de anumite apeluri */
// int errno;
const char* filePath = "timetable.xml";


typedef struct thData
{
    int idThread; // the thread's ID kept in evidence by the serv.cpp
    int cl;       // client's socket descriptor returned by accept function
} thData;

struct request_command{
    thData client_id;   //who made the request
    tm *when;           //when was the request made
    string command;     //what did the client request(the stdin command)
};

struct updated_request_command{
    string command_name;    //the command_key to call the command's class
    request_command old;    //copy of old struct(to be sent in fn. execute)
};

vector<request_command>Command_queue;   //command queue

struct Station{     
    string name;            //station name
    string departure_time;  //departure time in format HH:MM (if it's final st. def:NULL)
    string arrival_time;    //arrival time   in format HH:MM (if it's start st. def:NULL)
    int departure_late;     //departure late in format MMM ex: 120, 60, 5... def:0
    int arrival_late;       //arrival late   in format MMM ex: 120, 60, 5... def:0
    int arrival_early;      //arrival early  in format MMM ex: 120, 60, 5... def:0
};

struct Train{
    string ID;                //train ID ex: IR1972, IR13601, R5009 ...
    vector<Station> stations; //vector of stations
};

vector<Train>Trains;            //vector of Struct Trains
vector<string>Station_map;      //vector with all the Stations names


static void *treat(void *);                      //first function called by each thread(client) who got connected to the server 
void raspunde(void *);                           //second function called by treat which adds client's request to Command Queue
static void *treat_queue(void *);                //function to parse the Command Queue and splits the commands in 2 separate queues
static void *treat_separate_command( void *arg); //function to call an instance of each derivated class regardin the request type ex: 3, 4, 5....


//GET A TIME INPUT AS A STRING AND RETURNES THE NUMBER CONVERTED TO INT
int StringInt(string time){

    int time_int = 0;
    for(int i = 0 ; i < time.size(); i++)
    {
        time_int  = time_int * 10 + (int)(time[i] - '0');
    }
    return time_int;
}

//GET A INT AS INPUT AND RETURNS A STRING(similar to atoi())
char *IntString(int number){

    static char ans[100];
    bzero(ans,100);
    if(number == 0)
        strcpy(ans, "0");
    else
    {
        int nrcf = 0;
        while(number > 0)
        {
            ans[nrcf++] = number % 10 + '0';
            number/=10;
        }
        ans[nrcf]='\0';

        for(int i = 0 ; i < nrcf / 2; i++){
            swap(ans[i],ans[nrcf - i - 1]);
        }
    }
    return ans;
}

//FN TO PARSE THE XML FILE AND FILL ALL THE STRUCTURES FOR A MORE CONVENIENT USE OF THE DATA STRUCTURES
int GetDataFromXML(const char * filePath){

    // Open the file
    ifstream file(filePath);
    if (!file.is_open()) {
        perror("Error: Could not open the XML file\n");
        return errno;
    }
    string xmlData((istreambuf_iterator<char>(file)), istreambuf_iterator<char>()); // parse the XML and deserilize into a string
    rapidxml::xml_document<> doc;
    doc.parse<0>(&xmlData[0]); // Parse the content of the string

    rapidxml::xml_node<>* trainNode = doc.first_node("timetable")->first_node("train");
    while (trainNode) {
        
        Train to_add;
        to_add.ID = trainNode->first_attribute("id")->value();
        {
            // Found the train, now find the station node with the specified name
            rapidxml::xml_node<>* stationNode = trainNode->first_node("station");

            for (rapidxml::xml_node<>* stationNode = trainNode->first_node("station"); stationNode; stationNode = stationNode->next_sibling("station")) {
                
                Station to_insert;

                //these are strings
                to_insert.name = stationNode->first_node("name")->value();
                to_insert.departure_time = stationNode->first_node("departure")->value();
                to_insert.arrival_time = stationNode->first_node("arrival")->value();

                //these are strings, but they have to be integers
                string arrival_late_str = stationNode->first_node("arrival_late")->value();
                to_insert.arrival_late = StringInt(arrival_late_str);     //TO DO

                string arrival_early_str = stationNode->first_node("arrival_early")->value();
                to_insert.arrival_early = StringInt(arrival_early_str);    //TO DO
                
                string departure_late = stationNode->first_node("departure_late")->value();
                to_insert.departure_late = StringInt(departure_late);   //TO DO
                
                //add train to Train
                to_add.stations.push_back(to_insert);

                bool exists = false;
                for(int i = 0 ; i < Station_map.size(); i++)
                    if(Station_map[i] == to_insert.name)
                        exists = true;
                if(!exists)
                    Station_map.push_back(to_insert.name);
            }   
        }
        Trains.push_back(to_add);
        trainNode = trainNode->next_sibling("train");
    }
    return 0;
}

//DEBUG FUNCTION TO CHECK IF THE XML FILE WAS PARSED CORRECTELY 
void Print_structs(){

    //print information from my structs

    for (auto train : Trains) {
        cout << "Train ID: " << train.ID << "\n";

        vector<Station> st_cpy = train.stations;

        for (auto st : st_cpy) {
            cout << "   Station Name: " << st.name << '\n';
            cout << "   Arrival Time: " << st.arrival_time << '\n';
            cout << "   Departure Time: " << st.departure_time << '\n';
            cout << "   Arrival Early: " << st.arrival_early << '\n';
            cout << "   Arrival Late: " << st.arrival_late << '\n';
            cout << "   Departure Late: " << st.departure_late << "\n\n";
            //cout << "---------------------\n";
        }
        cout << "\n---------------------\n\n";
    }
}

//PRINT ALL THE AVAILABLE TRAINS AND STATIONS FOR THE CLIENT TO MAKE REQUESTS
void Show_trains_and_stations(){

    cout<<"TRAINS:\n";
    for(int i = 0 ; i < Trains.size(); i++)
    {   
        cout <<" - "<<Trains[i].ID << "\n";
    }
    cout<<"\n";

    cout<<"STATIONS:\n";
    sort(Station_map.begin(), Station_map.end());
    for(int i = 0 ; i < Station_map.size(); i++)
        cout << " - " << Station_map[i] << "\n";
    cout<<"\n";
}

//FN TO ADD OR SUBSTRACT A TIME INPUT(IN MINUTES(int)) OVER A TIME IN FORMAT HH:MM(string type)
string AddSubTime(string time, int time_to_add)  
{
    //time has format HH:MM (no other formats allowed), time_to_add is minutes ONLY
    bool next_day = false;

    int hours = 10*(time[0] - '0') + (time[1] - '0');
    int minutes = 10*(time[3] - '0') + (time[4] - '0');

    if(time_to_add  >= 0)
    {
        int hours_to_add = (int)(time_to_add / 60);
        int minutes_to_add = (time_to_add % 60);

        if(minutes + minutes_to_add >= 60)
            hours_to_add++;
        
        if(hours + hours_to_add >= 24)
            next_day = true;
        
        int final_hours = (hours + hours_to_add)%24;
        int final_minutes = (minutes + minutes_to_add)%60;

        char ret_c[100];
        ret_c[0]= char (final_hours / 10 % 10 + '0');
        ret_c[1]= char (final_hours % 10 + '0');
        ret_c[2]=':';
        ret_c[3]= char(final_minutes / 10 % 10 + '0');
        ret_c[4]= char(final_minutes % 10 + '0');

        if(next_day)
        {
            ret_c[5]='(';
            ret_c[6]='+';
            ret_c[7]='1';
            ret_c[8]='z';
            ret_c[9]='i';
            ret_c[10]=')';
            ret_c[11]='\0';
        }
        else
        {
            ret_c[5]='\0';
        }

        string ret(ret_c);
        return ret;
    }
    else
    {
        int hours_to_sub = (int)(-time_to_add / 60); // use -time_to_add for subtraction
        int minutes_to_sub = (-time_to_add % 60);   // use -time_to_add for subtraction

        // Adjust hours and minutes for subtraction
        if (minutes - minutes_to_sub <= 0)
        {
            hours_to_sub++;
            minutes += 60;
        }

        if (hours - hours_to_sub <= 0)
        {
            hours += 24;
            //next_day = true;
        }

        int final_hours = (hours - hours_to_sub) % 24;
        int final_minutes = (minutes - minutes_to_sub) % 60;

        char ret_c[100];
        ret_c[0] = char(final_hours / 10 % 10 + '0');
        ret_c[1] = char(final_hours % 10 + '0');
        ret_c[2] = ':';
        ret_c[3] = char(final_minutes / 10 % 10 + '0');
        ret_c[4] = char(final_minutes % 10 + '0');

        // if (next_day)
        // {
        //     ret_c[5] = '(';
        //     ret_c[6] = '-';
        //     ret_c[7] = '1';
        //     ret_c[8] = 'd';
        //     ret_c[9] = ')';
        //     ret_c[10] = '\0';
        // }
        // else
        {
            ret_c[5] = '\0';
        }

        string ret(ret_c);
        return ret;
    }
    

}

//FN TO MODIFY THE Trains DATA STRUCTURE AT A CLIENT'S REQUEST
void modifyDepartureLate(string TrainId, string StationID, int time){

    //if a client request an update to a station, all the stations which the train
    //is about to get through are going to get modidifications
    bool train_found = false;
    bool station_found = false;
    for(int i = 0; i < Trains.size(); i++)
    {
        if(Trains[i].ID == TrainId) //we found the train
        {
            for(int j = 0 ; j < Trains[i].stations.size(); j++)
            {
                if(Trains[i].stations[j].name == StationID) //we found the starting station
                {   
                    station_found = true;
                    Trains[i].stations[j].departure_late = time;
                    Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, time);
                    continue;
                }
                if(station_found)
                {
                    Trains[i].stations[j].departure_late = time;
                    if(Trains[i].stations[j].departure_time != "NULL")
                        Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, time);
                    Trains[i].stations[j].arrival_time = AddSubTime(Trains[i].stations[j].arrival_time, time);
                }
            }
            break;
        }
    }
}

//FN TO MODIFY THE Trains DATA STRUCTURE AT A CLIENT'S REQUEST
void modifyArrivalLate(string TrainId, string StationID, int time){

    //if a client request an update to a station, all the stations which the train
    //is about to get through are going to get modidifications
    bool train_found = false;
    bool station_found = false;
    for(int i = 0; i < Trains.size(); i++)
    {
        if(Trains[i].ID == TrainId) //we found the train
        {
            for(int j = 0 ; j < Trains[i].stations.size(); j++)
            {
                if(Trains[i].stations[j].name == StationID) //we found the starting station
                {   
                    station_found = true;
                    Trains[i].stations[j].arrival_late = time;
                    if(Trains[i].stations[j].arrival_time != "NULL")
                        Trains[i].stations[j].arrival_time = AddSubTime(Trains[i].stations[j].arrival_time, time);
                    if(Trains[i].stations[j].departure_time != "NULL")
                        Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, time);
                    continue;
                }
                if(station_found)
                {
                    Trains[i].stations[j].arrival_late = time;
                    if(Trains[i].stations[j].arrival_time != "NULL")
                        Trains[i].stations[j].arrival_time = AddSubTime(Trains[i].stations[j].arrival_time, time);
                    if(Trains[i].stations[j].departure_time != "NULL")
                        Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, time);
                }
            }
            break;
        }
    }
}

//FN TO MODIFY THE Trains DATA STRUCTURE AT A CLIENT'S REQUEST
void modifyArrivalEarly(string TrainId, string StationID, int time){

    //if a client request an update to a station, all the stations which the train
    //is about to get through are going to get modidifications
    bool train_found = false;
    bool station_found = false;
    for(int i = 0; i < Trains.size(); i++)
    {
        if(Trains[i].ID == TrainId) //we found the train
        {
            for(int j = 0 ; j < Trains[i].stations.size(); j++)
            {
                if(Trains[i].stations[j].name == StationID) //we found the starting station
                {   
                    station_found = true;
                    Trains[i].stations[j].arrival_early = time;
                    if(Trains[i].stations[j].arrival_time != "NULL")
                        Trains[i].stations[j].arrival_time = AddSubTime(Trains[i].stations[j].arrival_time, -time);
                    if(Trains[i].stations[j].departure_time != "NULL")
                        Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, -time);
                    continue;
                }
                if(station_found)
                {
                    Trains[i].stations[j].arrival_early = time;
                    if(Trains[i].stations[j].arrival_time != "NULL")
                        Trains[i].stations[j].arrival_time = AddSubTime(Trains[i].stations[j].arrival_time, -time);
                    if(Trains[i].stations[j].departure_time != "NULL")
                        Trains[i].stations[j].departure_time = AddSubTime(Trains[i].stations[j].departure_time, -time);
                }
            }
            break;
        }
    }
}

//FN TO PRINT/RETURN A TRAIN'S SCHEDULE FOR THE NEXT 24H 
char * PrintTrainSchedule(string TrainId){

    int train_pos = -1;
    for(int i = 0 ; i < Trains.size(); i++)
    {   
        if(Trains[i].ID == TrainId)
        {
            train_pos = i;
            break;
        }
    }
    static char ans[1024];
    bzero(ans, 1024);
    strcat(ans, "\n\nTrain ");
    strcat(ans, TrainId.c_str());
    strcat(ans, " will go through the stations:\n");
    cout<<"STATIONS:\n";
    for (auto st : Trains[train_pos].stations) {
        strcat(ans,"\n   Station Name: ");
        strcat(ans,st.name.c_str());

        if(st.arrival_time != "NULL"){
            strcat(ans,"\n       Arrival Time: ");
            strcat(ans,st.arrival_time.c_str());
        }
        if(st.departure_time != "NULL"){
            strcat(ans,"\n       Departure Time: ");
            strcat(ans,st.departure_time.c_str());
        }
        if(st.arrival_early != 0){

            strcat(ans,"\n       Arrival Early: ");
            char *int_to_string = IntString(st.arrival_early);
            strcat(ans, int_to_string);

        }else if(st.arrival_late != 0){

            strcat(ans,"\n       Arrival Late: ");
            char *int_to_string = IntString(st.arrival_late);
            strcat(ans, int_to_string);

        }else if(st.departure_late != 0){

            strcat(ans,"\n       Departure Late: ");
            char *int_to_string = IntString(st.departure_late);
            strcat(ans, int_to_string);

        }
        strcat(ans,"\n");
        cout << "   Station Name: " << st.name << '\n';
        cout << "       Arrival Time: " << st.arrival_time << '\n';
        cout << "       Departure Time: " << st.departure_time << '\n';
        cout << "       Arrival Early: " << st.arrival_early << '\n';
        cout << "       Arrival Late: " << st.arrival_late << '\n';
        cout << "       Departure Late: " << st.departure_late << "\n\n";
    }
    strcat(ans,"\0");
    return ans;
}

//FN TO PRINT/RETURN A STATION'S ARRIVAL SCHEDULE FOR THE NEXT HOUR
char * PrintStationArrival(string StationId, int request_hour, int request_minutes){

    static char ans[1024];
    bzero(ans, 1024);

    strcat(ans, "\nTRAINS ARRIVING AT STATION ");
    strcat(ans, StationId.c_str());
    strcat(ans, " BETWEEN ");

    cout<<"TRAINS ARRIVING AT STATION "<<StationId<<" BETWEEN ";
    if(request_hour <= 9)
    {
        cout <<"0"<< request_hour <<":";
        strcat(ans, "0");
        strcat(ans, IntString(request_hour));
        strcat(ans, ":");
    }
    else
    {
        cout << request_hour <<":";
        strcat(ans, IntString(request_hour));
        strcat(ans, ":");
    }

    if(request_minutes <= 9)
    {
        cout << "0" <<request_minutes<< " AND ";
        strcat(ans, "0");
        strcat(ans, IntString(request_minutes));
        strcat(ans, " AND ");
    }
    else
    {
        cout<<request_minutes<< " AND ";
        strcat(ans, IntString(request_minutes));
        strcat(ans, " AND ");
    }

    if( (request_hour+1)%24 <= 9)
    {
        cout <<"0"<< (request_hour+1)%24 <<":";
        strcat(ans, "0");
        strcat(ans, IntString((request_hour+1)%24));
        strcat(ans, ":");
    }
    else
    {
        cout << (request_hour+1)%24 <<":";
        strcat(ans, IntString((request_hour+1)%24));
        strcat(ans, ":");
    }
    
    if( request_minutes <=9)
    {
        cout << "0" <<request_minutes<<"\n";
        strcat(ans, "0");
        strcat(ans, IntString(request_minutes));
        strcat(ans, "\n\n");
    }
    else
    {
        cout <<request_minutes<<"\n";
        strcat(ans, IntString(request_minutes));
        strcat(ans, "\n\n");
    }

    bool arrives = false;
    for(int i = 0 ; i < Trains.size(); i++)
    {
        for(int j = 0 ; j < Trains[i].stations.size(); j++)
        {
                if(Trains[i].stations[j].name == StationId)
                {   
                    string arrival_time = Trains[i].stations[j].arrival_time;

                    int arrival_hour = StringInt(arrival_time.substr(0,2));
                    int arrival_minutes = StringInt(arrival_time.substr(3,5));

                    int request_hour_plus1 = (request_hour + 1);// % 24;
                    //req     arr   req + 1
                    //23:00         00:00
                    //23:59         00:59
                    //18:12         19:12

                    if(arrival_hour >= request_hour && arrival_hour <= request_hour_plus1)
                    {
                        if(arrival_hour == request_hour && arrival_minutes >= request_minutes)
                        {
                            cout << "Train " << Trains[i].ID << " will arrive at station "<< StationId <<" at ";
                            strcat(ans, "Train ");strcat(ans, Trains[i].ID.c_str());
                            strcat(ans, " will arrive at station ");strcat(ans, StationId.c_str());
                            strcat(ans, " at ");

                            if(arrival_hour <= 9)
                            {
                                cout<< "0" <<arrival_hour << ":";
                                strcat(ans, "0");
                                strcat(ans, IntString(arrival_hour));
                                strcat(ans, ":");
                            }
                            else
                            {
                                cout<<arrival_hour << ":";
                                strcat(ans, IntString(arrival_hour));
                                strcat(ans, ":");
                            }
                            if(arrival_minutes <=9)
                            {
                                cout << "0" << arrival_minutes << "\n";
                                strcat(ans, "0");
                                strcat(ans, IntString(arrival_minutes));
                                strcat(ans, "\n");
                            }
                            else
                            {
                                cout << arrival_minutes << "\n";
                                strcat(ans, IntString(arrival_minutes));
                                strcat(ans, "\n");
                            }
                            arrives = true;
                            break;
                        } else if(arrival_hour == request_hour_plus1 && arrival_minutes <= request_minutes)
                        {
                            cout << "Train " << Trains[i].ID << " will arrive at station "<< StationId <<" at ";
                            strcat(ans, "Train ");strcat(ans, Trains[i].ID.c_str());
                            strcat(ans, " will arrive at station ");strcat(ans, StationId.c_str());
                            strcat(ans, " at ");
                            if(arrival_hour <= 9)
                            {
                                cout<< "0" <<arrival_hour << ":";
                                strcat(ans, "0");
                                strcat(ans, IntString(arrival_hour));
                                strcat(ans, ":");
                            }
                            else
                            {
                                cout<<arrival_hour << ":";
                                strcat(ans, IntString(arrival_hour));
                                strcat(ans, ":");
                            }
                            if(arrival_minutes <=9)
                            {
                                cout << "0" << arrival_minutes << "\n";
                                strcat(ans, "0");
                                strcat(ans, IntString(arrival_minutes));
                                strcat(ans, "\n");
                            }
                            else
                            {
                                cout << arrival_minutes << "\n";
                                strcat(ans, IntString(arrival_minutes));
                                strcat(ans, "\n");
                            }
                            arrives = true;
                            break;
                        }
                    }                    
                }
        }
    }
    if(!arrives){
        cout<<"NO TRAINS FOUND\n";
        strcpy(ans, "NO TRAINS FOUND\n\n");
    }
    cout<<"\n\n";
    return ans;
}

//FN TO PRINT A STATION'S DEPARTURE SCHEDULE FOR THE NEXT HOUR
char * PrintStationDeparture(string StationId, int request_hour, int request_minutes){

    static char ans[1024];
    bzero(ans, 1024);

    cout<<"TRAINS LEAVING THE STATION "<<StationId<<" BETWEEN ";
    strcat(ans, "\nTRAINS LEAVING THE STATION ");
    strcat(ans, StationId.c_str());
    strcat(ans, " BETWEEN ");

    if(request_hour <= 9)
    {
        cout <<"0"<< request_hour <<":";
        strcat(ans, "0");
        strcat(ans, IntString(request_hour));
        strcat(ans, ":");
    }
    else
    {
        cout << request_hour <<":";
        strcat(ans, IntString(request_hour));
        strcat(ans, ":");
    }

    if(request_minutes <= 9)
    {
        cout << "0" <<request_minutes<< " AND ";
        strcat(ans, "0");
        strcat(ans, IntString(request_minutes));
        strcat(ans, " AND ");
    }
    else
    {
        cout<<request_minutes<< " AND ";
        strcat(ans, IntString(request_minutes));
        strcat(ans, " AND ");
    }

    if( (request_hour+1)%24 <= 9)
    {
        cout <<"0"<< (request_hour+1)%24 <<":";
        strcat(ans, "0");
        strcat(ans, IntString((request_hour+1)%24));
        strcat(ans, ":");
    }
    else
    {
        cout << (request_hour+1)%24 <<":";
        strcat(ans, IntString((request_hour+1)%24));
        strcat(ans, ":");
    }
    
    if( request_minutes <=9)
    {
        cout << "0" <<request_minutes<<"\n";
        strcat(ans, "0");
        strcat(ans, IntString(request_minutes));
        strcat(ans, "\n\n");
    }
    else
    {
        cout <<request_minutes<<"\n";
        strcat(ans, IntString(request_minutes));
        strcat(ans, "\n\n");
    }

    bool depatures = false;
    for(int i = 0 ; i < Trains.size(); i++)
    {
        for(int j = 0 ; j < Trains[i].stations.size(); j++)
        {
                if(Trains[i].stations[j].name == StationId)
                {   
                    string departure_time = Trains[i].stations[j].departure_time;

                    int departure_hour = StringInt(departure_time.substr(0,2));
                    int departure_minutes = StringInt(departure_time.substr(3,5));

                    int request_hour_plus1 = (request_hour + 1);// % 24;
                    //req     arr   req + 1
                    //23:00         00:00
                    //23:59         00:59
                    //18:12         19:12

                    if(departure_hour >= request_hour && departure_hour <= request_hour_plus1)
                    {
                        if(departure_hour == request_hour && departure_minutes >= request_minutes)
                        {
                            cout << "Train " << Trains[i].ID <<  " will leave the station " << StationId <<" at ";
                            strcat(ans, "Train ");strcat(ans, Trains[i].ID.c_str());
                            strcat(ans, " will leave the station ");strcat(ans, StationId.c_str());
                            strcat(ans, " at ");

                            if(departure_hour <= 9)
                            {
                                cout<< "0" <<departure_hour << ":";
                                strcat(ans, "0");
                                strcat(ans, IntString(departure_hour));
                                strcat(ans, ":");
                            }
                            else
                            {
                                cout<<departure_hour << ":";
                                strcat(ans, IntString(departure_hour));
                                strcat(ans, ":");
                            }
                            if(departure_minutes <=9)
                            {
                                cout << "0" << departure_minutes << "\n";
                                strcat(ans, "0");
                                strcat(ans, IntString(departure_minutes));
                                strcat(ans, "\n");
                            }
                            else
                            {
                                cout << departure_minutes << "\n";
                                strcat(ans, IntString(departure_minutes));
                                strcat(ans, "\n");
                            }

                            break;

                        }else if(departure_hour == request_hour_plus1 && departure_minutes <= request_minutes)
                        {
                            cout << "Train " << Trains[i].ID <<  " will leave the station " << StationId <<" at ";
                            strcat(ans, "Train ");strcat(ans, Trains[i].ID.c_str());
                            strcat(ans, " will leave the station ");strcat(ans, StationId.c_str());
                            strcat(ans, " at ");

                            if(departure_hour <= 9)
                            {
                                cout<< "0" <<departure_hour << ":";
                                strcat(ans, "0");
                                strcat(ans, IntString(departure_hour));
                                strcat(ans, ":");
                            }
                            else
                            {
                                cout<<departure_hour << ":";
                                strcat(ans, IntString(departure_hour));
                                strcat(ans, ":");
                            }
                            if(departure_minutes <=9)
                            {
                                cout << "0" << departure_minutes << "\n";
                                strcat(ans, "0");
                                strcat(ans, IntString(departure_minutes));
                                strcat(ans, "\n");
                            }
                            else
                            {
                                cout << departure_minutes << "\n";
                                strcat(ans, IntString(departure_minutes));
                                strcat(ans, "\n");
                            }

                            depatures = true;
                            break;
                        }
                    }                    
                }
        }
    }
    if(!depatures){
        cout<<"NO TRAINS FOUND\n";
        strcpy(ans, "NO TRAINS FOUND\n\n");
    }
    cout<<"\n\n";
    return ans;
}

//CHECK IF A TrainID EXISTS IN THE Trains STRUCTURE
bool TrainExists(string TrainId){

    for(int i = 0 ; i < Trains.size(); i++)
        if(Trains[i].ID == TrainId)
            return true;
    return false;
}

//CHECK IF A StationID EXISTS IN THE Station_map vector
bool StationExists(string StationId){

    for(int i = 0 ; i < Station_map.size(); i++)
        if(Station_map[i] == StationId)
            return true;
    return false;
}

//MAIN CLASS TO DESIGN A COMMAND PATTERN
class Command {
public:
    virtual void execute(void *arg) = 0;
    virtual ~Command() {};
};

//Get a TrainID from the client and prints the Train's schedule in that day
class  TrainSchedule: public Command {
public:
    void execute(void *arg) override {

        //TRAIN SCHEDULE(24H) -> command syntax: 1 : TrainID\n
        updated_request_command  *parse = (updated_request_command *)(arg);
        cout <<"COMANDA IN CLASA TS : --"<<parse->old.command.c_str()<<"--\n";

        char trainID[1000];
        bzero(trainID, 1000);
        int i = 4;
        for(; i < parse->old.command.size(); i++)
            trainID[i-4] = parse->old.command[i];
        trainID[i] = '\0';

        if(!TrainExists(trainID)){
            strcpy(trainID, "Train doesn't exist");
        }else{
            char *res = PrintTrainSchedule(trainID);
            strcpy(trainID, res);
        }

        if (write(parse->old.client_id.cl, &trainID, strlen(trainID)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }
    }
};

//Get a StationID from the client and prints all the Train which will arrive in the next hour
class  StatusArrival: public Command {
public:
    void execute(void *arg) override {

        //STATUS ARRIVAL(1H) -> command syntax: ARRIVAL STATUS : StationID\n
        updated_request_command  *parse = (updated_request_command *)(arg);
        cout <<"COMANDA IN CLASA SA : --"<<parse->old.command.c_str()<<"--\n";

        char stationID[1000];
        bzero(stationID, 1000);
        int i = 4;
        for(; i < parse->old.command.size(); i++)
            stationID[i-4] = parse->old.command[i];
        stationID[i] = '\0';

        int hour;
        int minute ;
        struct tm *localTime = parse->old.when;
        if (localTime != NULL) {
            //int year = localTime->tm_year + 1900;
            //int month = localTime->tm_mon + 1;  // Months are 0-based
            //int day = localTime->tm_mday;
            hour = localTime->tm_hour;
            minute = localTime->tm_min;
            //int second = localTime->tm_sec;

            // Print the components
            printf("Current Date and Time: %02d:%02d\n", hour, minute);
        }
        if(!StationExists(stationID)){
            strcpy(stationID, "Station doesn't exist");
        }else{
            char *res = PrintStationArrival(stationID, hour, minute);
            strcpy(stationID, res);
        }


        if (write(parse->old.client_id.cl, &stationID, strlen(stationID)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }
    }
};

//Get a StationID from the client and prints all the Train which will depart in the next hour
class  StatusDeparture: public Command {
public:
    void execute(void *arg) override {

        //STATUS DEPARTURE(1H) -> command syntax: DEPARTURE STATUS : StationID\n
        updated_request_command  *parse = (updated_request_command *)(arg);
        cout <<"COMANDA IN CLASA SD : --"<<parse->old.command.c_str()<<"--\n";

        char stationID[1000];
        bzero(stationID, 1000);
        int i = 4;
        for(; i < parse->old.command.size(); i++)
            stationID[i-4] = parse->old.command[i];
        stationID[i] = '\0';

        int hour;
        int minute ;
        struct tm *localTime = parse->old.when;
        if (localTime != NULL) {
            //int year = localTime->tm_year + 1900;
            //int month = localTime->tm_mon + 1;  // Months are 0-based
            //int day = localTime->tm_mday;
            hour = localTime->tm_hour;
            minute = localTime->tm_min;
            //int second = localTime->tm_sec;

            // Print the components
            printf("Current Date and Time: %02d:%02d\n", hour, minute);
        }
        if(!StationExists(stationID)){
            strcpy(stationID, "Station doesn't exist");
        }else{
            char *res = PrintStationDeparture(stationID, hour, minute);
            strcpy(stationID, res);
        }
        
        if (write(parse->old.client_id.cl, &stationID, strlen(stationID)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }
    }
};

//Get a TrainID, StationID, and Time in minuts and updates the server data with a arrival late delay
class ArrivalLate: public Command{
public:
    void execute(void * arg) override{

        //ARRIVAL LATE -> command syntax: ARRIVAL LATE : TrainID : StationID : TIME\n
        updated_request_command  *parse = static_cast<updated_request_command *>(arg);
        cout <<"COMANDA IN CLASA AL : --"<<parse->old.command.c_str()<<"--\n";

        bool train_err = false;
        char trainID[100];
        bzero(trainID, 100);
        int i = 4;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            trainID[i-4] = parse->old.command[i];
        trainID[i] = '\0';  //i is on 'here': -> shift i with 3 pos

        //CHECK IF TRAIN_ID EXISTS
        //todo: verify stuctures
        train_err = (strlen(trainID) == 0);
        train_err = !TrainExists(trainID);

        bool station_err = false;
        char stationID[100];
        bzero(stationID, 100);
        i += 3;
        int cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            stationID[cf++] = parse->old.command[i];
        stationID[cf] = '\0'; //i is on 'here': -> shift i with 3 pos

        //CHECK IF STATION_ID EXISTS 
        //todo: verify stuctures
        station_err = (strlen(stationID) == 0);
        station_err = !StationExists(stationID);

        bool time_err = false;
        char time_delay[100];
        bzero(time_delay, 100);
        i += 3;
        cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            time_delay[cf++] = parse->old.command[i];
        time_delay[cf] = '\0';

        //CHECK IF TIME IS FOLLOWING THE SYNTAX
        time_err = (strlen(time_delay) == 0);

        //ALL SYNTAX TESTS PASSED -> SERVER SENDS ANSWER TO CLIENT
        char send[1000]; //THIS WILL GET TO THE CLIENT
        bzero(send, 1000);

        //debug
        strcat(send, trainID);strcat(send, " -> ");strcat(send, stationID);strcat(send, " -> ");strcat(send, time_delay);
        strcat(send, "\nTimetable has been updated");
        strcat(send, "\0");
        //debug

        if(train_err == true){
            bzero(send, 100);
            strcat(send, "TrainID doesn't exist or syntax wasn't followed\0");
        }else if(station_err == true){
            bzero(send, 100);
            strcat(send, "StationID doesn't exist or syntax wasn't followed\0");
        }
        else if(time_err == true){
            bzero(send, 100);
            strcat(send, "Time syntax wasn't followed\0");
        }
        else{
            int minutes = StringInt(time_delay);
            modifyArrivalLate(trainID, stationID, minutes);
        }
        

        if (write(parse->old.client_id.cl, &send, strlen(send)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }    
    }
};

//Get a TrainID, StationID, and Time in minuts and updates the server data with a arrival early delay
class ArrivalEarly: public Command{
public:
    void execute(void * arg) override{

        //ARRIVAL EARLY  -> command syntax: ARRIVAL LATE : TrainID : StationID : TIME\n
        updated_request_command  *parse = static_cast<updated_request_command *>(arg);
        cout <<"COMANDA IN CLASA AE : --"<<parse->old.command.c_str()<<"--\n";

        bool train_err = false;
        char trainID[100];
        bzero(trainID, 100);
        int i = 4;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            trainID[i-4] = parse->old.command[i];
        trainID[i] = '\0';  //i is on 'here': -> shift i with 3 pos

        //CHECK IF TRAIN_ID EXISTS
        //todo: verify stuctures
        train_err = (strlen(trainID) == 0);
        train_err = !(TrainExists(trainID));

        bool station_err = false;
        char stationID[100];
        bzero(stationID, 100);
        i += 3;
        int cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            stationID[cf++] = parse->old.command[i];
        stationID[cf] = '\0'; //i is on 'here': -> shift i with 3 pos

        //CHECK IF STATION_ID EXISTS 
        //todo: verify stuctures
        station_err = (strlen(stationID) == 0);
        station_err = !(StationExists(stationID));

        bool time_err = false;
        char time_delay[100];
        bzero(time_delay, 100);
        i += 3;
        cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            time_delay[cf++] = parse->old.command[i];
        time_delay[cf] = '\0';

        //CHECK IF TIME IS FOLLOWING THE SYNTAX
        time_err = (strlen(time_delay) == 0);

        //ALL SYNTAX TESTS PASSED -> SERVER SENDS ANSWER TO CLIENT
        char send[100]; //THIS WILL GET TO THE CLIENT
        bzero(send, 100);

        //debug
        strcat(send, trainID);strcat(send, " -> ");strcat(send, stationID);strcat(send, " -> ");strcat(send, time_delay);
        strcat(send, "\nTimetable has been updated");
        strcat(send, "\0");
        //debug

        if(train_err == true){
            bzero(send, 100);
            strcat(send, "TrainID doesn't exist or syntax wasn't followed\0");
        }else if(station_err == true){
            bzero(send, 100);
            strcat(send, "StationID doesn't exist or syntax wasn't followed\0");
        }
        else if(time_err == true){
            bzero(send, 100);
            strcat(send, "Time syntax wasn't followed\0");
        }else{
            int minutes = StringInt(time_delay);
            modifyArrivalEarly(trainID, stationID, minutes);
        }

        if (write(parse->old.client_id.cl, &send, strlen(send)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }    
    }
};

//Get a TrainID, StationID, and Time in minuts and updates the server data with a departure late delay
class DepartureLate: public Command{
public:
    void execute(void * arg) override{

        //DEPARTURE LATE  -> command syntax: DEPARTURE LATE : TrainID : StationID : TIME\n
        updated_request_command  *parse = static_cast<updated_request_command *>(arg);
        cout <<"COMANDA IN CLASA DL : --"<<parse->old.command.c_str()<<"--\n";

        bool train_err = false;
        char trainID[100];
        bzero(trainID, 100);
        int i = 4;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            trainID[i-4] = parse->old.command[i];
        trainID[i] = '\0';  //i is on 'here': -> shift i with 3 pos

        //CHECK IF TRAIN_ID EXISTS
        //todo: verify stuctures
        train_err = (strlen(trainID) == 0);
        train_err = !(TrainExists(trainID));

        bool station_err = false;
        char stationID[100];
        bzero(stationID, 100);
        i += 3;
        int cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            stationID[cf++] = parse->old.command[i];
        stationID[cf] = '\0'; //i is on 'here': -> shift i with 3 pos

        //CHECK IF STATION_ID EXISTS 
        //todo: verify stuctures
        station_err = (strlen(stationID) == 0);
        station_err = !(StationExists(stationID));

        bool time_err = false;
        char time_delay[100];
        bzero(time_delay, 100);
        i += 3;
        cf = 0;
        for(; i < parse->old.command.size() && parse->old.command[i+1] != ':'; i++)
            time_delay[cf++] = parse->old.command[i];
        time_delay[cf] = '\0';

        //CHECK IF TIME IS FOLLOWING THE SYNTAX
        time_err = (strlen(time_delay) == 0);

        //ALL SYNTAX TESTS PASSED -> SERVER SENDS ANSWER TO CLIENT
        char send[100]; //THIS WILL GET TO THE CLIENT
        bzero(send, 100);

        //debug
        strcat(send, trainID);strcat(send, " -> ");strcat(send, stationID);strcat(send, " -> ");strcat(send, time_delay);
        strcat(send, "\nTimetable has been updated");
        strcat(send, "\0");
        //debug

        if(train_err == true){
            bzero(send, 100);
            strcat(send, "TrainID doesn't exist or syntax wasn't followed\0");
        }else if(station_err == true){
            bzero(send, 100);
            strcat(send, "StationID doesn't exist or syntax wasn't followed\0");
        }
        else if(time_err == true){
            bzero(send, 100);
            strcat(send, "Time syntax wasn't followed\0");
        }else{
            int minutes = StringInt(time_delay);
            modifyDepartureLate(trainID, stationID, minutes);
        }

        if (write(parse->old.client_id.cl, &send, strlen(send)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }    
    }
};


int main()
{   
    GetDataFromXML("timetable.xml");
    //modifyDepartureLate("IR1662", "Gara Mare Iasi", 341);

    struct sockaddr_in server; // structura folosita de server
    struct sockaddr_in from;
    int nr; // mesajul primit de trimis la client
    int sd; // descriptorul de socket
    int pid;
    pthread_t th[100]; // Identificatorii thread-urilor care se vor crea
    int i = 0;

    /* crearea unui socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server]Eroare la socket().\n");
        return errno;
    }
    /* utilizarea optiunii SO_REUSEADDR */
    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* pregatirea structurilor de date */
    bzero(&server, sizeof(server));
    bzero(&from, sizeof(from));

    /* umplem structura folosita de server */
    /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;
    /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    /* utilizam un port utilizator */
    server.sin_port = htons(PORT);

    /* atasam socketul */
    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server]Eroare la bind().\n");
        return errno;
    }

    /* punem serverul sa asculte daca vin clienti sa se conecteze */
    if (listen(sd, 2) == -1)
    {
        perror("[server]Eroare la listen().\n");
        return errno;
    }
    /* servim in mod concurent clientii...folosind thread-uri */

    pthread_t main;
    pthread_create(&main, NULL, &treat_queue, &main);

    while (1)
    {
        int client;
        thData *td; // parametru functia executata de thread
        int length = sizeof(struct sockaddr_in);

        printf("[server]Asteptam la portul %d...\n", PORT);
        fflush(stdout);

        // client= malloc(sizeof(int));
        /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
        if ((client = accept(sd, (struct sockaddr *)&from, (socklen_t *)&length)) < 0)
        {
            perror("[server]Eroare la accept().\n");
            continue;
        }

        /* s-a realizat conexiunea, se astepta mesajul */

        // int idThread; //id-ul threadului
        // int cl; //descriptorul intors de accept

        td = (struct thData *)malloc(sizeof(struct thData));
        td->idThread = i++;
        td->cl = client;

        pthread_create(&th[i], NULL, &treat, td);

    } // while
};

static void *treat_separate_command(void * arg){

    updated_request_command  *parse = (updated_request_command *)(arg);

    string ans = parse->command_name;
    char buffer[100]="";
    strcpy(buffer, ans.c_str());
    //std::cout<<"vreau sa transmit:"<<buffer<<"\n";

    if(parse->command_name == "TRAIN SCHEDULE")
    {
        Command *p = new TrainSchedule();
        p->execute(parse);
        delete p;
    }
    else if(parse->command_name == "STATUS ARRIVAL"){

        Command *p = new StatusArrival();
        p->execute(parse);
        delete p;
    }
    else if(parse->command_name == "STATUS DEPARTURE"){

        Command *p = new StatusDeparture();
        p->execute(parse);
        delete p;
    }
    else{

        if (write(parse->old.client_id.cl, &buffer, strlen(buffer)+1) <= 0){
            printf("[Thread %d] ", parse->old.client_id.idThread);
            perror("[Thread]Eroare la write() catre client.\n");
        }
        else{
            printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", parse->old.client_id.idThread);
        }
    }
    return NULL;
}

static void *treat(void *arg)
{
    struct thData tdL;
    tdL = *((struct thData *)arg);
    printf("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
    fflush(stdout);
    pthread_detach(pthread_self());
    raspunde((struct thData *)arg);
    /* am terminat cu acest client, inchidem conexiunea */
    close((intptr_t)arg);
    return (NULL);
};

static void *treat_queue(void *arg){

    while(true)
    {
        vector<updated_request_command> to_update_xml;  //comenzi ce modifica structura la XML (departure_late, arrival_late, arrival_early)
                                                        //acest struct contine elementele din Command_queue dar si numele comenzii ce tb aplicata
        vector<updated_request_command> to_parse_xml;   //comenzi ce nu modifica structura la XML(train_schedule, arrival_status, departure_status)
                                                        //acest struct contine elementele din Command_queue dar si numele comenzii ce tb aplicata
             
        while(Command_queue.size() > 0)
        {

            request_command to_parse = Command_queue.front();
            //int nr = to_parse.command + 1;
            //string 
            printf("[Thread %d]Trimitem mesajul inapoi...%s\n", to_parse.client_id.idThread, to_parse.command.c_str());

            if(to_parse.command[0] >= '4' && to_parse.command[0] <= '6' && to_parse.command[1] == ' '){
                updated_request_command to_add;
                to_add.old = to_parse;

                switch (to_parse.command[0])
                {
                case '4':
                    to_add.command_name = "ARRIVAL LATE";
                    break;
                case '5':
                    to_add.command_name = "ARRIVAL EARLY";
                    break;
                case '6':
                    to_add.command_name = "DEPARTURE LATE";
                    break;
                default:
                    break;
                }

                to_update_xml.push_back(to_add);
            }
            else if (to_parse.command[0] >= '1' && to_parse.command[0] <= '3' && to_parse.command[1] == ' '){
                updated_request_command to_mod;
                
                to_mod.old = to_parse;
                switch (to_parse.command[0])
                {
                case '1':
                    to_mod.command_name = "TRAIN SCHEDULE";
                    break;
                case '2':
                     to_mod.command_name = "STATUS ARRIVAL";
                    break;
                case '3':
                     to_mod.command_name = "STATUS DEPARTURE";
                    break;
                default:
                    break;
                }

                to_parse_xml.push_back(to_mod);
            }
            else
            {
                char buffer[100]="Wrong command";
                if (write(to_parse.client_id.cl, &buffer, strlen(buffer)+1) <= 0)
                {
                    printf("[Thread %d] ", to_parse.client_id.idThread);
                    perror("[Thread]Eroare la write() catre client.\n");
                }
                else
                    printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", to_parse.client_id.idThread);
            }
            Command_queue.pop_back();
        }

        for(int i = 0 ; i < to_update_xml.size();i++)
        {   
            string ans = to_update_xml[i].command_name;
            char buffer[100]="Wrong command";
            //strcpy(buffer, ans.c_str());

            if(ans == "ARRIVAL LATE"){

                Command *p = new ArrivalLate();
                p->execute(static_cast<void*>(&to_update_xml[i]));
                delete p;

            }else if(ans == "ARRIVAL EARLY"){

                Command *p = new ArrivalEarly();
                p->execute(static_cast<void*>(&to_update_xml[i]));
                delete p;
            }
            else if(ans == "DEPARTURE LATE"){

                Command *p = new DepartureLate();
                p->execute(static_cast<void*>(&to_update_xml[i]));
                delete p;
            }
            else if (write(to_update_xml[i].old.client_id.cl, &buffer, strlen(buffer)+1) <= 0)
            {
                printf("[Thread %d] ", to_update_xml[i].old.client_id.idThread);
                perror("[Thread]Eroare la write() catre client.\n");
            }
            else
                printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", to_update_xml[i].old.client_id.idThread);
        }

        pthread_t command_threads[100];
        for( int i = 0; i < to_parse_xml.size(); i++){

            //adaug in memorie (updated_request_command) pt a fi trimis in parametri pt functia de *treat*
            updated_request_command *tmp = new updated_request_command;
            tmp = &to_parse_xml[i];

            pthread_create(&command_threads[i], NULL, &treat_separate_command, tmp);
        }
        //join pentru thread-uri
        for(int j = 0 ; j < to_parse_xml.size() ; j++){
            pthread_join(command_threads[j], NULL);
        }

    }
}

void raspunde(void *arg)
{
    int nr, i = 0;
    char buffer[50];
    bzero(buffer,50);
    struct thData tdL;
    tdL = *((struct thData *)arg);
    while (true)
    {
        if (read(tdL.cl, buffer, sizeof(buffer)) <= 0)
        {
            printf("[Thread %d]\n", tdL.idThread);
            perror("Eroare la read() de la client.\n");
        }

        printf("[Thread %d]Mesajul a fost receptionat...%s\n", tdL.idThread, buffer);

        string cpy(buffer);
        
        printf("[Thread %d]Mesajul a fost receptionat in varianta string...%s\n", tdL.idThread, cpy.c_str());


        if(strcmp(buffer, "exit")== 0){
            
            char nr[100]="DISCONNECTED";
            if (write(tdL.cl, &nr, strlen(nr)+1) <= 0)
            {
                printf("[Thread %d] ", tdL.idThread);
                perror("[Thread]Eroare la write() catre client.\n");
            }
            else
                printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", tdL.idThread);

            close(tdL.cl);
            break;

        }else{

            request_command to_add;
            to_add.client_id = tdL;
            to_add.command = cpy;

            time_t time_seed = time(0);
            struct tm *get_actual_time = localtime(&time_seed);
            to_add.when = get_actual_time;
            Command_queue.push_back(to_add);
        }
        

        /*pregatim mesajul de raspuns */
        //nr++;
        // printf("[Thread %d]Trimitem mesajul inapoi...%d\n", tdL.idThread, nr);

        // /* returnam mesajul clientului */
        // if (write(tdL.cl, &nr, sizeof(int)) <= 0)
        // {
        //     printf("[Thread %d] ", tdL.idThread);
        //     perror("[Thread]Eroare la write() catre client.\n");
        // }
        // else
        //     printf("[Thread %d]Mesajul a fost trasmis cu succes.\n", tdL.idThread);
    }
}
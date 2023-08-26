#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <iostream>
#include <vector>
#include <utility>
#include <random>
#include <fcntl.h>
#include <string>
#include <fstream>

#define e 100
#define c 500
#define s 20
#define MAX_QUERIES 10
#define T 60

using namespace std;

typedef struct tabEntry{
    int event;
    int queryType;
    int numOfThreads;
}tabEntry;

tabEntry* getNewEntry(int event, int qtype, int n){
    tabEntry* t = (tabEntry*) malloc(sizeof(tabEntry));
    t->event = event;
    t->queryType = qtype;
    t->numOfThreads = n;
    return t;
}

tabEntry* sharedTable[MAX_QUERIES];

int event[e];
int activeQueries;

pthread_mutex_t tableMutex;
pthread_mutex_t exitMutex;
pthread_cond_t readPossible[e];
pthread_cond_t writePossible[e];
volatile bool exitNow; 
struct timespec start_time;
string pathToLogDir;

struct timespec ns_to_timespec(long long ns) {
    struct timespec ts;
    ts.tv_sec = ns / 1000000000;
    ts.tv_nsec = ns % 1000000000;
    return ts;
}

struct timespec get_current_time(){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long elapsed_ns = (ts.tv_sec - start_time.tv_sec) * 1000000000LL + (ts.tv_nsec - start_time.tv_nsec);
    return ns_to_timespec(elapsed_ns);
}

int min(int a, int b){
    return (a<b) ? a : b;
}
tabEntry* get_read_entry(int eventID){
    for(int i = 0; i < MAX_QUERIES; i++){
        tabEntry* query = sharedTable[i];
        if(query->event == eventID && query->queryType == 0)
            return query;
    }
    return NULL;
}

tabEntry* get_write_entry(int eventID){
    for(int i = 0; i < MAX_QUERIES; i++){
        tabEntry* query = sharedTable[i];
        if(query->event == eventID && query->queryType == 1)
            return query;
    }
    return NULL;
}

tabEntry* get_free_entry(){
    for(int i = 0; i < MAX_QUERIES; i++){
        tabEntry* query = sharedTable[i];
        if(query->event == -1)
            return query;
    }
    return NULL;
}

void signal_free_cond(){
    for(int i = 0; i < e; i++){
        pthread_cond_broadcast(&readPossible[i]);
        pthread_cond_broadcast(&writePossible[i]);
    }
}

void add_read_entry(int eventID, ofstream& outfile){
    pthread_mutex_lock(&tableMutex);
    struct timespec ts;
    tabEntry* p;
    int a = 0;
    while((a = activeQueries) >= MAX_QUERIES || (p = get_write_entry(eventID))){
        ts = get_current_time();
        if(a >= MAX_QUERIES){
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : SUSPENDED - MAX_QUERIES reached\n";
        }
        else{
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : SUSPENDED - write entry for event " << eventID << " found\n";
        }
        pthread_cond_wait(&readPossible[eventID], &tableMutex);
        ts = get_current_time();
        outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : RESUMING - signal received\n";
    }

    activeQueries++;
    
    p = get_read_entry(eventID);
    if(!p){
        tabEntry* freeEntry = get_free_entry();
        freeEntry->event = eventID;
        freeEntry->numOfThreads = 1;
        freeEntry->queryType = 0;
    }
    else{
        p->numOfThreads += 1;
    }
    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : read entry made\n";

    pthread_mutex_unlock(&tableMutex);
}

void remove_read_entry(int eventID, ofstream& outfile){
    pthread_mutex_lock(&tableMutex);
    tabEntry* p = get_read_entry(eventID);
    struct timespec ts;
    p->numOfThreads -= 1;
    if(p->numOfThreads == 0){
        p->event = -1;
        ts = get_current_time();
        outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Last read entry removed. waking up all the threads waiting to write event " << eventID << endl;
        pthread_cond_broadcast(&writePossible[eventID]);
    }
    else{
        ts = get_current_time();
        outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : read entry removed\n";
    }

    activeQueries -= 1;
    
    signal_free_cond();
    pthread_mutex_unlock(&tableMutex);
}

int enquire(int eventID, ofstream& outfile){
    struct timespec ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to make a read entry in shared table\n";

    add_read_entry(eventID, outfile);

    int availableSeats = event[eventID];

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : available seats for the event " << eventID << " = " << availableSeats << "\n";

    struct timespec ts2 = {0, 100000000}; 
    nanosleep(&ts2, NULL);

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to remove a read entry in shared table\n";

    remove_read_entry(eventID, outfile);

    return availableSeats;
}

void add_write_entry(int eventID, ofstream& outfile){
    struct timespec ts;
    pthread_mutex_lock(&tableMutex);
    tabEntry* p1 = NULL, *p2= NULL;
    int a = 0;
    while((a = activeQueries) >= MAX_QUERIES || (p1 = get_read_entry(eventID)) || (p2 = get_write_entry(eventID))){
        ts = get_current_time();
        if(a >= MAX_QUERIES){
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : SUSPENDED - MAX_QUERIES reached\n";
        }
        else if(p1){
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : SUSPENDED - read entry for event " << eventID << " found\n";
        }
        else{
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : SUSPENDED - write entry for event " << eventID << " found\n";
        }
        pthread_cond_wait(&writePossible[eventID], &tableMutex);
        ts = get_current_time();
        outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : RESUMING - signal received\n";
    }
    activeQueries++;

    tabEntry* p = get_free_entry();
    p->event = eventID;
    p->queryType = 1;
    p->numOfThreads = 1;

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Write entry made\n";

    pthread_mutex_unlock(&tableMutex);
}

void remove_write_entry(int eventID, ofstream& outfile){
    struct timespec ts;
    pthread_mutex_lock(&tableMutex);
    tabEntry* p = get_write_entry(eventID);
    p->event = -1;
    activeQueries--;
    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Write entry removed. waking up all the threads waiting to read/write event " << eventID << endl;
    pthread_cond_broadcast(&readPossible[eventID]);
    pthread_cond_broadcast(&writePossible[eventID]);
    signal_free_cond();
    pthread_mutex_unlock(&tableMutex);
}

int book(int eventID, int k, ofstream& outfile){
    struct timespec ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to make a write entry in shared table\n";

    add_write_entry(eventID, outfile);

    int available = event[eventID];
    int booked = min(available, k);

    struct timespec ts2 = {0, 100000000}; 
    nanosleep(&ts2, NULL);

    event[eventID] -= booked;

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : booked " << booked << " seats for the event " << eventID << "\n";

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to remove a write entry in shared table\n";

    remove_write_entry(eventID, outfile);

    return booked;
}

void cancel(int eventID, int k, ofstream& outfile){
    struct timespec ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to make a write entry in shared table\n";
    
    add_write_entry(eventID, outfile);

    struct timespec ts2 = {0, 100000000}; 
    nanosleep(&ts2, NULL);

    event[eventID] += k;
    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : cancelled " << k << " seats for the event " << eventID << "\n";

    ts = get_current_time();
    outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to remove a write entry in shared table\n";

    remove_write_entry(eventID, outfile);
}



void* worker(void* arg){
    int threadID = *((int*) arg);
    string filename = pathToLogDir + "/file_" + to_string(threadID) + ".txt";
    struct timespec ts;
    
    ofstream outfile;
    outfile.open(filename.c_str(), ios::out | ios::trunc );
    vector<pair<int, int> > bookings;
    std::random_device rd;
    std::mt19937 gen(rd());

    uniform_int_distribution<> queryDis(1, 3);
    uniform_int_distribution<> seatDis(5, 10);
    uniform_int_distribution<> eventDis(0, e-1);

    while(true){
        pthread_mutex_lock(&exitMutex);
        if(exitNow){
            pthread_mutex_unlock(&exitMutex);
            break;
        }
        pthread_mutex_unlock(&exitMutex);

        int q = queryDis(gen);
        if(q == 1){
            int eventToQuery = eventDis(gen);
            ts = get_current_time();
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to enquire event " << eventToQuery << endl;
            int available = enquire(eventToQuery, outfile);
        }
        else if(q == 2){
            int eventToQuery = eventDis(gen);
            int k = seatDis(gen);
            ts = get_current_time();
            outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : "<< "Attempting to book " << k << " tickets for the event " << eventToQuery << endl;
            int booked = book(eventToQuery, k, outfile);
            bookings.push_back(make_pair(eventToQuery, booked));
        }
        else{
            uniform_int_distribution<> dis(0, bookings.size()-1);
            int index = dis(gen);
            
            if(index < bookings.size()){
                int eventToQuery = bookings[index].first;
                int k = bookings[index].second;
                bookings.erase(bookings.begin() + index);
                ts = get_current_time();
                outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : " << "Attempting to cancel " << k << " tickets for the event " << eventToQuery << endl;
                cancel(eventToQuery, k, outfile);
            }
            else{
                ts = get_current_time();
                outfile << ts.tv_sec << " sec, " << ts.tv_nsec << " ns : Attempting to cancel - no bookings made yet\n";
            }
        }
        outfile << "\n";
        struct timespec ts = {0, 100000000000}; 
        nanosleep(&ts, NULL);
    }
    outfile << "\n\nThread-" << threadID << " : Exiting....\n" << endl;
    outfile.close();
    return NULL;
}


int main(int argc, char* argv[]){
    if(argc < 2){
        fprintf(stderr, "provide path to directory of the log files\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&tableMutex, NULL);
    //pthread_cond_init(&queryFreed, NULL);
    int i = 0;
    for(i = 0; i < e; i++){
        event[i] = c;
        pthread_cond_init(&readPossible[i], NULL);
        pthread_cond_init(&writePossible[i], NULL);
    }

    for(i = 0; i < MAX_QUERIES; i++)
        sharedTable[i] = getNewEntry(-1, -1, -1);

    exitNow = false;

    pthread_t threads[s];
    int threadids[s];

    char* cmd = (char*) malloc(strlen(argv[1])+4);
    snprintf(cmd, sizeof(cmd), "rm -f %s/*", argv[1]);
    system(cmd);
    free(cmd);
    pathToLogDir = string(argv[1]);
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for(int i = 0; i < s; i++){
        threadids[i] = i+1;
        pthread_create(&threads[i], NULL, worker, &threadids[i]);
    }

    struct timespec ts = {T, 0}; 
    nanosleep(&ts, NULL);

    pthread_mutex_lock(&exitMutex);
    exitNow = true;
    pthread_mutex_unlock(&exitMutex);
    
    for(int i = 0; i < s; i++){
        pthread_join(threads[i], NULL);
    }

    cout << "   Registration Status\n\n";
    for(int i = 0; i < e; i++){
        printf("%5d  ---->  %5d\n", i+1, event[i]);
    }
    exit(0);
}
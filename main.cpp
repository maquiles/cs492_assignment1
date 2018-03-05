/*
** Matthew Aquiles, Thomas Falsone
** 3/5/2018
** 
** Compiled with C++ 2011
*/

#include <iostream>
#include <stdio.h>
#include <pthread.h> 
#include <sys/time.h>
#include <list>
#include <random>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <climits>

using namespace std;

//Product to be added to the queue
struct Product {
    int product_id; //product id
    clock_t ts;     //timestamp
    int life;       //randomly generated "life" of product

    Product(int id, unsigned int s, uint64_t t, int l) : product_id(id), ts(t), life(l) {}
    ~Product(){}
};

//queue class using c++ standard linked list
class Queue {
    size_t size; //max size
    list<Product*> queue;
public:
    Queue(size_t s) : size(s) {}
    Queue() {}
    ~Queue() {}

    Product* pop() {
        Product* out = nullptr;
        if(!queue.empty()){
            out = queue.front();
            queue.pop_front();
        }
        return out;
    }

    void push(Product *p) {
        if(queue.size() != size || size == 0){
            queue.push_back(p);
        }
    } 

    bool empty(){
        return queue.empty();
    }

    bool full(){
        return (queue.size() == size && size != 0);
    }
};

int consumed = 0;           //number of products that have been comsumed
int id = 0;                 //ids of products, counts amount created
int seed;
int product_num;            //number of products to be generated
int quantum;
Queue* q;
timeval tv;                 //calculated the time values for performace analysis
uint64_t min_t = INT_MAX;   //min turn around
uint64_t max_t = 0;         //max turn around
uint64_t av_t;              //average time
uint64_t min_w = INT_MAX;   //min wait time
uint64_t max_w = 0;         //max wait time
uint64_t av_w;              //average wait time
uint64_t curr_time;         //current time

pthread_mutex_t q_mutex;    //mutex to control which process is active
pthread_cond_t notFull;     //condition to prevent accessing full q
pthread_cond_t notEmpty;    //condition to prevent accessing empty q

//function to retrun the 10th fibonacci number
void fn(int n){
    int a = 1;
    int b = 0;
    int c;
    while (n > 0){
        c = a;
        a = a+b;
        b=c;
        n--;
    }
}

//producer to create products to put into the queue
void *producer(void* i){
    int a = *((int *)i); //producer id
    while (true){
        pthread_mutex_lock(&q_mutex);

        //wait while the q is full
        while (id < product_num && q->full()){
            pthread_cond_wait(&notFull, &q_mutex);
        }

        if(id < product_num){
            //contruct the product
            gettimeofday(&tv, nullptr);
            uint64_t t = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec; //get the current timestamp
            Product* p = new Product(id, seed, t, rand()%1024);
            id++;
            q->push(p);

            cout<< "Product "<< p->product_id<< " gener`ted by producer \n";

            pthread_mutex_unlock(&q_mutex);
            pthread_cond_signal(&notEmpty);
        }

        if(id >= product_num){
            pthread_mutex_unlock(&q_mutex);
            pthread_cond_broadcast(&notEmpty); //broadcast to any consumers waiting to end
            break;
        }
        usleep(100000);
    }
    pthread_exit(0);
}

//round robin consumer function
void *consumer_RR(void* i){
    int a = *((int *) i);
    while (true){
        pthread_mutex_lock(&q_mutex);

        //wait while the q is empty
        while (consumed < product_num && q->empty()){
            pthread_cond_wait(&notEmpty, &q_mutex);
        }

        if(consumed < product_num){
            Product* p = q->pop();
            if(p != nullptr){ //handle the product if life longer than quantum
                if(p->life >= quantum){
                    p->life -= quantum;
                    //finds waiting times and changes values
                    gettimeofday(&tv, nullptr);
                    curr_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
                    av_w += curr_time - p->ts;
                    if(curr_time - p->ts > max_w){
                        max_w = curr_time - p->ts;
                    }
                    if(curr_time - p->ts < min_w){
                        min_w = curr_time - p->ts;
                    }

                    //insert into the product based on the quantum
                    for(int i = 0; i<quantum; i++){
                        fn(10);
                    }

                    //insert to back of q for further consumption
                    q->push(p);
                }
                else { //handle the entire product
                    //update waiting time values and turn around time values for completed product
                    gettimeofday(&tv, nullptr);
                    curr_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
                    av_w += curr_time - p->ts;

                    //update the wait times
                    if(curr_time - p->ts > max_w){
                        max_w = curr_time - p->ts;
                    }

                    if(curr_time - p->ts < min_w){
                        min_w = curr_time - p->ts;
                    }

                    for (int i = 0; i<p->life; i++){
                        fn(10);
                    }
                    cout<< "Product "<< p->product_id<< " consumed by consumer \n";

                    gettimeofday(&tv, nullptr);
                    curr_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
                    av_t += curr_time - p->ts;

                    //update turn around time as product is consumed
                    if(curr_time - p->ts > max_t){
                        max_t = curr_time - p->ts;
                    }

                    if(curr_time - p->ts < min_t){
                        min_t = curr_time - p->ts;
                    }

                    consumed++;
                    delete p;
                }
            }

            //signal that the q is not full
            pthread_cond_signal(&notFull);
            pthread_mutex_unlock(&q_mutex);
            usleep(100000);
        }

        if(consumed >= product_num){
            pthread_mutex_unlock(&q_mutex);
            pthread_cond_broadcast(&notFull); //broadcast to any producers waiting to end
            break;
        }
    }
    pthread_exit(0);
}

//first come first serve consumer function
void *consumer_FCFS(void* i){
    int a = *((int *) i);
    while(true){
        //lock the q
        pthread_mutex_lock(&q_mutex);

        //wait while the q is empty
        while(consumed < product_num && q->empty()){
            pthread_cond_wait(&notEmpty, &q_mutex);
        }

        if(consumed++ < product_num){
            Product* p = q->pop();
            cout<< "Product "<< p->product_id<< " consumed by consumer "<< a<< "\n";
            //consume the product
            //but the first is calculates the waiting time for the products and changes values as needed
            gettimeofday(&tv, nullptr);
            curr_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec; //update the current time
            av_w += curr_time - p->ts;

            //update wait times
            if(curr_time - p->ts > max_w){
                max_w = curr_time - p->ts;
            }
            if(curr_time - p->ts < min_w){
                min_w = curr_time - p->ts;
            }

            //consume the product
            if(p != nullptr){
                for (int i = 0; i<p->life; i++){
                    fn(10);
                }
            }

            //update turn around time
            gettimeofday(&tv, nullptr);
            curr_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
            av_t += curr_time - p->ts;

            if(curr_time - p->ts > max_t){
                max_t = curr_time - p->ts;
            }

            if(curr_time - p->ts < min_t){
                min_t = curr_time - p->ts;
            }

            delete p;
            pthread_cond_signal(&notFull);
            pthread_mutex_unlock(&q_mutex);
            usleep(100000);
        }
        if(consumed >= product_num){
            pthread_mutex_unlock(&q_mutex);
            pthread_cond_broadcast(&notFull);
            break;
        }
    }
    pthread_exit(0);
}

int main(int argc, char *argv[]){
    string err = "Usage : ./HW1exec <producer #> <consumer #> <# of products> <queue size> <algorithm> <quantum> <seed> \nAll values must be positive";

    //initialize mutexes
    pthread_mutex_init(&q_mutex, nullptr);

    //initialize conditions
    pthread_cond_init(&notEmpty, nullptr);
    pthread_cond_init(&notFull, nullptr);

    //set global variable to arguments
    size_t queue_size;
    int producer_arg;
    int consumer_arg;
    int schedule_type;

    //ensure usage is correct
    if (argc != 8){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[7]) >> seed)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[6]) >> quantum)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[5]) >> schedule_type)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[4]) >> queue_size)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[3]) >> product_num)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[2]) >> consumer_arg)){
        cout<< err<< "\n";
        return 1;
    }

    if(!(stringstream(argv[1]) >> producer_arg)){
        cout<< err<< "\n";
        return 1;
    }

    if(producer_arg <= 0 || consumer_arg <= 0 || product_num < 0 || queue_size < 0 || quantum <= 0 || seed == 0){
        cout<< err<< "\n";
        return 1;
    }

    //build make queue
    q = new Queue(queue_size);

    srand(seed);
    int producer_ids[producer_arg];             //producer ids
    int consumer_ids[consumer_arg];             //consumer ids
    pthread_t producer_threads[producer_arg];   //producer threads
    pthread_t consumer_threads[consumer_arg];   //consumer threads
    gettimeofday(&tv, nullptr);
    uint64_t s_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;

    //generate the producer and consumer threads
    for (int i = 0; i<producer_arg; i++){
        producer_ids[i] = i;
        pthread_create(&producer_threads[i], nullptr, producer, &producer_ids[i]);
    }

    //determine algorithm to use
    void *(*choice) (void*) = (!schedule_type) ? &consumer_FCFS : &consumer_RR;

    for (int i = 0; i<consumer_arg; i++){
        consumer_ids[i] = i;
        pthread_create(&consumer_threads[i], nullptr, choice, &consumer_ids[i]);
    }

    for (int i = 0; i<producer_arg; i++){
        pthread_join(producer_threads[i], nullptr);
    }

    //join producer and consumer threads
    for (int i = 0; i < consumer_arg; i++){
        pthread_join(consumer_threads[i], nullptr);
    }

    pthread_mutex_destroy(&q_mutex);
    pthread_cond_destroy(&notFull);
    pthread_cond_destroy(&notEmpty);

    gettimeofday(&tv, nullptr);
    //calculate the proccesing time for all products
    uint64_t e_time = tv.tv_sec*(uint64_t)1000000 + tv.tv_usec; 
    e_time = e_time - s_time;
    //print out total processing time
    cout<< "Total time for processing all products : "<< e_time<< "\n";
    av_t = av_t/product_num;
    av_w = av_w/product_num;
    cout<< "Average turn-around time : "<< av_t<< "\n";
    cout<< "Min turn-around time : "<< min_t<< "\n";
    cout<< "Max turn-around time : "<< max_t<< "\n";
    cout<< "Average wait time : "<< av_w<< "\n";
    cout<< "Min wait time : "<< min_w<< "\n";
    cout<< "Max wait time : "<< max_w<< "\n";

    delete q;
    return 0;
}
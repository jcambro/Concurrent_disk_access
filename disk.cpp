// jcambro 482 project 1

#include <iostream>
#include <fstream>
#include "thread.h"
#include <list>
#include <vector>
#include "disk.h"
#include <string>
#include <cmath>  //for absolute value

using namespace std;

struct DiskEntry{
	int track;
	int disk_number;
};

struct ThreadInfo{
	int thread_number;
	char *file;
	bool alive;
	bool on_queue;
};

unsigned int max_disk_queue;
unsigned int threads_needed;

list<DiskEntry> sequence_queue;

mutex info_lock;
cv servicer_thread;
cv main_thread;

bool in_queue_already(int thread_number){
	for(list<DiskEntry>::iterator x = sequence_queue.begin(); x != sequence_queue.end(); ++x){
		if(x->disk_number == thread_number){
			return true;
		}
	}
	return false;
}

// check to see if we have active threads needing attention.
bool threads_alive(vector<ThreadInfo *> info){
	bool not_in_queue = false;
	bool alive = false;
	for(unsigned int y = 0; y < info.size(); ++y){
		if(info[y]->alive){
			if(!info[y]->on_queue){
				not_in_queue = true;
			}
			alive = true;
		}
	}
	return (not_in_queue && alive);
}

void requester_thread_func(void *a){
	ThreadInfo *t_info = (ThreadInfo *)a;
	ifstream thread_file;
	info_lock.lock();
	thread_file.open(t_info->file);
	info_lock.unlock();
	string track_number_in;

  info_lock.lock();
	while( getline(thread_file, track_number_in) ){
		DiskEntry in;
		in.disk_number = t_info->thread_number;
		info_lock.unlock();
		in.track = stoi(track_number_in);

		info_lock.lock();
		while(in_queue_already(t_info->thread_number)){
			// wait until we are done for our next opportunity.
			servicer_thread.wait(info_lock);
		}

		while(sequence_queue.size() >= max_disk_queue){
			servicer_thread.wait(info_lock);
		}

		// not on queue already and it is not max size. Free to push on list.
		sequence_queue.push_front(in);
		t_info->on_queue = true;
		print_request(in.disk_number, in.track);
    info_lock.unlock();
		main_thread.signal();
    info_lock.lock();
	}
  // When the thread dies.
	thread_file.close();
	t_info->alive = false;
	t_info->on_queue = false;
	info_lock.unlock();

  servicer_thread.broadcast();
  main_thread.signal();
}

void main_thread_func(void *a){
	// return if there are 0's in queue length or file names.
	if(threads_needed == 0 || max_disk_queue == 0){
		return;
	}

	vector<thread *> spindle;
	vector<ThreadInfo *> info_container;
	char **arguments = (char **)a;

	for(unsigned int x = 0; x < threads_needed; ++x){
		// create requester threads
		ThreadInfo *temp = new ThreadInfo;
		temp->thread_number = x;
		temp->file = arguments[x+2];            //plus 2 for the offset
		temp->alive = true;
		temp->on_queue = false;
		info_container.push_back(temp);
		thread *spawn_thread = new thread((thread_startfunc_t) requester_thread_func, (void *) temp);
		spindle.push_back(spawn_thread);
	}

	int last_track_serviced = 0;

	info_lock.lock();
	while( (sequence_queue.size() < max_disk_queue) && (sequence_queue.size() < threads_needed) ){
    main_thread.wait(info_lock);
	} //while
	info_lock.unlock();

	info_lock.lock();
	while(!sequence_queue.empty()){
		int current_diff_min = 1001; //we know the max is 999
		list<DiskEntry>::iterator queue_index;

		/*****************************
		 * Find the minimum difference
		 ****************************/
		for(list<DiskEntry>::iterator x = sequence_queue.begin(); x != sequence_queue.end(); ++x){
			int difference = abs(x->track - last_track_serviced);
			if(difference < current_diff_min){
				current_diff_min = difference;
				queue_index = x;
			}
		} //for

		print_service(queue_index->disk_number, queue_index->track);
		last_track_serviced = queue_index->track;
		sequence_queue.erase(queue_index);
		(info_container[queue_index->disk_number])->on_queue = false;

		servicer_thread.broadcast();
		// this actually checks for a deadlock.
    if( threads_alive(info_container) ){
			main_thread.wait(info_lock);
		}
	} //while
	info_lock.unlock();

	// free all of our memory.
	for(unsigned int x = 0; x < spindle.size(); ++x){
		delete info_container[x];
		delete spindle[x];
	}
} //main thread

int main(int argc, char** argv){
	// read in command line args
	// command max_disk_queue input_file_1 input_file_2 ...
	max_disk_queue = atoi(argv[1]);
	threads_needed = argc - 2;

	// create main operation thread
	cpu::boot((thread_startfunc_t) main_thread_func, (void *) argv, 0);
}

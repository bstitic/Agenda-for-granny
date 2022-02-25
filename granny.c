// Comments (possible optimizations) 
// Possible aspects to optmize: 
// 1) Consider using one member instead of counted1 / counted2 or a global variable protected by a mutex
// 2) Algorithm for calculating 3 real life seconds (from any last printed output, not just the outputs that 
//    are produced when granny inputs something) could be optimized / redesigned. For now, it works.
// 3) A new feature could be adding a restart feature, to start a new day, just before breakfast.

// Important assumptions: 
// 1) Assumed granny will input the hour correctly, ie minutes 0-59, hours 0-23, otherwise some control
//    conditions could be implemented. 
// 2) Asssumed a certain format for the input (':' for the time instead of '.', only word accepted is "now", 
//    replies in the form of "yes" or "no")
// 3) Assumed only 'nocturnal' activity (which is the last activity) is something before bed (like reading) + sleeping. 
//    This is important because based on this condition this last activity will be marked as done, otherwise the 
//    controls would have to be modified. Eg. 23.00 pm - 6.00 am
// 4) Since it was asked to design a schedule for granny and she has a bad memory, a schedule was predesigned
//    for her here. A new feature in the future could be making the schedule modifiable.
// 5) Important: corner cases (like going from one day to the next) were considered to detect the last activity
//    when receiving user input, to detect 10 minutes remaining for an activity that ends at midnight (limit case),
//    and to mark the last activity as done (sleeping, roll to the next day).
// 6) Important: we're assuming that the last, largest (in time) printf to the console will be when the last activity 
//    starts, meaning eg. book + sleep from 23.00-6:00 am, and that granny won't input anything after that
//    because she should be sleeping. 
//    This way, this will be the largest static point of reference in time, so that when the activity ends
//    around 6 am for instance, it will be marked as done and the program will print the correct output (please
//    see three_seconds function in the code).
// 7) Assumed activities cannot start / end at the same time (like act1: 19.00-20.00, act2: 20:00-21.00);
//    and that the start/end times are dedicated to the activity.

// Libraries ********************************************************************************************
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sched.h>

// Global struct definition *****************************************************************************
struct activity { 
	int start_hour; // range: 0-24 
	int start_minute; // range: 0-59
	int end_hour; // range: 0-24
	int end_minute; // range: 0-59
	char start_time[6]; // simply for printing to console without internal integer - str conversion 
	char end_time[6];
	char name[101]; // max 100 characters + null; name of the activity
	char state[7]; // store state, with extra position for the null char
	int counted; // 0: not counted by thread1, 1: counted by thread1
	int counted2; // 0 : not counted by thread2, 1: countd by thread2
};

// Create a global array to store all the activities, from earliest to latest 
struct activity acts[8] = {
{7,0,8,30,"07:00","08:30","Breakfast", "Undone",0,0}, 
{9,0,11,40,"09:00","11:40","Play instrument", "Undone",0,0},
{12,0,13,30,"12:00","13:30","Lunch", "Undone",0,0},
{14,0,16,15,"14:00","16:15","Knitting", "Undone",0,0},
{16,30,17,0,"16:30","17:00","Tea time", "Undone",0,0},
{17,15,19,15,"17:15","19:15","Play cards","Undone",0,0},
{19,30,21,0,"19:30","21:00","Dinner", "Undone",0,0},
{21,10,6,45,"21:10","06:45","Read book and sleep", "Undone",0,0}};

// Global variables / mutexes ****************************************************************************
// The last time each thread executed printf will be stored, with respect to the real time clock so that
// the user can perceive 3 seconds (with respect to what they're seeing on the screen, the last print). 
// Additionally, each thread will keep its own internal clock (structs) to keep as their internal 
// time trackers, since the internal clock can go faster than the one in the real world. These structs
// are not declared here but in the thread's functions.
// There will be mutexes to synchronize threads and protect global variables, thus making the program
// thread safe (below).
struct tm tm_main; // MAIN THREAD: to capture a point of reference in time (static point of reference)
time_t raw_init;   // Main thread: to capture main thread's raw time 
struct tm tm_th1;  // THREAD1: static point of reference
time_t raw_th1;
struct tm tm_th2;  // THREAD2: static point of reference 
time_t raw_th2;
struct tm tm_granny; // Granny: when waiting for granny to answer to not lock other threads
time_t raw_granny; 
pthread_mutex_t time_mutex; // to protect modifications to time related global variables
pthread_mutex_t state_mutex; // the same, but to protect state members of the struct variables
pthread_mutex_t internal_mutex; // to read the internal clock

// Create global variable to add extra hours to internal clock *******************************************
// About this part it's important to say that the speedup will increase the frequency of the internal
// clock used by the threads. The frequency might be too fast to go through all the activities, 
// for instance when using a speedup of 10 the program goes way faster and the internal clock is 
// produced correctly. However, it's not possible to check all 8 activities in one cycle which causes
// some outputs to get omitted. 
// Using a speedup factor of 3 (ie. 60 real seconds (1 minute) = 3 internal minutes) worked well. 
// Also, 'sleep' calls in the code need to consider the internal frequency.
struct tm tm_internal; // internal clock for the speedup
int speedup = 3; // speedup factor for internal clock 

// Function declarations *********************************************************************************
void three_seconds(struct tm reference_point); // for the 3 second latency between any two outputs
void capture(int index); // to capture a static point of reference
struct tm minimum_reference(void); // to look for most recent static point of reference 
void *f1(); // for thread 1 (which checks for the start  and end of an activity)
void *f2(); // for thread 2 (which checks when there are 10 mins remaining for an activity)
void *f3(); // for thread 3 (which will accelerate or not the internal clock)

// Main **************************************************************************************************

int main(void){

// This is the initial schedule, printed when initializing the program  
printf("-------------------------------------- Granny's schedule --------------------------------------\n");
printf("%s at: %s until: %s\n", acts[0].name,acts[0].start_time, acts[0].end_time);
printf("%s at: %s until: %s\n", acts[1].name,acts[1].start_time, acts[1].end_time);
printf("%s at: %s until: %s\n", acts[2].name,acts[2].start_time, acts[2].end_time);
printf("%s at: %s until: %s\n", acts[3].name,acts[3].start_time, acts[3].end_time);
printf("%s at: %s until: %s\n", acts[4].name,acts[4].start_time, acts[4].end_time);
printf("%s at: %s until: %s\n", acts[5].name,acts[5].start_time, acts[5].end_time);
printf("%s at: %s until: %s\n", acts[6].name,acts[6].start_time, acts[6].end_time);
printf("%s at: %s until: %s\n", acts[7].name,acts[7].start_time, acts[7].end_time);
printf("-----------------------------------------------------------------------------------------------\n");

// Variable declarations for the main thread
int input_hour; // for user input
int input_minute; // for user input
int i; // for the for loop
int j; // to save the value of the activity to print out on the console
bool flag = false; // flag used when searching for the right activity
char answer[4]; // to capture answer from the user (yes/no)?
char hour_user[6]; // to capture user's input in string format ie. 08:25, 6th place for \0
char hour_in[3]; // to capture the user's hour input (substring)
char min_in[3]; // to capture the user's minute input (substring)
struct tm tm_local_ref; // to capture local time reference for the main thread
time_t local_raw; 

// Initialize the other three threads
pthread_t thread1, thread2, thread3; 
pthread_create(&thread1, NULL, f1, NULL); // for activities starting / ending
pthread_create(&thread2, NULL, f2, NULL); // for activities about to end
pthread_create(&thread3, NULL, f3, NULL); // for internal clock production

// Initialize mutexes to coordinate threads / protect global vars
pthread_mutex_init(&time_mutex, NULL);
pthread_mutex_init(&state_mutex, NULL);
pthread_mutex_init(&internal_mutex, NULL);

while(1) {
sched_yield(); // for scheduling 

// Store information from the user (Granny) (note: assume she will input the time correctly, either for eg. 8:25 or 08:25)
pthread_mutex_lock(&time_mutex);
tm_local_ref = minimum_reference(); //obtain most recent static reference
three_seconds(tm_local_ref); // calculate three seconds from most recent static reference
pthread_mutex_unlock(&time_mutex);

printf("Please input some time: \n"); 

pthread_mutex_lock(&time_mutex);
capture(3); // capture question time (granny struct tm)
pthread_mutex_unlock(&time_mutex);

scanf("%s", hour_user);

pthread_mutex_lock(&time_mutex); // put mutexes here to not lock threads
capture(0); // capture static reference time here so that granny can observe 3 seconds 
pthread_mutex_unlock(&time_mutex);

// Now extract useful information (substrings)
if( strcmp(hour_user, "now")  ) { // for strings with numbers
	if((hour_user[0] == '0') || (hour_user[0]!='0' && hour_user[1] != ':')){ // for cases like 08:25 or 15:00
		hour_in[0] = hour_user[0];
		hour_in[1] = hour_user[1];
		min_in[0] = hour_user[3];
		min_in[1] = hour_user[4];
			
	}
	else{ // if the first character is not '0', for example 8:25
		hour_in[0] = '0';
		hour_in[1] = hour_user[0];
		min_in[0] = hour_user[2];
		min_in[1] = hour_user[3];
	}
	strcpy(hour_user, "");
	hour_in[2] = '\0';
	min_in[2] = '\0';
	input_hour = atoi(hour_in); // transforming to int to work with this info internally
	input_minute = atoi(min_in);
}
else { // other possible input option: "now"
	local_raw = time(NULL);
	if (local_raw == -1){
		printf("Error calculating raw time, please restart program.\n");
	}
	tm_local_ref = *localtime(&local_raw);	
	input_hour = tm_local_ref.tm_hour;
	input_minute = tm_local_ref.tm_min;
	printf("Time captured, hour: %d and minute: %d\n", input_hour, input_minute);
}

// Now phase 1 and phase 2 of the main thread begin
// PHASE 1: Search to see if there's an activity that matches the input data
	for(i=0;i<8;i++){
		if((acts[i].start_hour == input_hour) && (acts[i].end_hour == input_hour) ){ // in the same hour
			// Check minutes for next decision
			if((input_minute >= acts[i].start_minute) && (input_minute <= acts[i].end_minute)){
				j = i; // save the right activity
				flag = true;
			}
		}
		else if((acts[i].start_hour <= input_hour) && (input_hour <= acts[i].end_hour)){ // some range, different hours
			// check subcases now
			if( ( (input_minute >= acts[i].start_minute) && (acts[i].start_hour == input_hour)) || ( (input_minute <= acts[i].end_minute) && (input_hour == acts[i].end_hour) )) { // It's in the range of the activity (limit hours) 
				j = i; 
				flag = true;
			}
			if((acts[i].start_hour < input_hour) && (input_hour < acts[i].end_hour)) { // within the range (not edges)
				j = i;
				flag = true;
			}
		}

		else if ((acts[i].start_hour > acts[i].end_hour)) { // for activities like sleeping where we have, for instance, 23:00 - 6:00 am
			if( (input_hour == acts[i].end_hour) && (input_minute <= acts[i].end_minute) ){
				j = i;
				flag = true;
			}

			if ( (input_hour == acts[i].start_hour) && (input_minute >= acts[i].start_minute) ) {
				j = i;
				flag = true;

			}

			if ( ((input_hour > acts[i].start_hour) || (input_hour < acts[i].end_hour) ) ) { // within the range
				j = i;
				flag = true; 
			}

		}

	}

// PHASE 2: After searching, print the right outputs and ensure latency between agenda printed outputs
	if(flag == false){ // No activity found for the input data
		pthread_mutex_lock(&time_mutex);
		tm_local_ref = minimum_reference(); 
		three_seconds(tm_local_ref);
		printf("No activity scheduled for this time.\n");
		capture(0);
		pthread_mutex_unlock(&time_mutex);
	}
	else{ // If it's true, return the flag to false to continue to the next loop
		pthread_mutex_lock(&time_mutex); 
		tm_local_ref = minimum_reference();
		three_seconds(tm_local_ref);
		printf("Activity scheduled for this time: %s, with start time: %s and end time: %s\n", acts[j].name, acts[j].start_time, acts[j].end_time);
		capture(0);
		pthread_mutex_unlock(&time_mutex);
		flag = false;

		// Check here if the activity is undone / done
		if( (strcmp(acts[j].state, "Undone")) ){ // 0 if both are identical -> "if" activates if result is '1' (activity is done)
			pthread_mutex_lock(&time_mutex);
			tm_local_ref = minimum_reference();
			three_seconds(tm_local_ref);	
			printf("Chill out! You already completed the activity: %s\n", acts[j].name);
			capture(0);
			pthread_mutex_unlock(&time_mutex);
		
		}
		else{ // so if the result is 0 (meaning the activity is undone)
			pthread_mutex_lock(&time_mutex);
			tm_local_ref = minimum_reference();
			three_seconds(tm_local_ref);
			printf("Are you doing the activity: %s (yes/no)?\n", acts[j].name);
			capture(3);
			pthread_mutex_unlock(&time_mutex);

			scanf("%s", answer); // just like before, add mutexes after the scanf

			pthread_mutex_lock(&time_mutex);
			capture(0);
			pthread_mutex_unlock(&time_mutex);

			// The following printf is just to verify it worked (can be commented out) 
			//printf("Answer received: %s\n", answer); // this is for checking only (should be removed)
		
			if((strcmp(answer, "no"))){ //0 if both identical -> "if" activates if result is '1' (answer is yes)
				pthread_mutex_lock(&state_mutex);
				sleep(1);
				strcpy(acts[j].state, "");
				strcpy(acts[j].state, "Done");
				pthread_mutex_unlock(&state_mutex);
			}

			strcpy(answer, ""); // reset string
			// This part below is also to check, it can be commented out
			//printf("State of activity: %s and contents of string answer: %s\n", acts[j].state, answer);
		}

	}

	// This part is only for checking whether inputs were received correctly or not (this can be commented out)
	// if(input_minute >= 10){
	//	printf("Value received is: %d:%d\n",input_hour,input_minute);
	// }
	// else{
	//	printf("Value received is: %d:0%d\n",input_hour,input_minute);
	//}
}
return 0;
}

// Function bodies ****************************************************************************************************
struct tm minimum_reference(void) {
// This function is just to find the latest static point of reference and later return it to the calling thread. 
// Calls to this function are protected by mutexes.
	struct tm result;
	// keep tm static references in an array 
	struct tm array_references_tm[4] = {tm_main, tm_th1, tm_th2, tm_granny};
	// convert important metrics to int for easier manipulation 
	int init_conversion    = tm_main.tm_hour*60     + tm_main.tm_min;
	int thread1_conversion = tm_th1.tm_hour*60 + tm_th1.tm_min;
	int thread2_conversion = tm_th2.tm_hour*60 + tm_th2.tm_min;
	int granny_conversion =  tm_granny.tm_hour*60 + tm_granny.tm_min;
	// keep the conversions in an array
	int array_references_int[4] = {init_conversion, thread1_conversion, thread2_conversion, granny_conversion};
	int k; // for index to go through the array above
	int temp = init_conversion; //assume this is the minimum, to start with (the latest in time)
	int result_index = 0; // preliminary condition if init is the minimum one (just assuming)
	for(k=1; k<4; k++) {
		if( array_references_int[k] > temp){ // if one of the others is bigger than the current result
			temp = array_references_int[k];
			result_index = k;
		}
	}
	result = array_references_tm[result_index];
	return result; 
}

void three_seconds(struct tm reference_point) {
// This is the function that takes the latest static point of reference (any), and calculates 3 real time seconds 
	struct tm current_struct; // keep track of local (current) time
	int time_delta = 0; // local variable to keep the difference
	time_t raw = time(NULL);
	if (raw == -1){
		printf("Error calculating raw time, please restart program.\n");
	}
	current_struct = *localtime(&raw);
	// To avoid having multiple 'ifs' for all the possible cases (from one hour to the next, separate minutes, etc), transform
	// everything to seconds. The largest possible value can be stored in an int variable
	time_delta = (current_struct.tm_hour*3600 + current_struct.tm_min*60 + current_struct.tm_sec) - (reference_point.tm_hour*3600 + reference_point.tm_min*60 + reference_point.tm_sec);
	// The printf below was a checkpoint, which can be commented out
	// printf("Time_delta: %d\n, with tm_current_struct second: %d, tm_current_struct minute: %d and reference_point second: %d\n", time_delta, current_struct.tm_sec, current_struct.tm_min, reference_point.tm_sec);

	if (time_delta < 3 && time_delta >= 0) { // includes the case in which both printfs happened at the same time 
	// If more than 3 seconds have already passed (in the real world), then output will be printed immediately
		while(time_delta < 3){
			raw = time(NULL);
			if (raw == -1){
			 	printf("Error calculating raw time, please restart program.\n");
			 	break;
			}
			current_struct = *localtime(&raw);
			time_delta = (current_struct.tm_hour*3600 + current_struct.tm_min*60 + current_struct.tm_sec) - (reference_point.tm_hour*3600 + reference_point.tm_min*60 + reference_point.tm_sec);
			// the instruction below was just to verify
			//printf("Time delta: %d\n",time_delta);
		}
	}

}

void capture(int index) { // 0: main thread, 1: thread1, 2: thread2, 3: waiting for granny 
// This function is to capture, in its respective variable, the last time a thread printed out something to the console or when
// the user inputs something so they can observe 3 seconds from when they input information
		if(index == 0) {
			raw_init = time(NULL);
			if (raw_init == -1){
			 	printf("Error calculating raw time; please restart program.\n");
			}
			tm_main = *localtime(&raw_init);
		}
		else if(index == 1) { // thread 1: the one that prints start + changes internal states
			raw_th1 = time(NULL);
			if (raw_th1 == -1){
			 	printf("Error calculating raw time; please restart program.\n");
			}
			tm_th1 = *localtime(&raw_th1);

		}
		else if (index == 2) { // thread 2: the one that watches out for the last 10 remaining minutes
			raw_th2 = time(NULL);
			if (raw_th2 == -1){
			 	printf("Error calculating raw time; please restart program.\n");
			}
			tm_th2 = *localtime(&raw_th2);
		}

		else { // 3: when waiting for granny (so other threads won't get locked)
			raw_granny = time(NULL);
			if (raw_granny == -1) {
			 	printf("Error calculating raw time; please restart program.\n");			
			}
			tm_granny = *localtime(&raw_granny);
		}

}

void *f1(){
// This thread will have its own internal clock and check the activities, to see which one will start next.
// It will also change an activity's state to 'done' if the ending has been reached (so it will check the start and finish)

// Local variables for thread 1
struct tm tm_thread1; // struct for the thread's internal clock
int j = 0; // index for the for loop below 
int counter_one = 0; // counter for the thread

	while ( counter_one < 8 ){
		for(j=0;j<8;j++){ // go through the activities
			// Check the internal time first to look for any candidates
			// Use the mutex destined for internal clock protection
			pthread_mutex_lock(&internal_mutex);
			tm_thread1.tm_sec = tm_internal.tm_sec;
			tm_thread1.tm_min = tm_internal.tm_min;
			tm_thread1.tm_hour = tm_internal.tm_hour; // this can also be changed to tm_thread1 = tm_internal
			pthread_mutex_unlock(&internal_mutex);
			sched_yield(); // important for the CPU
			
			// This part below was just a checkpoint to verify behavior (it's commented out)
			//printf("Testing data, hour: %d and minute: %d and j: %d, and counter: %d, activity state: %s, and counted: %d\n", tm_thread1.tm_hour, tm_thread1.tm_min, j, counter_one, acts[j].state, acts[j].counted);

			// Part 1: check if an activity is starting now (based on internal clock)
			if ( (acts[j].start_hour == tm_thread1.tm_hour) && (acts[j].start_minute == tm_thread1.tm_min) ){
				// We should print this out 3 seconds after the last message printed to the console		
				pthread_mutex_lock(&time_mutex);
				struct tm tm_thread1_2 = minimum_reference(); // struct used for real world clock
				three_seconds(tm_thread1_2);
				printf("Activity: %s is starting now, hour: %d and minute: %d\n", acts[j].name, tm_thread1.tm_hour, tm_thread1.tm_min);		
				capture(1); // capture the last time the thread printed out something to the console
				pthread_mutex_unlock(&time_mutex);
				sleep(20);  // sleep to only print the start once; seconds here depend on frequency
			}

						
			if ( ( (acts[j].end_hour == tm_thread1.tm_hour) && (acts[j].end_minute < tm_thread1.tm_min) ) || 
(acts[j].end_hour < tm_thread1.tm_hour) ) { // when an activity just finished or finished in some past hour
				if( ((strcmp(acts[j].state, "Done")) && (acts[j].counted == 0) && (counter_one < 7)) && acts[j].start_hour <= acts[j].end_hour ){ // all activities except the last one 
					pthread_mutex_lock(&state_mutex);
					sleep(6);					
					strcpy(acts[j].state, "");
					strcpy(acts[j].state, "Done");
					acts[j].counted = 1;
					pthread_mutex_unlock(&state_mutex);
					// The following was just used to verify behavior, it can be commented out
					//printf("State of activity: %s and name of activity: %s\n", acts[j].state, acts[j].name);				
					counter_one++;
				}
			}

			if ((strcmp(acts[j].state, "Undone")) && (acts[j].counted == 0)) { // if granny changed an activity to 'done'
				acts[j].counted = 1;
				// The part below was just a checkpoint; it can be removed
				//printf("(in else) state of activity: %s and name of activity: %s\n", acts[j].state, acts[j].name);				
				counter_one++;
			}	

			// The last activity to check for is when granny goes to sleep (critical since hours restart)
			if ( (counter_one == 7) && (acts[j].start_hour > acts[j].end_hour) && (strcmp(acts[j].state, "Done")) && (acts[j].counted == 0) && (tm_thread1.tm_hour <= acts[0].start_hour) ) { // Only to be marked as done once all other activities are ready	
				if( ( (acts[j].end_hour == tm_thread1.tm_hour) && (acts[j].end_minute < tm_thread1.tm_min) ) || (acts[j].end_hour < tm_thread1.tm_hour) ) { // now, the usual condition 				
					pthread_mutex_lock(&state_mutex);					
					strcpy(acts[j].state, "");
					strcpy(acts[j].state, "Done");
					acts[j].counted = 1;
					pthread_mutex_unlock(&state_mutex);
					// The following is just to verify (can be deleted)
					//printf("State of activity: %s and name of activity: %s\n", acts[j].state, acts[j].name);
					counter_one++;
				}	

			}
			

		}
	}
	//pthread_exit(NULL); // if all the activities are done exit
	return 0; // instead of the return 0, pthread_exit(NULL) can be used
} // thread1 function


void *f2(){
// Thread 2 will have its own internal clock too and check the activities. 
// The goal is to check when there are 10 minutes remaining.

// Local variables
struct tm tm_thread2; // struct for the thread's internal clock
int j = 0; // index for the for loop below 
int counter_two = 0; // if the activity has been changed to "done", then update the counter

	while ( counter_two < 8 ){	
		for(j=0;j<8;j++){ // check the activities
			// Just like for thread 1, check the internal clock first
			sched_yield(); // important for the CPU
			pthread_mutex_lock(&internal_mutex);
			tm_thread2.tm_sec = tm_internal.tm_sec;
			tm_thread2.tm_min = tm_internal.tm_min;
			tm_thread2.tm_hour = tm_internal.tm_hour; // again, this can be changed for tm_thread2 = tm_internal
			pthread_mutex_unlock(&internal_mutex);

			// The printf below was used only to to verify behavior (can be deleted)
			//printf("Thread2, Testing data, hour: %d and minute: %d and j: %d, and counter: %d, activity state: %s, and counted: %d\n", tm_thread2.tm_hour, tm_thread2.tm_min, j, counter_two, acts[j].state, acts[j].counted2);			

			if ( (strcmp(acts[j].state, "Done")) ){ // 1 if different, meaning activity is undone
 
				if ( ( (acts[j].end_hour == tm_thread2.tm_hour) && (acts[j].end_minute == (tm_thread2.tm_min+10)) && (acts[j].end_hour != 0) ) || 
 ( (acts[j].end_hour == (tm_thread2.tm_hour+1)) && ((acts[j].end_minute+50) == (tm_thread2.tm_min)) && (acts[j].end_hour != 0) ) || 
 ( (tm_thread2.tm_hour + 1 == acts[j].end_hour + 24 ) &&  ((acts[j].end_minute+50) == (tm_thread2.tm_min)) ) 
  ) { // (10 mins remaining, normal case) || (when passing from one hour to another) || (edge case, something ending at midnight)
					pthread_mutex_lock(&time_mutex);
					struct tm tm_thread2_2 = minimum_reference(); // to store real world reference
					three_seconds(tm_thread2_2);
					//printf("Activity: %s will be finishing in 10 minutes, internal hour: %d and minute: %d; tm copy (reference) has hour: %d and minute: %d\n", acts[j].name, tm_thread2.tm_hour, tm_thread2.tm_min, tm_thread2_2.tm_hour, tm_thread2_2.tm_min);
					printf("Activity: %s will be finishing in 10 minutes.\n", acts[j].name);
					capture(2);
					pthread_mutex_unlock(&time_mutex);
					counter_two++;
					acts[j].counted2 = 1; 
					sleep(20); // sleep to avoid printing this many times for the same activity
				}

			}

			if ( (strcmp(acts[j].state, "Undone") && acts[j].counted2 == 0) ){ // 1 if the activity is done - this is from user input
				counter_two++;
				acts[j].counted2 = 1;
			}				
	
		}	
	}
	return 0; // The pthread_exit(NULL) could be used here instead
	//pthread_exit(NULL); // if all the activities are already done, exit
}

void *f3(void) {
// This is the thread that creates the internal, accelerated clock

// Local variables 
struct tm tm_test; // to obtain reference time from the outside world (using localtime()) for the start
int start = 0; // to capture static reference only for the start 
int factor = (60/speedup); // the speedup factor for the seconds

	while (1) { // This is for the speedup
		sched_yield(); // for the CPU

		time_t local_test = time(NULL);
		if (local_test == -1){
			printf("Error calculating raw time, please restart program.\n");
		}
		tm_test = *localtime(&local_test);

		pthread_mutex_lock(&internal_mutex);
		if(start == 0) {
			tm_internal.tm_hour = tm_test.tm_hour; // capture the reference
			tm_internal.tm_min = tm_test.tm_min;
			start = 1; 
		}

		// The next formula works, since C takes the integer part when dividing; it's an algorithm that allows us to 
		// divide 60 seconds into subintervals, each going from 0 to some maximum. With this formula, the '0' second
		// is calculated automatically to restart the new second internval. This also allows accelerating time internally
		// in a continuous way (no discrete jumps)
		tm_internal.tm_sec = tm_test.tm_sec - (tm_test.tm_sec/factor) * factor; 

		if(tm_internal.tm_sec == 0 ) { // 0: first second of the new minute
			tm_internal.tm_min = tm_internal.tm_min + 1;
			sleep(1); // to synchronize, otherwise it explodes
			// The printf below was just for verification, it can be removed
			//printf("Speedup, minute:%d, second: %d\n",  tm_internal.tm_min, tm_internal.tm_sec);
			if(tm_internal.tm_min == 60){
				tm_internal.tm_min = 0;
				tm_internal.tm_hour = tm_internal.tm_hour + 1;
				if(tm_internal.tm_hour == 24){
					tm_internal.tm_hour = 0;
				}
			}

		}
		pthread_mutex_unlock(&internal_mutex);
		// The printf below was for verification, it can be commented out
		//printf("Speedup, hour: %d, minute:%d, second: %d\n", tm_internal.tm_hour, tm_internal.tm_min, tm_internal.tm_sec);
	}
}

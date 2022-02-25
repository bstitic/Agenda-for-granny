## Interactive agenda for granny

##### This is an interactive agenda in C I developed when applying to an electrical engineering internship at Fazua (it's a particularly fun project design-wise :)! ).

##### This tool notifies the user of the activities to be done at a particular time. It should be noted that activities have an internal state, which can correspond to "done" or "undone". Therefore, the agenda prints to the console: 1) whenever the user inputs some time (to ask what should be done during that time); 2) when an acitivity is about to start and; 3) 10 minutes before a task is about to finish only if the state of the activity is "undone".

##### This agenda also makes sure that between each printed output, there is an internval of 3 seconds. It is also important to explain that, when the user inputs some time, the agenda will answer with the activity that the user should be doing during that time. If the state of the activity is "undone", the agenda will then ask the user if the specific activity is being performed. If the user replies "yes", then the internal state will be changed to "done". 

##### Also, in this project, there are 8 activities which are part of a struct in the code. These activities are scheduled considering a 24h clock. Furthermore, this interactive agenda can use both a real-time, local timezone clock and an internal, accelerated clock which uses a speed factor. This speed factor, which modifies clock frequency, can be up to x10. In addition, the project incorporates the usage of Linux system calls, threads and semaphores.

##### Finally, the code includes relevant comments at the start that cover possible optimizations and important assumptions (including edge cases and string format). There were a lot of aspects that were up to the applicant to design so this initial comment section is very relevant. The code also includes headings to organize it clearly and helpful comments. 

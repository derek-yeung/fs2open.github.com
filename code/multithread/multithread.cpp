#include "asteroid/asteroid.h"
#include "cmeasure/cmeasure.h"
#include "debris/debris.h"
#include "fireball/fireballs.h"
#include "freespace2/freespace.h"
#include "globalincs/linklist.h"
#include "iff_defs/iff_defs.h"
#include "io/timer.h"
#include "jumpnode/jumpnode.h"
#include "lighting/lighting.h"
#include "mission/missionparse.h"
#include "network/multi.h"
#include "network/multiutil.h"
#include "object/objcollide.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "object/objectshield.h"
#include "object/objectsnd.h"
#include "observer/observer.h"
#include "parse/scripting.h"
#include "playerman/player.h"
#include "radar/radar.h"
#include "radar/radarsetup.h"
#include "render/3d.h"
#include "ship/afterburner.h"
#include "ship/ship.h"
#include "weapon/beam.h"
#include "weapon/shockwave.h"
#include "weapon/swarm.h"
#include "weapon/weapon.h"

#include "multithread/multithread.h"

#include <time.h>

extern int Cmdline_num_threads;

extern void game_shutdown(void);

extern int G3_count;

SDL_mutex *collision_master_mutex = NULL;
SDL_cond *collision_master_condition = NULL;
SDL_mutex *render_mutex = NULL;
SDL_mutex *g3_count_mutex = NULL;
SDL_mutex *hook_mutex = NULL;

SCP_vector<collision_pair> collision_list;
SCP_vector<unsigned int> thread_number;
SCP_vector<collision_pair> thread_collision_vars;
SCP_vector<thread_condition> conditions;

bool threads_alive = false;

unsigned int executions = 0;
int threads_used_record = 0;

timespec wait_time = { 2, 0 };

int supercollider_thread(void *obj_collision_vars_ptr);

void create_threads()
{
	int i;
	collision_pair setup_pair;
	char buffer[50];

	threads_alive = true;
	setup_pair.object_1 = NULL;
	setup_pair.object_2 = NULL;
	setup_pair.processed = true;
	setup_pair.operation_func = NULL;

//	SDL_SetMainReady();
//	SDL_Init(0);

	collision_master_mutex = SDL_CreateMutex();
	if (collision_master_mutex == NULL) {
		Error(LOCATION, "collision_master_mutex create failed: %s\n", SDL_GetError());
	}
	collision_master_condition = SDL_CreateCond();
	if (collision_master_condition == NULL) {
		Error(LOCATION, "collision_master_condition conditional var create failed: %s\n", SDL_GetError());
	}
	render_mutex = SDL_CreateMutex();
	if (render_mutex == NULL) {
		Error(LOCATION, "render_mutex create failed: %s\n", SDL_GetError());
	}
	g3_count_mutex = SDL_CreateMutex();
	if (render_mutex == NULL) {
		Error(LOCATION, "g3_count_mutex create failed: %s\n", SDL_GetError());
	}
	hook_mutex = SDL_CreateMutex();
	if (render_mutex == NULL) {
		Error(LOCATION, "hook_mutex create failed: %s\n", SDL_GetError());
	}

	if (Cmdline_num_threads < 1) {
		Cmdline_num_threads = 1;
	}
	conditions.resize(Cmdline_num_threads);

	//ensure these numbers are filled before creating our threads
	for (i = 0; i < Cmdline_num_threads; i++) {
		thread_number.push_back(i);
	}

	for (i = 0; i < Cmdline_num_threads; i++) {
		nprintf(("Multithread", "multithread: Creating thread %d\n", i));
		thread_collision_vars.push_back(setup_pair);
		conditions[i].mutex = SDL_CreateMutex();
		if (conditions[i].mutex == NULL) {
			Error(LOCATION, "supercollider mutex create failed: %s\n", SDL_GetError());
		}
		conditions[i].condition = SDL_CreateCond();
		if (conditions[i].mutex == NULL) {
			Error(LOCATION, "supercollider conditional var create failed: %s\n", SDL_GetError());
		}
		sprintf(buffer, "Collider Thread %d", thread_number[i]);
		conditions[i].thread = SDL_CreateThread(supercollider_thread, buffer, &thread_number[i]);
		if (conditions[i].mutex == NULL) {
			Error(LOCATION, "supercollider thread create failed: %s\n", SDL_GetError());
		}
	}
}

void destroy_threads()
{
	int i, retval;

	threads_alive = false;
	//wait for our threads to finish, they shouldn't be doing anything anyway
	for (i = 0; i < Cmdline_num_threads; i++) {
		nprintf(("Multithread", "multithread: waiting for threads to finish %d\n", i));
		SDL_WaitThread(conditions[i].thread, &retval);
		SDL_DestroyCond(conditions[i].condition);
		SDL_DestroyMutex(conditions[i].mutex);
	}
	SDL_DestroyMutex(render_mutex);
	SDL_DestroyMutex(g3_count_mutex);
	SDL_DestroyMutex(collision_master_mutex);
	SDL_DestroyMutex(render_mutex);
	SDL_DestroyCond(collision_master_condition);
}

void collision_pair_clear()
{
	collision_list.clear();
}

void collision_pair_add(object *object_1, object *object_2)
{
	collision_pair pair;

	pair.object_1 = object_1;
	pair.object_2 = object_2;
	pair.processed = false;
	pair.operation_func = NULL;

	collision_list.push_back(pair);
}

void execute_collisions()
{
	int i;
	unsigned int object_counter = 0;
	unsigned int loop_counter = 0;
	SCP_vector<collision_pair>::iterator it;
	bool assigned_any, assigned_once = false;
	bool done = false;
	bool skip = false;
	int threads_used = 1;
	int SDL_return;

	time_t start_time = time(NULL);

	OPENGL_LOCK
	if (!G3_count) {
		g3_start_frame(1);
	}
	nprintf(("Multithread", "multithread: execution %d start\n", executions));

	while (done == false) {
		//go through our collision list
		nprintf(("Multithread", "multithread: execution %d start main loop %d\n", executions, loop_counter));
		if ((time(NULL) - start_time) > SAFETY_TIME) {
			nprintf(("Multithread", "multithread: execution %d - STUCK\n", executions));
			Error(LOCATION, "Encountered fatal error in multithreading\n");
		}
		assigned_any = false;
		object_counter = 0;
		for (it = collision_list.begin(); it != collision_list.end(); it++, object_counter++) {
			if (it->processed == true) {
				continue;
			}
			assigned_once = false;
			skip = false;

			//ensure the objects being checked aren't already being used
			for (i = 0; i < Cmdline_num_threads; i++) {
				if ((it->object_1 == thread_collision_vars[i].object_1) || (it->object_1 == thread_collision_vars[i].object_2) || (it->object_2 == thread_collision_vars[i].object_1) || (it->object_2 == thread_collision_vars[i].object_2)) {
					skip = true;
					break;
				}
			}

			if (skip == true) {
				continue;
			}
			//check for a free thread to handle the collision
			for (i = 0; i < Cmdline_num_threads; i++) {
				SDL_return = SDL_TryLockMutex(conditions[i].mutex);
				if (SDL_return == 0) {
					if (thread_collision_vars[i].processed == false) {
						nprintf(("Multithread", "multithread: execution %d object pair %d - thread %d busy\n", executions, object_counter, i));
						SDL_UnlockMutex(conditions[i].mutex);
						continue;
					}
					thread_collision_vars[i].object_1 = it->object_1;
					thread_collision_vars[i].object_2 = it->object_2;
					thread_collision_vars[i].processed = false;
					it->processed = true;

					if (SDL_CondSignal(conditions[i].condition) < 0) {
						Error(LOCATION, "supercollider conditionl var signal failed: %s\n", SDL_GetError());
					}
					SDL_return = SDL_UnlockMutex(conditions[i].mutex);
					if (SDL_UnlockMutex(conditions[i].mutex) < 0) {
						Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
					}
					assigned_once = true;
					if ((i + 1) > threads_used) {
						threads_used = (i + 1);
					}
					break;
				}
				//may be bugged
//				else if (SDL_return == -1) {
//					Error(LOCATION, "supercollider mutex trylock failed: %s\n", SDL_GetError());
//				}
			}
		}

		//make sure we processs everything on the list
		done = true;
		for (it = collision_list.begin(); it != collision_list.end(); it++) {
			if (it->processed == false) {
				nprintf(("Multithread", "multithread: execution %d - looping back\n", executions));
				done = false;
				loop_counter++;
				break;
			}
		}
	}
	if (threads_used > threads_used_record) {
		threads_used_record = threads_used;
	}
	nprintf(("Multithread", "multithread: execution %d - %d collision pairs, %d threads used - record is %d threads used\n", executions, collision_list.size(), threads_used, threads_used_record));

	//make sure all the threads are done executing
	for (i = 0; i < Cmdline_num_threads; i++) {
		if (SDL_LockMutex(conditions[i].mutex) < 0) {
			Error(LOCATION, "supercollider mutex lock failed: %s\n", SDL_GetError());
		}
		if (SDL_UnlockMutex(conditions[i].mutex) < 0) {
			Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
		}
	}
	executions++;
	g3_end_frame();
	OPENGL_UNLOCK
}

int supercollider_thread(void *num)
{
	int thread_num = *(int *) num;

	nprintf(("Multithread", "multithread: supercollider_thread %d started\n", thread_num));

	if (SDL_LockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex lock failed: %s\n", SDL_GetError());
	}
	while (threads_alive) {
		if (SDL_CondWait(conditions[thread_num].condition, conditions[thread_num].mutex) < 0) {
			Error(LOCATION, "supercollider conditional wait failed: %s\n", SDL_GetError());
		}
		if ((thread_collision_vars[thread_num].object_1 != NULL) && (thread_collision_vars[thread_num].object_2 != NULL)) {
			obj_collide_pair(thread_collision_vars[thread_num].object_1, thread_collision_vars[thread_num].object_2);
			thread_collision_vars[thread_num].object_1 = NULL;
			thread_collision_vars[thread_num].object_2 = NULL;
		}
		thread_collision_vars[thread_num].processed = true;
	}
	if (SDL_UnlockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
	}

	return 0;
}

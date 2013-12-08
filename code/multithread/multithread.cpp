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

SCP_vector<unsigned int> collision_list;
SCP_vector<unsigned int>::iterator collision_list_it;

SCP_vector<unsigned int> thread_number;
SCP_vector<thread_vars> thread_collision_vars;
SCP_vector<thread_condition> conditions;

bool threads_alive = false;

SCP_hash_map<unsigned int, int (*)(obj_pair *)> collision_func_list;
SCP_hash_map<unsigned int, int (*)(obj_pair *)>::iterator collision_func_it;

SCP_hash_map<unsigned int, collision_data> collision_cache;
SCP_hash_map<unsigned int, collision_data>::iterator collision_cache_it;

unsigned int executions = 0;
int threads_used_record = 0;

int supercollider_thread(void *obj_collision_vars_ptr);

//char *pref_path = NULL;
//
//void InitializePrefPath() {
//    char *base_path = SDL_GetPrefPath("FSO SCP", "Freespace 2 Open");
//    if (base_path) {
//        pref_path = SDL_strdup(base_path);
//        SDL_free(base_path);
//        nprintf(("path = %s\n", pref_path));
//    } else {
//        /* Do something to disable writing in-game */
//    	nprintf(("path = NULL\n"));
//    }
//}

void create_threads()
{
	int i;
	thread_vars setup_vars;
	char buffer[50];

	threads_alive = true;
//	setup_pair.objs.a = NULL;
//	setup_pair.objs.b = NULL;
//	setup_pair.objs.check_collision = NULL;
//	setup_pair.objs.next_check_time = -1;
//	setup_pair.objs.next = NULL;
//	setup_pair.processed = true;
//	setup_pair.signature_a = -1;
//	setup_pair.signature_b = -1;
//	setup_pair.operation_func = NULL;
	setup_vars.collision = NULL;

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
		thread_collision_vars.push_back(setup_vars);
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

	//populate functions for lookup table
	collision_func_list.clear();
	collision_func_list[COLLISION_OF(OBJ_WEAPON,OBJ_SHIP)] = collide_ship_weapon;
	collision_func_list[COLLISION_OF(OBJ_SHIP, OBJ_WEAPON)] = collide_ship_weapon;
	collision_func_list[COLLISION_OF(OBJ_DEBRIS, OBJ_WEAPON)] = collide_debris_weapon;
	collision_func_list[COLLISION_OF(OBJ_WEAPON, OBJ_DEBRIS)] = collide_debris_weapon;
	collision_func_list[COLLISION_OF(OBJ_DEBRIS, OBJ_SHIP)] = collide_debris_ship;
	collision_func_list[COLLISION_OF(OBJ_SHIP, OBJ_DEBRIS)] = collide_debris_ship;
	collision_func_list[COLLISION_OF(OBJ_ASTEROID, OBJ_WEAPON)] = collide_asteroid_weapon;
	collision_func_list[COLLISION_OF(OBJ_WEAPON, OBJ_ASTEROID)] = collide_asteroid_weapon;
	collision_func_list[COLLISION_OF(OBJ_ASTEROID, OBJ_SHIP)] = collide_asteroid_ship;
	collision_func_list[COLLISION_OF(OBJ_SHIP, OBJ_ASTEROID)] = collide_asteroid_ship;
	collision_func_list[COLLISION_OF(OBJ_SHIP, OBJ_BEAM)] = beam_collide_ship;
	collision_func_list[COLLISION_OF(OBJ_BEAM, OBJ_SHIP)] = beam_collide_ship;
	collision_func_list[COLLISION_OF(OBJ_ASTEROID, OBJ_BEAM)] = beam_collide_asteroid;
	collision_func_list[COLLISION_OF(OBJ_BEAM, OBJ_ASTEROID)] = beam_collide_asteroid;
	collision_func_list[COLLISION_OF(OBJ_DEBRIS, OBJ_BEAM)] = beam_collide_debris;
	collision_func_list[COLLISION_OF(OBJ_BEAM, OBJ_DEBRIS)] = beam_collide_debris;
	collision_func_list[COLLISION_OF(OBJ_WEAPON, OBJ_BEAM)] = beam_collide_missile;
	collision_func_list[COLLISION_OF(OBJ_BEAM, OBJ_WEAPON)] = beam_collide_missile;
	collision_func_list[COLLISION_OF(OBJ_SHIP,OBJ_SHIP)] = collide_ship_ship;
	collision_func_list[COLLISION_OF(OBJ_WEAPON, OBJ_WEAPON)] = collide_weapon_weapon;

	collision_cache.clear();
//	InitializePrefPath();
}

void destroy_threads()
{
	int i, retval;

	threads_alive = false;
	//wait for our threads to finish, they shouldn't be doing anything anyway
	for (i = 0; i < Cmdline_num_threads; i++) {
		nprintf(("Multithread", "multithread: waiting for threads to finish %d\n", i));
		if (SDL_CondSignal(conditions[i].condition) < 0) {
			Error(LOCATION, "supercollider conditionl var signal failed: %s\n", SDL_GetError());
		}
		SDL_WaitThread(conditions[i].thread, &retval);
		SDL_DestroyCond(conditions[i].condition);
		SDL_DestroyMutex(conditions[i].mutex);
	}
	SDL_DestroyMutex(render_mutex);
	SDL_DestroyMutex(g3_count_mutex);
	SDL_DestroyMutex(collision_master_mutex);
	SDL_DestroyCond(collision_master_condition);
}

void collision_pair_clear()
{
	collision_list.clear();
}

void collision_pair_add(object *object_1, object *object_2)
{
	collision_data pair;
	unsigned int ctype = 0, ctype_2 = 0;
	unsigned int key = 0;
	bool cache_hit = false;

	if(
			(object_1 == NULL) ||
			(object_2 == NULL) ||
			// Don't check collisions with yourself
			(object_1 == object_2) ||
			// This object doesn't collide with anything
			(!(object_1->flags & OF_COLLIDES)) ||
			// This object doesn't collide with anything
			(!(object_2->flags & OF_COLLIDES)) ||
			// Make sure you're not checking a parent with it's kid or vicy-versy
			(reject_obj_pair_on_parent(object_1, object_2))
			) {
		return;
	}

	Assert(object_1->type < 127);
	Assert(object_2->type < 127);

	ctype = COLLISION_OF(object_1->type,object_2->type);
	switch (ctype) {
		case COLLISION_OF(OBJ_WEAPON, OBJ_SHIP):
		case COLLISION_OF(OBJ_WEAPON, OBJ_DEBRIS):
		case COLLISION_OF(OBJ_SHIP, OBJ_DEBRIS):
		case COLLISION_OF(OBJ_WEAPON, OBJ_ASTEROID):
		case COLLISION_OF(OBJ_SHIP, OBJ_ASTEROID):
		case COLLISION_OF(OBJ_SHIP, OBJ_BEAM):
		case COLLISION_OF(OBJ_ASTEROID, OBJ_BEAM):
		case COLLISION_OF(OBJ_DEBRIS, OBJ_BEAM):
		case COLLISION_OF(OBJ_WEAPON, OBJ_BEAM): {
			// Swap them if needed
			pair.objs.a = object_2;
			pair.objs.b = object_1;
			ctype_2 = COLLISION_OF(object_2->type,object_1->type);
			break;
		}

		case COLLISION_OF(OBJ_WEAPON, OBJ_WEAPON): {
			weapon_info *awip, *bwip;
			awip = &Weapon_info[Weapons[object_1->instance].weapon_info_index];
			bwip = &Weapon_info[Weapons[object_2->instance].weapon_info_index];

			if ((awip->weapon_hitpoints > 0) || (bwip->weapon_hitpoints > 0)) {
				if (bwip->weapon_hitpoints == 0) {
					pair.objs.a = object_2;
					pair.objs.b = object_1;
					ctype_2 = COLLISION_OF(object_2->type,object_1->type);
				} else {
					pair.objs.a = object_1;
					pair.objs.b = object_2;
					ctype_2 = ctype;
				}
			}
			break;
		}

		case COLLISION_OF(OBJ_SHIP, OBJ_WEAPON):
		case COLLISION_OF(OBJ_DEBRIS, OBJ_WEAPON):
		case COLLISION_OF(OBJ_DEBRIS, OBJ_SHIP):
		case COLLISION_OF(OBJ_ASTEROID, OBJ_WEAPON):
		case COLLISION_OF(OBJ_SHIP, OBJ_SHIP):
		case COLLISION_OF(OBJ_BEAM, OBJ_SHIP):
		case COLLISION_OF(OBJ_BEAM, OBJ_ASTEROID):
		case COLLISION_OF(OBJ_BEAM, OBJ_DEBRIS):
		case COLLISION_OF(OBJ_BEAM, OBJ_WEAPON): {
			pair.objs.a = object_1;
			pair.objs.b = object_2;
			ctype_2 = ctype;
			break;
		}

		default: {
			//not a valid collision
			return;
			break;
		}
	}

	collision_func_it = collision_func_list.find(ctype_2);

	if(collision_func_it != collision_func_list.end()) {
		//do a quick beam collision check
		if (pair.objs.a->type == OBJ_BEAM) {
			switch(pair.objs.b->type)
			{
				case OBJ_SHIP:
				case OBJ_ASTEROID:
				case OBJ_DEBRIS:
				case OBJ_WEAPON:
				{
					if (beam_collide_early_out(pair.objs.a, pair.objs.b)) {
						return;
					}
					break;
				}
			}
		}
		pair.objs.check_collision = collision_func_it->second;
	}
	else {
		//exit if not found in list
		return;
	}


	key = (OBJ_INDEX(pair.objs.a) << 12) + OBJ_INDEX(pair.objs.b);
	collision_cache_it = collision_cache.find(key);

	if (collision_cache_it == collision_cache.end()) {
		//collision not cached - create using []
		collision_cache[key].objs.a = pair.objs.a;
		collision_cache[key].objs.b = pair.objs.b;
		collision_cache[key].signature_a = pair.objs.a->signature;
		collision_cache[key].signature_b = pair.objs.b->signature;
		collision_cache[key].objs.next_check_time = timestamp(0);
	} else {
		//check for correct pair
		if ((collision_cache[key].signature_a == collision_cache[key].objs.a->signature) && (collision_cache[key].signature_b == collision_cache[key].objs.b->signature)) {
			cache_hit = true;
		} else {
			collision_cache[key].objs.a = pair.objs.a;
			collision_cache[key].objs.b = pair.objs.b;
			collision_cache[key].signature_a = pair.objs.a->signature;
			collision_cache[key].signature_b = pair.objs.b->signature;
			collision_cache[key].objs.next_check_time = timestamp(0);
		}
	}
	collision_cache[key].processed = PROCESS_STATE_UNPROCESSED;
	collision_cache[key].objs.check_collision = pair.objs.check_collision;

	if ( cache_hit &&  pair.objs.a->type != OBJ_BEAM ) {
		// if this signature is valid, make the necessary checks to see if we need to collide check
		if ( collision_cache[key].objs.next_check_time == -1 ) {
			return;
		} else {
			if ( !timestamp_elapsed(collision_cache[key].objs.next_check_time) ) {
				return;
			}
		}
	} else {
		// only check debris:weapon collisions for player
		if (pair.objs.check_collision == collide_debris_weapon) {
			// weapon is object_2
			if ( !(Weapon_info[Weapons[pair.objs.b->instance].weapon_info_index].wi_flags & WIF_TURNS) ) {
				// check for dumbfire weapon
				// check if debris is behind laser
				float vdot;
				if (Weapon_info[Weapons[pair.objs.b->instance].weapon_info_index].subtype == WP_LASER) {
					vec3d velocity_rel_weapon;
					vm_vec_sub(&velocity_rel_weapon, &pair.objs.b->phys_info.vel, &pair.objs.a->phys_info.vel);
					vdot = -vm_vec_dot(&velocity_rel_weapon, &pair.objs.b->orient.vec.fvec);
				} else {
					vdot = vm_vec_dot( &pair.objs.a->phys_info.vel, &pair.objs.b->phys_info.vel);
				}
				if ( vdot <= 0.0f )	{
					// They're heading in opposite directions...
					// check their positions
					vec3d weapon2other;
					vm_vec_sub( &weapon2other, &pair.objs.a->pos, &pair.objs.b->pos );
					float pdot = vm_vec_dot( &pair.objs.b->orient.vec.fvec, &weapon2other );
					if ( pdot <= -pair.objs.a->radius )	{
						// The other object is behind the weapon by more than
						// its radius, so it will never hit...
						collision_cache[key].objs.next_check_time = -1;
						return;
					}
				}

				// check dist vs. dist moved during weapon lifetime
				vec3d delta_v;
				vm_vec_sub(&delta_v, &pair.objs.b->phys_info.vel, &pair.objs.a->phys_info.vel);
				if (vm_vec_dist_squared(&pair.objs.a->pos, &pair.objs.b->pos) > (vm_vec_mag_squared(&delta_v)*Weapons[pair.objs.b->instance].lifeleft*Weapons[pair.objs.b->instance].lifeleft)) {
					collision_cache[key].objs.next_check_time = -1;
					return;
				}

				// for nonplayer ships, only create collision pair if close enough
				if ( (pair.objs.b->parent >= 0) && !(Objects[pair.objs.b->parent].flags & OF_PLAYER_SHIP) && (vm_vec_dist(&pair.objs.b->pos, &pair.objs.a->pos) < (4.0f*pair.objs.a->radius + 200.0f)) ) {
					collision_cache[key].objs.next_check_time = -1;
					return;
				}
			}
		}

		// don't check same team laser:ship collisions on small ships if not player
		if (pair.objs.check_collision == collide_ship_weapon) {
			// weapon is object_2
			if ( (pair.objs.b->parent >= 0)
				&& !(Objects[pair.objs.b->parent].flags & OF_PLAYER_SHIP)
				&& (Ships[Objects[pair.objs.b->parent].instance].team == Ships[pair.objs.a->instance].team)
				&& (Ship_info[Ships[pair.objs.a->instance].ship_info_index].flags & SIF_SMALL_SHIP)
				&& (Weapon_info[Weapons[pair.objs.b->instance].weapon_info_index].subtype == WP_LASER) ) {
				collision_cache[key].objs.next_check_time = -1;
				return;
			}
		}
	}

	collision_list.push_back(key);
}

void execute_collisions()
{
	int i;
	unsigned int loop_counter = 0;
	bool done = false;
	bool skip = false;
	int threads_used = 1;
	int SDL_return;
	int object_counter;

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
			Error(LOCATION, "Saftey time exceeded\n");
		}
		object_counter = 0;
		for (collision_list_it = collision_list.begin(); collision_list_it != collision_list.end(); collision_list_it++, object_counter++) {
			if (collision_cache[*collision_list_it].processed == PROCESS_STATE_FINISHED) {
				nprintf(("Multithread", "multithread: execution %d object pair %d already done\n", executions, object_counter));
				continue;
			}
			skip = false;

			//ensure the objects being checked aren't already being used
			for (i = 0; i < Cmdline_num_threads; i++) {
				SDL_return = SDL_TryLockMutex(conditions[i].mutex);
				if (SDL_return == 0) {
					//not assigned, can't be busy
					SDL_UnlockMutex(conditions[i].mutex);
					continue;
				} else {
					if (
							(collision_cache[*collision_list_it].objs.a == thread_collision_vars[i].a) ||
							(collision_cache[*collision_list_it].objs.a == thread_collision_vars[i].b) ||
							(collision_cache[*collision_list_it].objs.b == thread_collision_vars[i].a) ||
							(collision_cache[*collision_list_it].objs.b == thread_collision_vars[i].b)) {
						nprintf(("Multithread", "multithread: execution %d object pair %d conflicts with thread %d\n", executions, object_counter, i));
						skip = true;
						break;
					}
				}
			}

			if (skip == true) {
				continue;
			}
			//check for a free thread to handle the collision
			for (i = 0; i < Cmdline_num_threads; i++) {
				SDL_return = SDL_TryLockMutex(conditions[i].mutex);
				if (SDL_return == 0) {
					if (thread_collision_vars[i].collision != NULL) {
						nprintf(("Multithread", "multithread: execution %d object pair %d - thread %d busy\n", executions, object_counter, i));
						SDL_UnlockMutex(conditions[i].mutex);
						continue;
					}
					if (collision_cache[*collision_list_it].processed == PROCESS_STATE_BUSY) {
						nprintf(("Multithread", "multithread: execution %d object pair %d busy\n", executions, object_counter));
						SDL_UnlockMutex(conditions[i].mutex);
						break;
					}
					nprintf(("Multithread", "multithread: execution %d object pair %d assigned to thread %d\n", executions, object_counter, i));
					thread_collision_vars[i].collision = &collision_cache[*collision_list_it];
					thread_collision_vars[i].collision->processed = PROCESS_STATE_BUSY;
					thread_collision_vars[i].a = thread_collision_vars[i].collision->objs.a;
					thread_collision_vars[i].b = thread_collision_vars[i].collision->objs.b;

					if (SDL_CondSignal(conditions[i].condition) < 0) {
						Error(LOCATION, "supercollider conditionl var signal failed: %s\n", SDL_GetError());
					}
					SDL_return = SDL_UnlockMutex(conditions[i].mutex);
					if (SDL_UnlockMutex(conditions[i].mutex) < 0) {
						Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
					}
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
		for (collision_list_it = collision_list.begin(); collision_list_it != collision_list.end(); collision_list_it++) {
			if (collision_cache[*collision_list_it].processed != PROCESS_STATE_FINISHED) {
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
	int temp_check_time = -1;

	nprintf(("Multithread", "multithread: supercollider_thread %d started\n", thread_num));

	if (SDL_LockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex lock failed: %s\n", SDL_GetError());
	}
	while (threads_alive) {
		if (SDL_CondWait(conditions[thread_num].condition, conditions[thread_num].mutex) < 0) {
			Error(LOCATION, "supercollider conditional wait failed: %s\n", SDL_GetError());
		}
		if (threads_alive == false)
		{
			//check for exit condition
			return 0;
		}

		temp_check_time = thread_collision_vars[thread_num].collision->objs.next_check_time;

		if (thread_collision_vars[thread_num].collision->objs.check_collision(&(thread_collision_vars[thread_num].collision->objs))) {
			// don't have to check ever again
			thread_collision_vars[thread_num].collision->objs.next_check_time = -1;
		} else {
			//the functions can mess with this value - put it back
			thread_collision_vars[thread_num].collision->objs.next_check_time = temp_check_time;
		}

		nprintf(("Multithread", "multithread: thread %d done\n", thread_num));
		thread_collision_vars[thread_num].collision->processed = PROCESS_STATE_FINISHED;
		thread_collision_vars[thread_num].collision = NULL;
	}
	if (SDL_UnlockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
	}

	return 0;
}


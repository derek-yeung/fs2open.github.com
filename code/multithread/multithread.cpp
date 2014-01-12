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
SDL_mutex *beam_mutex = NULL;
SDL_mutex *ship_mutex = NULL;

SCP_vector<unsigned int> collision_list;

SCP_vector<unsigned int> thread_number;
SCP_vector<thread_vars> thread_collision_vars;
SCP_vector<thread_condition> conditions;

bool threads_alive = false;

typedef struct
{
	collision_eval_func eval_func;
	collision_exec_func exec_func;
	collision_fallback fallback_func;
} collision_func_set;

SCP_hash_map<unsigned int, collision_func_set> collision_func_list;

SCP_hash_map<unsigned int, collision_data> collision_cache;

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

void collision_func_list_entry_set(unsigned int objtype1, unsigned int objtype2, collision_eval_func eval, collision_exec_func exec, collision_fallback fallback)
{
	collision_func_list[COLLISION_OF(objtype1, objtype2)].eval_func = eval;
	collision_func_list[COLLISION_OF(objtype1, objtype2)].exec_func = exec;
	collision_func_list[COLLISION_OF(objtype1, objtype2)].fallback_func = fallback;
}

void create_threads()
{
	int i;
	thread_vars setup_vars;
	char buffer[50];

	threads_alive = true;
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
	if (g3_count_mutex == NULL) {
		Error(LOCATION, "g3_count_mutex create failed: %s\n", SDL_GetError());
	}
	hook_mutex = SDL_CreateMutex();
	if (hook_mutex == NULL) {
		Error(LOCATION, "hook_mutex create failed: %s\n", SDL_GetError());
	}
	beam_mutex = SDL_CreateMutex();
	if (beam_mutex == NULL) {
		Error(LOCATION, "beam_mutex create failed: %s\n", SDL_GetError());
	}
	ship_mutex = SDL_CreateMutex();
	if (ship_mutex == NULL) {
		Error(LOCATION, "ship_mutex create failed: %s\n", SDL_GetError());
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
	collision_func_list_entry_set(OBJ_SHIP, OBJ_WEAPON, collide_ship_weapon_eval, collide_ship_weapon_exec, collide_ship_weapon);
//	collision_func_list_entry_set(OBJ_SHIP, OBJ_WEAPON, NULL, NULL, collide_ship_weapon);

	collision_func_list_entry_set(OBJ_DEBRIS, OBJ_WEAPON, NULL, NULL, collide_debris_weapon);
	collision_func_list_entry_set(OBJ_DEBRIS, OBJ_SHIP, NULL, NULL, collide_debris_ship);
	collision_func_list_entry_set(OBJ_ASTEROID, OBJ_WEAPON, NULL, NULL, collide_asteroid_weapon);
	collision_func_list_entry_set(OBJ_ASTEROID, OBJ_SHIP, NULL, NULL, collide_asteroid_ship);

	collision_func_list_entry_set(OBJ_BEAM, OBJ_SHIP, beam_collide_ship_eval, beam_collide_ship_exec, beam_collide_ship);
//	collision_func_list_entry_set(OBJ_BEAM, OBJ_SHIP, NULL, NULL, beam_collide_ship);

	collision_func_list_entry_set(OBJ_BEAM, OBJ_ASTEROID, NULL, NULL, beam_collide_asteroid);
	collision_func_list_entry_set(OBJ_BEAM, OBJ_DEBRIS, NULL, NULL, beam_collide_debris);
	collision_func_list_entry_set(OBJ_BEAM, OBJ_WEAPON, NULL, NULL, beam_collide_missile);
	collision_func_list_entry_set(OBJ_SHIP, OBJ_SHIP, NULL, NULL, collide_ship_ship);
	collision_func_list_entry_set(OBJ_WEAPON, OBJ_WEAPON, NULL, NULL, collide_weapon_weapon);

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
	collision_data *data = NULL;
	unsigned int ctype = 0;
	unsigned int key = 0;
	bool cache_hit = false;
	SCP_hash_map<unsigned int, collision_data>::iterator collision_cache_it;
	SCP_hash_map<unsigned int, collision_func_set>::iterator collision_func_it;

	if ((object_1 == NULL) || (object_2 == NULL) ||
	// Don't check collisions with yourself
	(object_1 == object_2) ||
	// This object doesn't collide with anything
	(!(object_1->flags & OF_COLLIDES)) ||
	// This object doesn't collide with anything
	(!(object_2->flags & OF_COLLIDES)) ||
	// Make sure you're not checking a parent with it's kid or vicy-versy
	(reject_obj_pair_on_parent(object_1, object_2))) {
		return;
	}

	Assert(object_1->type < 127);
	Assert(object_2->type < 127);

	pair.objs.a = object_1;
	pair.objs.b = object_2;
	ctype = COLLISION_OF(object_1->type, object_2->type);
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
			ctype = COLLISION_OF(object_2->type, object_1->type);
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
					ctype = COLLISION_OF(object_2->type, object_1->type);
				}
			}
			break;
		}

		default: {
			//not a valid collision
			return;
			break;
		}
	}

	collision_func_it = collision_func_list.find(ctype);

	if (collision_func_it != collision_func_list.end()) {
		//do a quick beam collision check
		if (pair.objs.a->type == OBJ_BEAM) {
			switch (pair.objs.b->type) {
				case OBJ_SHIP:
				case OBJ_ASTEROID:
				case OBJ_DEBRIS:
				case OBJ_WEAPON: {
					if (beam_collide_early_out(pair.objs.a, pair.objs.b)) {
						return;
					}
					break;
				}
				default:
					Error(LOCATION, "Should never get here\n");
			}
		}
		pair.objs.check_collision = collision_func_it->second.fallback_func;
		pair.eval_func = collision_func_it->second.eval_func;
		pair.exec_func = collision_func_it->second.exec_func;
	} else {
		//exit if not found in list
		return;
	}

	key = (OBJ_INDEX(pair.objs.a) << 12) + OBJ_INDEX(pair.objs.b);
	collision_cache_it = collision_cache.find(key);

	if (collision_cache_it == collision_cache.end()) {
		//collision not cached - create using []
		data = &collision_cache[key];
		data->objs.a = pair.objs.a;
		data->objs.b = pair.objs.b;
		data->signature_a = pair.objs.a->signature;
		data->signature_b = pair.objs.b->signature;
		data->objs.next_check_time = timestamp(0);
		data->in_use = true;
	} else {
		data = &collision_cache[key];
		if(data->in_use == false) {
			data->objs.a = pair.objs.a;
			data->objs.b = pair.objs.b;
			data->signature_a = pair.objs.a->signature;
			data->signature_b = pair.objs.b->signature;
			data->objs.next_check_time = timestamp(0);
			data->in_use = true;
		} else {
			//check for correct pair
			if ((data->signature_a == data->objs.a->signature) && (data->signature_b == data->objs.b->signature)) {
				cache_hit = true;
			} else {
				data->objs.a = pair.objs.a;
				data->objs.b = pair.objs.b;
				data->signature_a = pair.objs.a->signature;
				data->signature_b = pair.objs.b->signature;
				data->objs.next_check_time = timestamp(0);
			}
		}
	}
	data->processed = PROCESS_STATE_UNPROCESSED;
	data->objs.check_collision = pair.objs.check_collision;
	data->eval_func = pair.eval_func;
	data->exec_func = pair.exec_func;
	data->exec_data.result = COLLISION_RESULT_UNEVALUATED;

	if (cache_hit && data->objs.a->type != OBJ_BEAM) {
		// if this signature is valid, make the necessary checks to see if we need to collide check
		if (data->objs.next_check_time == -1) {
			return;
		} else {
			if (!timestamp_elapsed(data->objs.next_check_time)) {
				return;
			}
		}
	} else {
		// only check debris:weapon collisions for player
		if (pair.objs.check_collision == collide_debris_weapon) {
			// weapon is object_2
			if (!(Weapon_info[Weapons[data->objs.b->instance].weapon_info_index].wi_flags & WIF_TURNS)) {
				// check for dumbfire weapon
				// check if debris is behind laser
				float vdot;
				if (Weapon_info[Weapons[data->objs.b->instance].weapon_info_index].subtype == WP_LASER) {
					vec3d velocity_rel_weapon;
					vm_vec_sub(&velocity_rel_weapon, &data->objs.b->phys_info.vel, &data->objs.a->phys_info.vel);
					vdot = -vm_vec_dot(&velocity_rel_weapon, &data->objs.b->orient.vec.fvec);
				} else {
					vdot = vm_vec_dot(&data->objs.a->phys_info.vel, &data->objs.b->phys_info.vel);
				}
				if (vdot <= 0.0f) {
					// They're heading in opposite directions...
					// check their positions
					vec3d weapon2other;
					vm_vec_sub(&weapon2other, &data->objs.a->pos, &data->objs.b->pos);
					float pdot = vm_vec_dot(&data->objs.b->orient.vec.fvec, &weapon2other);
					if (pdot <= -data->objs.a->radius) {
						// The other object is behind the weapon by more than
						// its radius, so it will never hit...
						data->objs.next_check_time = -1;
						return;
					}
				}

				// check dist vs. dist moved during weapon lifetime
				vec3d delta_v;
				vm_vec_sub(&delta_v, &data->objs.b->phys_info.vel, &data->objs.a->phys_info.vel);
				if (vm_vec_dist_squared(&data->objs.a->pos, &data->objs.b->pos) > (vm_vec_mag_squared(&delta_v) * Weapons[data->objs.b->instance].lifeleft * Weapons[data->objs.b->instance].lifeleft)) {
					data->objs.next_check_time = -1;
					return;
				}

				// for nonplayer ships, only create collision pair if close enough
				if ((data->objs.b->parent >= 0) && !(Objects[data->objs.b->parent].flags & OF_PLAYER_SHIP) && (vm_vec_dist(&data->objs.b->pos, &data->objs.a->pos) < (4.0f * data->objs.a->radius + 200.0f))) {
					data->objs.next_check_time = -1;
					return;
				}
			}
		}

		// don't check same team laser:ship collisions on small ships if not player
		if (pair.objs.check_collision == collide_ship_weapon) {
			// weapon is object_2
			if ((data->objs.b->parent >= 0) && !(Objects[data->objs.b->parent].flags & OF_PLAYER_SHIP) && (Ships[Objects[data->objs.b->parent].instance].team == Ships[data->objs.a->instance].team) && (Ship_info[Ships[data->objs.a->instance].ship_info_index].flags & SIF_SMALL_SHIP) && (Weapon_info[Weapons[data->objs.b->instance].weapon_info_index].subtype == WP_LASER)) {
				data->objs.next_check_time = -1;
				return;
			}
		}
	}

	collision_list.push_back(key);
//	mprintf(("key = %d, ctype = %X, sig A = %d, sig B = %d\n", key, ctype, collision_cache[key].objs.a->signature, collision_cache[key].objs.b->signature));
}

void evaluate_collisions()
{
	int i;
	bool done = false;
	int SDL_return;
	static int thread_counter = 0;
	SCP_vector<unsigned int>::iterator collision_list_it;

	while (done == false) {
		//go through our collision list
		for (collision_list_it = collision_list.begin(); collision_list_it != collision_list.end(); collision_list_it++) {
			if (collision_cache[*collision_list_it].processed >= PROCESS_STATE_COLLIDED) {
				continue;
			}
			else {
				if ((Cmdline_num_threads == 1) || (collision_cache[*collision_list_it].eval_func == NULL)) {
					collision_cache[*collision_list_it].processed = PROCESS_STATE_COLLIDED;
					continue;
				}
			}

			//check for a free thread to handle the collision
			while (1) {
				SDL_return = SDL_TryLockMutex(conditions[thread_counter].mutex);
				if (SDL_return == 0) {
					if (thread_collision_vars[thread_counter].collision != NULL) {
						SDL_UnlockMutex(conditions[thread_counter].mutex);
						thread_counter = (thread_counter + 1) % Cmdline_num_threads;
						continue;
					}
					thread_collision_vars[thread_counter].collision = &collision_cache[*collision_list_it];
					thread_collision_vars[thread_counter].collision->processed = PROCESS_STATE_BUSY;
					thread_collision_vars[thread_counter].collision->exec_data.result = COLLISION_RESULT_INVALID;

					if (SDL_CondSignal(conditions[thread_counter].condition) < 0) {
						Error(LOCATION, "supercollider conditional var signal failed: %s\n", SDL_GetError());
					}
					SDL_return = SDL_UnlockMutex(conditions[thread_counter].mutex);
					if (SDL_UnlockMutex(conditions[thread_counter].mutex) < 0) {
						Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
					}
					break;
				}
				//may be bugged
//				else if (SDL_return == -1) {
//					Error(LOCATION, "supercollider mutex trylock failed: %s\n", SDL_GetError());
//				}
				thread_counter = (thread_counter + 1) % Cmdline_num_threads;
			}
		}

		//make sure we processs everything on the list
		while (1) {
			done = true;
			for (collision_list_it = collision_list.begin(); collision_list_it != collision_list.end(); collision_list_it++) {
				if (collision_cache[*collision_list_it].processed < PROCESS_STATE_COLLIDED) {
					done = false;
				}
			}
			if (done == true) {
				break;
			}
		}
	}

	//make sure all the threads are done executing
	for (i = 0; i < Cmdline_num_threads; i++) {
		if (SDL_LockMutex(conditions[i].mutex) < 0) {
			Error(LOCATION, "supercollider mutex lock failed: %s\n", SDL_GetError());
		}
		if (SDL_UnlockMutex(conditions[i].mutex) < 0) {
			Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
		}
	}
}

void execute_collisions()
{
	int temp_check_time = -1;
	SCP_vector<unsigned int>::iterator collision_list_it;

	OPENGL_LOCK
	if (!G3_count) {
		g3_start_frame(1);
	}

	for (collision_list_it = collision_list.begin(); collision_list_it != collision_list.end(); collision_list_it++) {
//		mprintf(("*collision_list_it = %d, ctype = %X, sig A = %d, sig B = %d\n", *collision_list_it, COLLISION_OF(collision_cache[*collision_list_it].objs.a->type, collision_cache[*collision_list_it].objs.b->type), collision_cache[*collision_list_it].objs.a->signature, collision_cache[*collision_list_it].objs.b->signature));
		Assert(collision_cache[*collision_list_it].processed == PROCESS_STATE_COLLIDED);
		//we didn't evaluate earlier, assume fallback
		if (collision_cache[*collision_list_it].exec_data.result == COLLISION_RESULT_UNEVALUATED) {
			temp_check_time = collision_cache[*collision_list_it].objs.next_check_time;
			Assert(collision_cache[*collision_list_it].objs.check_collision);
			if (collision_cache[*collision_list_it].objs.check_collision(&collision_cache[*collision_list_it].objs)) {
				// don't have to check ever again
				collision_cache[*collision_list_it].objs.next_check_time = -1;
			} else {
				collision_cache[*collision_list_it].objs.next_check_time = temp_check_time;
			}
		}
		else {
			if ((collision_cache[*collision_list_it].exec_func) && (collision_cache[*collision_list_it].exec_data.result == COLLISION_RESULT_COLLISION)) {
				collision_cache[*collision_list_it].exec_func(&(collision_cache[*collision_list_it].objs), &(collision_cache[*collision_list_it].exec_data));
			}
		}
		collision_cache[*collision_list_it].processed = PROCESS_STATE_EXECUTED;
	}

	g3_end_frame();
	OPENGL_UNLOCK
}

int supercollider_thread(void *num)
{
	int thread_num = *(int *) num;

	if (SDL_LockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex lock failed: %s\n", SDL_GetError());
	}
	while (threads_alive) {
		if (SDL_CondWait(conditions[thread_num].condition, conditions[thread_num].mutex) < 0) {
			Error(LOCATION, "supercollider conditional wait failed: %s\n", SDL_GetError());
		}
		if (threads_alive == false) {
			//check for exit condition
			return 0;
		}

		thread_collision_vars[thread_num].collision->exec_data.result = thread_collision_vars[thread_num].collision->eval_func(&(thread_collision_vars[thread_num].collision->objs), &(thread_collision_vars[thread_num].collision->exec_data));

		thread_collision_vars[thread_num].collision->processed = PROCESS_STATE_COLLIDED;
		thread_collision_vars[thread_num].collision = NULL;
	}
	if (SDL_UnlockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
	}

	return 0;
}


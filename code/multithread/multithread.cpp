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

SCP_hash_map<unsigned int, int (*)(obj_pair *)> collision_func_list;
SCP_hash_map<unsigned int, int (*)(obj_pair *)>::iterator collision_func_it;

unsigned int executions = 0;
int threads_used_record = 0;

int supercollider_thread(void *obj_collision_vars_ptr);

void create_threads()
{
	int i;
	collision_pair setup_pair;
	char buffer[50];

	threads_alive = true;
	setup_pair.pair.a = NULL;
	setup_pair.pair.b = NULL;
	setup_pair.pair.check_collision = NULL;
	setup_pair.pair.next_check_time = 0;
	setup_pair.pair.next = NULL;
	setup_pair.processed = true;
	setup_pair.operation_func = NULL;

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
	int (*check_collision)( obj_pair *pair );

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


	// Swap them if needed
	switch (COLLISION_OF(object_1->type, object_2->type)) {
		case COLLISION_OF(OBJ_WEAPON, OBJ_SHIP):
		case COLLISION_OF(OBJ_WEAPON, OBJ_DEBRIS):
		case COLLISION_OF(OBJ_SHIP, OBJ_DEBRIS):
		case COLLISION_OF(OBJ_WEAPON, OBJ_ASTEROID):
		case COLLISION_OF(OBJ_SHIP, OBJ_ASTEROID):
		case COLLISION_OF(OBJ_SHIP, OBJ_BEAM):
		case COLLISION_OF(OBJ_ASTEROID, OBJ_BEAM):
		case COLLISION_OF(OBJ_DEBRIS, OBJ_BEAM):
		case COLLISION_OF(OBJ_WEAPON, OBJ_BEAM): {
			pair.pair.a = object_2;
			pair.pair.b = object_1;
			break;
		}

		case COLLISION_OF(OBJ_WEAPON, OBJ_WEAPON): {
			weapon_info *awip, *bwip;
			awip = &Weapon_info[Weapons[object_1->instance].weapon_info_index];
			bwip = &Weapon_info[Weapons[object_2->instance].weapon_info_index];

			if ((awip->weapon_hitpoints > 0) || (bwip->weapon_hitpoints > 0)) {
				if (bwip->weapon_hitpoints == 0) {
					pair.pair.a = object_2;
					pair.pair.b = object_1;
				} else {
					pair.pair.a = object_1;
					pair.pair.b = object_2;
				}
			}
			break;
		}

		default: {
			pair.pair.a = object_1;
			pair.pair.b = object_2;
			break;
		}
	}

	collision_func_it = collision_func_list.find(COLLISION_OF(object_1->type,object_2->type));

	if(collision_func_it != collision_func_list.end()) {
		//do a quick beam collision check
		if (pair.pair.a->type == OBJ_BEAM) {
			switch(pair.pair.b->type)
			{
				case OBJ_SHIP:
				case OBJ_ASTEROID:
				case OBJ_DEBRIS:
				case OBJ_WEAPON:
				{
					if (beam_collide_early_out(pair.pair.a, pair.pair.b)) {
						return;
					}
					break;
				}
			}
		}
		pair.pair.check_collision = collision_func_it->second;
	}
	else {
		//exit if not found in list
		return;
	}

//
//	collider_pair *collision_info = NULL;
//	bool valid = false;
//	uint key = (OBJ_INDEX(object_1) << 12) + OBJ_INDEX(object_2);
//
//	collision_info = &Collision_cached_pairs[key];
//
//	if ( collision_info->initialized ) {
//		// make sure we're referring to the correct objects in case the original pair was deleted
//		if ( collision_info->signature_a == collision_info->a->signature &&
//			collision_info->signature_b == collision_info->b->signature ) {
//			valid = true;
//		} else {
//			collision_info->a = object_1;
//			collision_info->b = object_2;
//			collision_info->signature_a = object_1->signature;
//			collision_info->signature_b = object_2->signature;
//			collision_info->next_check_time = timestamp(0);
//		}
//	} else {
//		collision_info->a = object_1;
//		collision_info->b = object_2;
//		collision_info->signature_a = object_1->signature;
//		collision_info->signature_b = object_2->signature;
//		collision_info->initialized = true;
//		collision_info->next_check_time = timestamp(0);
//	}
//
//	if ( valid &&  object_1->type != OBJ_BEAM ) {
//		// if this signature is valid, make the necessary checks to see if we need to collide check
//		if ( collision_info->next_check_time == -1 ) {
//			return;
//		} else {
//			if ( !timestamp_elapsed(collision_info->next_check_time) ) {
//				return;
//			}
//		}
//	} else {
//		// only check debris:weapon collisions for player
//		if (check_collision == collide_debris_weapon) {
//			// weapon is object_2
//			if ( !(Weapon_info[Weapons[object_2->instance].weapon_info_index].wi_flags & WIF_TURNS) ) {
//				// check for dumbfire weapon
//				// check if debris is behind laser
//				float vdot;
//				if (Weapon_info[Weapons[object_2->instance].weapon_info_index].subtype == WP_LASER) {
//					vec3d velocity_rel_weapon;
//					vm_vec_sub(&velocity_rel_weapon, &object_2->phys_info.vel, &object_1->phys_info.vel);
//					vdot = -vm_vec_dot(&velocity_rel_weapon, &object_2->orient.vec.fvec);
//				} else {
//					vdot = vm_vec_dot( &object_1->phys_info.vel, &object_2->phys_info.vel);
//				}
//				if ( vdot <= 0.0f )	{
//					// They're heading in opposite directions...
//					// check their positions
//					vec3d weapon2other;
//					vm_vec_sub( &weapon2other, &object_1->pos, &object_2->pos );
//					float pdot = vm_vec_dot( &object_2->orient.vec.fvec, &weapon2other );
//					if ( pdot <= -object_1->radius )	{
//						// The other object is behind the weapon by more than
//						// its radius, so it will never hit...
//						collision_info->next_check_time = -1;
//						return;
//					}
//				}
//
//				// check dist vs. dist moved during weapon lifetime
//				vec3d delta_v;
//				vm_vec_sub(&delta_v, &object_2->phys_info.vel, &object_1->phys_info.vel);
//				if (vm_vec_dist_squared(&object_1->pos, &object_2->pos) > (vm_vec_mag_squared(&delta_v)*Weapons[object_2->instance].lifeleft*Weapons[object_2->instance].lifeleft)) {
//					collision_info->next_check_time = -1;
//					return;
//				}
//
//				// for nonplayer ships, only create collision pair if close enough
//				if ( (object_2->parent >= 0) && !(Objects[object_2->parent].flags & OF_PLAYER_SHIP) && (vm_vec_dist(&object_2->pos, &object_1->pos) < (4.0f*object_1->radius + 200.0f)) ) {
//					collision_info->next_check_time = -1;
//					return;
//				}
//			}
//		}
//
//		// don't check same team laser:ship collisions on small ships if not player
//		if (check_collision == collide_ship_weapon) {
//			// weapon is object_2
//			if ( (object_2->parent >= 0)
//				&& !(Objects[object_2->parent].flags & OF_PLAYER_SHIP)
//				&& (Ships[Objects[object_2->parent].instance].team == Ships[object_1->instance].team)
//				&& (Ship_info[Ships[object_1->instance].ship_info_index].flags & SIF_SMALL_SHIP)
//				&& (Weapon_info[Weapons[object_2->instance].weapon_info_index].subtype == WP_LASER) ) {
//				collision_info->next_check_time = -1;
//				return;
//			}
//		}
//	}
//
//	obj_pair new_pair;
//
//	new_pair.a = object_1;
//	new_pair.b = object_2;
//	new_pair.check_collision = check_collision;
//	new_pair.next_check_time = collision_info->next_check_time;
//
//	if ( check_collision(&new_pair) ) {
//		// don't have to check ever again
//		collision_info->next_check_time = -1;
//	} else {
//		collision_info->next_check_time = new_pair.next_check_time;
//	}

	pair.pair.a = object_1;
	pair.pair.b = object_2;
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
		object_counter = 0;
		for (it = collision_list.begin(); it != collision_list.end(); it++, object_counter++) {
			if (it->processed == true) {
				continue;
			}
			skip = false;

			//ensure the objects being checked aren't already being used
			for (i = 0; i < Cmdline_num_threads; i++) {
				if ((it->pair.a == thread_collision_vars[i].pair.a) || (it->pair.a == thread_collision_vars[i].pair.b) || (it->pair.b == thread_collision_vars[i].pair.a) || (it->pair.b == thread_collision_vars[i].pair.b)) {
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
					thread_collision_vars[i].pair.a = it->pair.a;
					thread_collision_vars[i].pair.b = it->pair.b;
					thread_collision_vars[i].processed = false;
					it->processed = true;

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
		if ((thread_collision_vars[thread_num].pair.a != NULL) && (thread_collision_vars[thread_num].pair.b != NULL)) {
			obj_collide_pair(thread_collision_vars[thread_num].pair.a, thread_collision_vars[thread_num].pair.b);
			thread_collision_vars[thread_num].pair.a = NULL;
			thread_collision_vars[thread_num].pair.b = NULL;
		}
		thread_collision_vars[thread_num].processed = true;
	}
	if (SDL_UnlockMutex(conditions[thread_num].mutex) < 0) {
		Error(LOCATION, "supercollider mutex unlock failed: %s\n", SDL_GetError());
	}

	return 0;
}

#if 0
void obj_collide_pair(object *object_1, object *object_2)
{
	uint ctype;
	int (*check_collision)( obj_pair *pair );
	int swapped = 0;

	check_collision = NULL;

	if ( object_1==object_2 ) return;		// Don't check collisions with yourself

	if ( !(object_1->flags&OF_COLLIDES) ) return;		// This object doesn't collide with anything
	if ( !(object_2->flags&OF_COLLIDES) ) return;		// This object doesn't collide with anything

	// Make sure you're not checking a parent with it's kid or vicy-versy
//	if ( object_1->parent_sig == object_2->signature && !(object_1->type == OBJ_SHIP && object_2->type == OBJ_DEBRIS) ) return;
//	if ( object_2->parent_sig == object_1->signature && !(object_1->type == OBJ_DEBRIS && object_2->type == OBJ_SHIP) ) return;
	if ( reject_obj_pair_on_parent(object_1,object_2) ) {
		return;
	}

	Assert( object_1->type < 127 );
	Assert( object_2->type < 127 );

	ctype = COLLISION_OF(object_1->type,object_2->type);
	switch( ctype )	{
	case COLLISION_OF(OBJ_WEAPON,OBJ_SHIP):
		swapped = 1;
		check_collision = collide_ship_weapon;
		break;
	case COLLISION_OF(OBJ_SHIP, OBJ_WEAPON):
		check_collision = collide_ship_weapon;
		break;
	case COLLISION_OF(OBJ_DEBRIS, OBJ_WEAPON):
		check_collision = collide_debris_weapon;
		break;
	case COLLISION_OF(OBJ_WEAPON, OBJ_DEBRIS):
		swapped = 1;
		check_collision = collide_debris_weapon;
		break;
	case COLLISION_OF(OBJ_DEBRIS, OBJ_SHIP):
		check_collision = collide_debris_ship;
		break;
	case COLLISION_OF(OBJ_SHIP, OBJ_DEBRIS):
		check_collision = collide_debris_ship;
		swapped = 1;
		break;
	case COLLISION_OF(OBJ_ASTEROID, OBJ_WEAPON):
		// Only check collision's with player weapons
//		if ( Objects[object_2->parent].flags & OF_PLAYER_SHIP ) {
			check_collision = collide_asteroid_weapon;
//		}
		break;
	case COLLISION_OF(OBJ_WEAPON, OBJ_ASTEROID):
		swapped = 1;
		// Only check collision's with player weapons
//		if ( Objects[object_1->parent].flags & OF_PLAYER_SHIP ) {
			check_collision = collide_asteroid_weapon;
//		}
		break;
	case COLLISION_OF(OBJ_ASTEROID, OBJ_SHIP):
		// Only check collisions with player ships
//		if ( object_2->flags & OF_PLAYER_SHIP )	{
			check_collision = collide_asteroid_ship;
//		}
		break;
	case COLLISION_OF(OBJ_SHIP, OBJ_ASTEROID):
		// Only check collisions with player ships
//		if ( object_1->flags & OF_PLAYER_SHIP )	{
			check_collision = collide_asteroid_ship;
//		}
		swapped = 1;
		break;
	case COLLISION_OF(OBJ_SHIP,OBJ_SHIP):
		check_collision = collide_ship_ship;
		break;

	case COLLISION_OF(OBJ_SHIP, OBJ_BEAM):
		if(beam_collide_early_out(object_2, object_1)){
			return;
		}
		swapped = 1;
		check_collision = beam_collide_ship;
		break;

	case COLLISION_OF(OBJ_BEAM, OBJ_SHIP):
		if(beam_collide_early_out(object_1, object_2)){
			return;
		}
		check_collision = beam_collide_ship;
		break;

	case COLLISION_OF(OBJ_ASTEROID, OBJ_BEAM):
		if(beam_collide_early_out(object_2, object_1)) {
			return;
		}
		swapped = 1;
		check_collision = beam_collide_asteroid;
		break;

	case COLLISION_OF(OBJ_BEAM, OBJ_ASTEROID):
		if(beam_collide_early_out(object_1, object_2)){
			return;
		}
		check_collision = beam_collide_asteroid;
		break;
	case COLLISION_OF(OBJ_DEBRIS, OBJ_BEAM):
		if(beam_collide_early_out(object_2, object_1)) {
			return;
		}
		swapped = 1;
		check_collision = beam_collide_debris;
		break;
	case COLLISION_OF(OBJ_BEAM, OBJ_DEBRIS):
		if(beam_collide_early_out(object_1, object_2)){
			return;
		}
		check_collision = beam_collide_debris;
		break;
	case COLLISION_OF(OBJ_WEAPON, OBJ_BEAM):
		if(beam_collide_early_out(object_2, object_1)) {
			return;
		}
		swapped = 1;
		check_collision = beam_collide_missile;
		break;

	case COLLISION_OF(OBJ_BEAM, OBJ_WEAPON):
		if(beam_collide_early_out(object_1, object_2)){
			return;
		}
		check_collision = beam_collide_missile;
		break;

	case COLLISION_OF(OBJ_WEAPON, OBJ_WEAPON): {
		weapon_info *awip, *bwip;
		awip = &Weapon_info[Weapons[object_1->instance].weapon_info_index];
		bwip = &Weapon_info[Weapons[object_2->instance].weapon_info_index];

		if ((awip->weapon_hitpoints > 0) || (bwip->weapon_hitpoints > 0)) {
			if (bwip->weapon_hitpoints == 0) {
				check_collision = collide_weapon_weapon;
				swapped=1;
			} else {
				check_collision = collide_weapon_weapon;
			}
		}

		break;
	}

	default:
		return;
	}

	if ( !check_collision ) return;

	// Swap them if needed
	if ( swapped )	{
		object *tmp = object_1;
		object_1 = object_2;
		object_2 = tmp;
	}

	collider_pair *collision_info = NULL;
	bool valid = false;
	uint key = (OBJ_INDEX(object_1) << 12) + OBJ_INDEX(object_2);

	collision_info = &Collision_cached_pairs[key];

	if ( collision_info->initialized ) {
		// make sure we're referring to the correct objects in case the original pair was deleted
		if ( collision_info->signature_a == collision_info->a->signature &&
			collision_info->signature_b == collision_info->b->signature ) {
			valid = true;
		} else {
			collision_info->a = object_1;
			collision_info->b = object_2;
			collision_info->signature_a = object_1->signature;
			collision_info->signature_b = object_2->signature;
			collision_info->next_check_time = timestamp(0);
		}
	} else {
		collision_info->a = object_1;
		collision_info->b = object_2;
		collision_info->signature_a = object_1->signature;
		collision_info->signature_b = object_2->signature;
		collision_info->initialized = true;
		collision_info->next_check_time = timestamp(0);
	}

	if ( valid &&  object_1->type != OBJ_BEAM ) {
		// if this signature is valid, make the necessary checks to see if we need to collide check
		if ( collision_info->next_check_time == -1 ) {
			return;
		} else {
			if ( !timestamp_elapsed(collision_info->next_check_time) ) {
				return;
			}
		}
	} else {
		//if ( object_1->type == OBJ_BEAM ) {
			//if(beam_collide_early_out(object_1, object_2)){
				//collision_info->next_check_time = -1;
				//return;
			//}
		//}

		// only check debris:weapon collisions for player
		if (check_collision == collide_debris_weapon) {
			// weapon is object_2
			if ( !(Weapon_info[Weapons[object_2->instance].weapon_info_index].wi_flags & WIF_TURNS) ) {
				// check for dumbfire weapon
				// check if debris is behind laser
				float vdot;
				if (Weapon_info[Weapons[object_2->instance].weapon_info_index].subtype == WP_LASER) {
					vec3d velocity_rel_weapon;
					vm_vec_sub(&velocity_rel_weapon, &object_2->phys_info.vel, &object_1->phys_info.vel);
					vdot = -vm_vec_dot(&velocity_rel_weapon, &object_2->orient.vec.fvec);
				} else {
					vdot = vm_vec_dot( &object_1->phys_info.vel, &object_2->phys_info.vel);
				}
				if ( vdot <= 0.0f )	{
					// They're heading in opposite directions...
					// check their positions
					vec3d weapon2other;
					vm_vec_sub( &weapon2other, &object_1->pos, &object_2->pos );
					float pdot = vm_vec_dot( &object_2->orient.vec.fvec, &weapon2other );
					if ( pdot <= -object_1->radius )	{
						// The other object is behind the weapon by more than
						// its radius, so it will never hit...
						collision_info->next_check_time = -1;
						return;
					}
				}

				// check dist vs. dist moved during weapon lifetime
				vec3d delta_v;
				vm_vec_sub(&delta_v, &object_2->phys_info.vel, &object_1->phys_info.vel);
				if (vm_vec_dist_squared(&object_1->pos, &object_2->pos) > (vm_vec_mag_squared(&delta_v)*Weapons[object_2->instance].lifeleft*Weapons[object_2->instance].lifeleft)) {
					collision_info->next_check_time = -1;
					return;
				}

				// for nonplayer ships, only create collision pair if close enough
				if ( (object_2->parent >= 0) && !(Objects[object_2->parent].flags & OF_PLAYER_SHIP) && (vm_vec_dist(&object_2->pos, &object_1->pos) < (4.0f*object_1->radius + 200.0f)) ) {
					collision_info->next_check_time = -1;
					return;
				}
			}
		}

		// don't check same team laser:ship collisions on small ships if not player
		if (check_collision == collide_ship_weapon) {
			// weapon is object_2
			if ( (object_2->parent >= 0)
				&& !(Objects[object_2->parent].flags & OF_PLAYER_SHIP)
				&& (Ships[Objects[object_2->parent].instance].team == Ships[object_1->instance].team)
				&& (Ship_info[Ships[object_1->instance].ship_info_index].flags & SIF_SMALL_SHIP)
				&& (Weapon_info[Weapons[object_2->instance].weapon_info_index].subtype == WP_LASER) ) {
				collision_info->next_check_time = -1;
				return;
			}
		}
	}

	obj_pair new_pair;

	new_pair.a = object_1;
	new_pair.b = object_2;
	new_pair.check_collision = check_collision;
	new_pair.next_check_time = collision_info->next_check_time;

	if ( check_collision(&new_pair) ) {
		// don't have to check ever again
		collision_info->next_check_time = -1;
	} else {
		collision_info->next_check_time = new_pair.next_check_time;
	}
}
#endif

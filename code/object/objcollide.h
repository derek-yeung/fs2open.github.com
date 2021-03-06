/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 



#ifndef _COLLIDESTUFF_H
#define _COLLIDESTUFF_H

#include "globalincs/pstypes.h"
#include "weapon/beam.h"
#include "multithread/multithread.h"

class object;
struct CFILE;
struct mc_info;

//Collision physics constants
#define COLLISION_FRICTION_FACTOR		0.0f	//Default value if not set in ships.tbl
#define COLLISION_ROTATION_FACTOR		0.2f	//Default value if not set in ships.tbl
#define MIN_LANDING_SOUND_VEL			2.0f
#define LANDING_POS_OFFSET				0.05f

//===============================================================================
// GENERAL COLLISION DETECTION HELPER FUNCTIONS 
// These are in CollideGeneral.cpp and are used by one or more of the collision-
// type specific collision modules.
//===============================================================================


#define COLLISION_OF(a,b) (((a)<<8)|(b))

#define COLLISION_TYPE_NONE	0	
#define COLLISION_TYPE_OLD		1	// checks all n objects with each other each frame
#define COLLISION_TYPE_NEW		2	// keeps track of collision pairs.  throws out collisions that won't happen.

extern int collision_type;

#define SUBMODEL_NO_ROT_HIT	0
#define SUBMODEL_ROT_HIT		1

void obj_pairs_close();
void obj_reset_pairs();
void obj_add_pair( object *A, object *B, int check_time = -1, int add_to_end = 0 );

void obj_add_collider(int obj_index);
void obj_remove_collider(int obj_index);
void obj_reset_colliders();

void obj_check_all_collisions();
void obj_sort_and_collide();
void obj_quicksort_colliders(SCP_vector<int> *list, int left, int right, int axis);
void obj_find_overlap_colliders(SCP_vector<int> *overlap_list_out, SCP_vector<int> *list, int axis, bool collide);
//float obj_get_collider_endpoint(int obj_num, int axis, bool min);
void obj_collide_pair(object *A, object *B);

// retimes all collision pairs to be checked (in 25ms by default)
void obj_all_collisions_retime(int checkdly=25);
void obj_collide_retime_cached_pairs(int checkdly=25);

// Returns TRUE if the weapon will never hit the other object.
// If it can it predicts how long until these two objects need
// to be checked and fills the time in in current_pair.
// CODE is locatated in CollideGeneral.cpp
int weapon_will_never_hit( object *weapon, object *other, obj_pair * current_pair );


//	See if two lines intersect by doing recursive subdivision.
//	Bails out if larger distance traveled is less than sum of radii + 1.0f.
// CODE is locatated in CollideGeneral.cpp
int collide_subdivide(vec3d *p0, vec3d *p1, float prad, vec3d *q0, vec3d *q1, float qrad);


//===============================================================================
// SPECIFIC COLLISION DETECTION FUNCTIONS 
//===============================================================================

// Checks weapon-weapon collisions.  pair->a and pair->b are weapons.
// Returns 1 if all future collisions between these can be ignored
// CODE is locatated in CollideWeaponWeapon.cpp
int collide_weapon_weapon( obj_pair * pair );

// Checks ship-weapon collisions.  pair->a is ship and pair->b is weapon.
// Returns 1 if all future collisions between these can be ignored
// CODE is locatated in CollideShipWeapon.cpp
int collide_ship_weapon( obj_pair * pair );
void ship_weapon_do_hit_stuff(object *ship_obj, object *weapon_obj, vec3d *world_hitpos, vec3d *hitpos, int quadrant_num, int submodel_num = -1);

// Checks debris-weapon collisions.  pair->a is debris and pair->b is weapon.
// Returns 1 if all future collisions between these can be ignored
// CODE is locatated in CollideDebrisWeapon.cpp
int collide_debris_weapon( obj_pair * pair );

// Checks debris-ship collisions.  pair->a is debris and pair->b is ship.
// Returns 1 if all future collisions between these can be ignored
// CODE is locatated in CollideDebrisShip.cpp
int collide_debris_ship( obj_pair * pair );

int collide_asteroid_ship(obj_pair *pair);
int collide_asteroid_weapon(obj_pair *pair);

// Checks ship-ship collisions.  pair->a and pair->b are ships.
// Returns 1 if all future collisions between these can be ignored
// CODE is locatated in CollideShipShip.cpp
int collide_ship_ship( obj_pair * pair );

//	Predictive functions.
//	Returns true if vector from curpos to goalpos with radius radius will collide with object goalobjp
int pp_collide(vec3d *curpos, vec3d *goalpos, object *goalobjp, float radius);

//	Return true if objp will collide with some large ship if it moves distance distance.
int collide_predict_large_ship(object *objp, float distance);

// function to remove old weapons when no more weapon slots available.
int collide_remove_weapons(void);

void collide_ship_ship_do_sound(vec3d *world_hit_pos, object *A, object *B, int player_involved);
void collide_ship_ship_sounds_init();

int get_ship_quadrant_from_global(vec3d *global_pos, object *objp);

int reject_due_collision_groups(object *A, object *B);

void obj_collide_pair(object *A, object *B);

inline float obj_get_collider_endpoint(int obj_num, int axis, bool min)
{
  if ( Objects[obj_num].type == OBJ_BEAM ) {
    beam *b = &Beams[Objects[obj_num].instance];

    // use the last start and last shot as endpoints
    float min_end, max_end;
    if ( b->last_start.a1d[axis] > b->last_shot.a1d[axis] ) {
      min_end = b->last_shot.a1d[axis];
      max_end = b->last_start.a1d[axis];
    } else {
      min_end = b->last_start.a1d[axis];
      max_end = b->last_shot.a1d[axis];
    }

    if ( min ) {
      return min_end;
    } else {
      return max_end;
    }
  } else if ( Objects[obj_num].type == OBJ_WEAPON ) {
    float min_end, max_end;

    if ( Objects[obj_num].pos.a1d[axis] > Objects[obj_num].last_pos.a1d[axis] ) {
      min_end = Objects[obj_num].last_pos.a1d[axis];
      max_end = Objects[obj_num].pos.a1d[axis];
    } else {
      min_end = Objects[obj_num].pos.a1d[axis];
      max_end = Objects[obj_num].last_pos.a1d[axis];
    }

    if ( min ) {
      return min_end - Objects[obj_num].radius;
    } else {
      return max_end + Objects[obj_num].radius;
    }
  } else {
    vec3d *pos = &Objects[obj_num].pos;

    if ( min ) {
      return pos->a1d[axis] - Objects[obj_num].radius;
    } else {
      return pos->a1d[axis] + Objects[obj_num].radius;
    }
  }
}

// returns true if we should reject object pair if one is child of other.
inline bool reject_obj_pair_on_parent(object *A, object *B)
{
	if((A->type == OBJ_SHIP) && (B->type == OBJ_DEBRIS) && (B->parent_sig == A->signature)) {
		return false;
	}

	if((B->type == OBJ_SHIP) && (A->type == OBJ_DEBRIS) && (A->parent_sig == B->signature)) {
		return false;
	}

	if ((A->parent_sig == B->signature) || (B->parent_sig == A->signature)) {
		return true;
	}

	return false;
}


#endif

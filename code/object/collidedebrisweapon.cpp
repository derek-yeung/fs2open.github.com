/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 



#include "object/objcollide.h"
#include "object/object.h"
#include "asteroid/asteroid.h"
#include "debris/debris.h"
#include "weapon/weapon.h"
#include "math/fvi.h"
#include "parse/scripting.h"



// placeholder struct for ship_debris collisions
typedef struct ship_weapon_debris_struct {
	object	*ship_object;
	object	*debris_object;
	vec3d	ship_collision_cm_pos;
	vec3d	r_ship;
	vec3d	collision_normal;
	int		shield_hit_tri;
	vec3d	shield_hit_tri_point;
	float		impulse;
} ship_weapon_debris_struct;


/**
 * Checks debris-weapon collisions.  
 * @param pair obj_pair pointer to the two objects. pair->a is debris and pair->b is weapon.
 * @return 1 if all future collisions between these can be ignored
 */
int collide_debris_weapon( obj_pair * pair )
{
	vec3d	hitpos;
	int		hit;
	object *pdebris = pair->a;
	object *weapon = pair->b;

	Assert( pdebris->type == OBJ_DEBRIS );
	Assert( weapon->type == OBJ_WEAPON );

	// first check the bounding spheres of the two objects.
	hit = fvi_segment_sphere(&hitpos, &weapon->last_pos, &weapon->pos, &pdebris->pos, pdebris->radius);
	if (hit) {
		hit = debris_check_collision(pdebris, weapon, &hitpos );
		if ( !hit )
			return 0;

		Script_system.SetHookObjects(4, "Weapon", weapon, "Debris", pdebris, "Self",weapon, "Object", pdebris);
		bool weapon_override = Script_system.IsConditionOverride(CHA_COLLIDEDEBRIS, weapon);

		Script_system.SetHookObjects(2, "Self",pdebris, "Object", weapon);
		bool debris_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pdebris);

		if(!weapon_override && !debris_override)
		{
			weapon_hit( weapon, pdebris, &hitpos );
			debris_hit( pdebris, weapon, &hitpos, Weapon_info[Weapons[weapon->instance].weapon_info_index].damage );
		}

		Script_system.SetHookObjects(2, "Self",weapon, "Object", pdebris);
		if(!(debris_override && !weapon_override))
			Script_system.RunCondition(CHA_COLLIDEDEBRIS, '\0', NULL, weapon);

		Script_system.SetHookObjects(2, "Self",pdebris, "Object", weapon);
		if((debris_override && !weapon_override) || (!debris_override && !weapon_override))
			Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pdebris);

		Script_system.RemHookVars(4, "Weapon", "Debris", "Self","ObjectB");
		return 0;

	} else {
		return weapon_will_never_hit( weapon, pdebris, pair );
	}
}				



/**
 * Checks debris-weapon collisions.  
 * @param pair obj_pair pointer to the two objects. pair->a is debris and pair->b is weapon.
 * @return 1 if all future collisions between these can be ignored
 */
int collide_asteroid_weapon( obj_pair * pair )
{
	if (!Asteroids_enabled)
		return 0;

	vec3d	hitpos;
	int		hit;
	object	*pasteroid = pair->a;
	object	*weapon = pair->b;

	Assert( pasteroid->type == OBJ_ASTEROID);
	Assert( weapon->type == OBJ_WEAPON );

	// first check the bounding spheres of the two objects.
	hit = fvi_segment_sphere(&hitpos, &weapon->last_pos, &weapon->pos, &pasteroid->pos, pasteroid->radius);
	if (hit) {
		hit = asteroid_check_collision(pasteroid, weapon, &hitpos );
		if ( !hit )
			return 0;

		Script_system.SetHookObjects(4, "Weapon", weapon, "Asteroid", pasteroid, "Self",weapon, "Object", pasteroid);

		bool weapon_override = Script_system.IsConditionOverride(CHA_COLLIDEASTEROID, weapon);
		Script_system.SetHookObjects(2, "Self",pasteroid, "Object", weapon);
		bool asteroid_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pasteroid);

		if(!weapon_override && !asteroid_override)
		{
			weapon_hit( weapon, pasteroid, &hitpos );
			asteroid_hit( pasteroid, weapon, &hitpos, Weapon_info[Weapons[weapon->instance].weapon_info_index].damage );
		}

		Script_system.SetHookObjects(2, "Self",weapon, "Object", pasteroid);
		if(!(asteroid_override && !weapon_override))
			Script_system.RunCondition(CHA_COLLIDEASTEROID, '\0', NULL, weapon);

		Script_system.SetHookObjects(2, "Self",pasteroid, "Object", weapon);
		if((asteroid_override && !weapon_override) || (!asteroid_override && !weapon_override))
			Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pasteroid);

		Script_system.RemHookVars(4, "Weapon", "Asteroid", "Self","ObjectB");
		return 0;

	} else {
		return weapon_will_never_hit( weapon, pasteroid, pair );
	}
}				

void collide_debris_weapon_exec(obj_pair *pair, collision_exec_data *data)
{
	if (!debris_check_collision(pair->a, pair->b, &data->misc.hitpos))
		return;

	Script_system.SetHookObjects(4, "Weapon", pair->b, "Debris", pair->a, "Self", pair->b, "Object", pair->a);
	bool weapon_override = Script_system.IsConditionOverride(CHA_COLLIDEDEBRIS, pair->b);

	Script_system.SetHookObjects(2, "Self", pair->a, "Object", pair->b);
	bool debris_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pair->a);

	if (!weapon_override && !debris_override) {
		weapon_hit(pair->b, pair->a, &data->misc.hitpos);
		debris_hit(pair->a, pair->b, &data->misc.hitpos, Weapon_info[Weapons[pair->b->instance].weapon_info_index].damage);
	}

	Script_system.SetHookObjects(2, "Self", pair->b, "Object", pair->a);
	if (!(debris_override && !weapon_override))
		Script_system.RunCondition(CHA_COLLIDEDEBRIS, '\0', NULL, pair->b);

	Script_system.SetHookObjects(2, "Self", pair->a, "Object", pair->b);
	if ((debris_override && !weapon_override) || (!debris_override && !weapon_override))
		Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pair->a);

	Script_system.RemHookVars(4, "Weapon", "Debris", "Self", "ObjectB");
}

collision_result collide_debris_weapon_eval(obj_pair *pair, collision_exec_data *data)
{
	Assert(pair->a->type == OBJ_DEBRIS);
	Assert(pair->b->type == OBJ_WEAPON);

	// first check the bounding spheres of the two objects.
	if (fvi_segment_sphere(&data->misc.hitpos, &pair->b->last_pos, &pair->b->pos, &pair->a->pos, pair->a->radius)) {
		return COLLISION_RESULT_COLLISION;
	} else {
		return (weapon_will_never_hit(pair->b, pair->a, pair)) ? COLLISION_RESULT_NEVER : COLLISION_RESULT_NO_COLLISION;
	}
}

void collide_asteroid_weapon_exec(obj_pair *pair, collision_exec_data *data)
{
	if (!asteroid_check_collision(pair->a, pair->b, &data->misc.hitpos))
		return;

	Script_system.SetHookObjects(4, "Weapon", pair->b, "Asteroid", pair->a, "Self", pair->b, "Object", pair->a);

	bool weapon_override = Script_system.IsConditionOverride(CHA_COLLIDEASTEROID, pair->b);
	Script_system.SetHookObjects(2, "Self", pair->a, "Object", pair->b);
	bool asteroid_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pair->a);

	if (!weapon_override && !asteroid_override) {
		weapon_hit(pair->b, pair->a, &data->misc.hitpos);
		asteroid_hit(pair->a, pair->b, &data->misc.hitpos, Weapon_info[Weapons[pair->b->instance].weapon_info_index].damage);
	}

	Script_system.SetHookObjects(2, "Self", pair->b, "Object", pair->a);
	if (!(asteroid_override && !weapon_override))
		Script_system.RunCondition(CHA_COLLIDEASTEROID, '\0', NULL, pair->b);

	Script_system.SetHookObjects(2, "Self", pair->a, "Object", pair->b);
	if ((asteroid_override && !weapon_override) || (!asteroid_override && !weapon_override))
		Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pair->a);

	Script_system.RemHookVars(4, "Weapon", "Asteroid", "Self", "ObjectB");
}

collision_result collide_asteroid_weapon_eval(obj_pair *pair, collision_exec_data *data)
{
	if (!Asteroids_enabled)
		return COLLISION_RESULT_NEVER;

	Assert(pair->a->type == OBJ_ASTEROID);
	Assert(pair->b->type == OBJ_WEAPON);

	// first check the bounding spheres of the two objects.
	if (fvi_segment_sphere(&data->misc.hitpos, &pair->b->last_pos, &pair->b->pos, &pair->a->pos, pair->a->radius)) {
		return COLLISION_RESULT_COLLISION;
	} else {
		return (weapon_will_never_hit(pair->b, pair->a, pair)) ? COLLISION_RESULT_NEVER : COLLISION_RESULT_NO_COLLISION;
	}
}

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
#include "weapon/weapon.h"
#include "ship/ship.h"
#include "parse/lua.h"
#include "parse/scripting.h"
#include "freespace2/freespace.h"
#include "stats/scoring.h"
#include "network/multi.h"


/**
 * Checks weapon-weapon collisions.  
 * @param pair obj_pair pointer to the two objects. pair->a and pair->b are weapons.
 * @return 1 if all future collisions between these can be ignored
 */
int collide_weapon_weapon( obj_pair * pair )
{
	float A_radius, B_radius;
	object *A = pair->a;
	object *B = pair->b;

	Assert( A->type == OBJ_WEAPON );
	Assert( B->type == OBJ_WEAPON );
	
	//	Don't allow ship to shoot down its own missile.
	if (A->parent_sig == B->parent_sig)
		return 1;

	//	Only shoot down teammate's missile if not traveling in nearly same direction.
	if (Weapons[A->instance].team == Weapons[B->instance].team)
		if (vm_vec_dot(&A->orient.vec.fvec, &B->orient.vec.fvec) > 0.7f)
			return 1;

	//	Ignore collisions involving a bomb if the bomb is not yet armed.
	weapon	*wpA, *wpB;
	weapon_info	*wipA, *wipB;

	wpA = &Weapons[A->instance];
	wpB = &Weapons[B->instance];
	wipA = &Weapon_info[wpA->weapon_info_index];
	wipB = &Weapon_info[wpB->weapon_info_index];

	A_radius = A->radius;
	B_radius = B->radius;

	if (wipA->weapon_hitpoints > 0) {
		if (!(wipA->wi_flags2 & WIF2_HARD_TARGET_BOMB)) {
			A_radius *= 2;		// Makes bombs easier to hit
		}
		
		if (wipA->wi_flags & WIF_LOCKED_HOMING) {
			if ( (wipA->max_lifetime - wpA->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level] )
				return 0;
		}
		else if ( (wipA->lifetime - wpA->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level] )
			return 0;
	}

	if (wipB->weapon_hitpoints > 0) {
		if (!(wipB->wi_flags2 & WIF2_HARD_TARGET_BOMB)) {
			B_radius *= 2;		// Makes bombs easier to hit
		}
		if (wipB->wi_flags & WIF_LOCKED_HOMING) {
			if ( (wipB->max_lifetime - wpB->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level] )
				return 0;
		}
		else if ( (wipB->lifetime - wpB->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level] )
			return 0;
	}

	//	Rats, do collision detection.
	if (collide_subdivide(&A->last_pos, &A->pos, A_radius, &B->last_pos, &B->pos, B_radius))
	{
		Script_system.SetHookObjects(4, "Weapon", A, "WeaponB", B, "Self",A, "Object", B);
		bool a_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, A);
		
		//Should be reversed
		Script_system.SetHookObjects(4, "Weapon", B, "WeaponB", A, "Self",B, "Object", A);
		bool b_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, B);

		if(!a_override && !b_override)
		{
			float aDamage = wipA->damage;
			if (wipB->armor_type_idx >= 0)
				aDamage = Armor_types[wipB->armor_type_idx].GetDamage(aDamage, wipA->damage_type_idx, 1.0f);

			float bDamage = wipB->damage;
			if (wipA->armor_type_idx >= 0)
				bDamage = Armor_types[wipA->armor_type_idx].GetDamage(bDamage, wipB->damage_type_idx, 1.0f);

			if (wipA->weapon_hitpoints > 0) {
				if (wipB->weapon_hitpoints > 0) {		//	Two bombs collide, detonate both.
					if ((wipA->wi_flags & WIF_BOMB) && (wipB->wi_flags & WIF_BOMB)) {
						Weapons[A->instance].lifeleft = 0.01f;
						Weapons[B->instance].lifeleft = 0.01f;
						Weapons[A->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
						Weapons[B->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					} else {
						A->hull_strength -= bDamage;
						B->hull_strength -= aDamage;

						// safety to make sure either of the weapons die - allow 'bulkier' to keep going
						if ((A->hull_strength > 0.0f) && (B->hull_strength > 0.0f)) {
							if (wipA->weapon_hitpoints > wipB->weapon_hitpoints) {
								B->hull_strength = -1.0f;
							} else {
								A->hull_strength = -1.0f;
							}
						}
						
						if (A->hull_strength < 0.0f) {
							Weapons[A->instance].lifeleft = 0.01f;
							Weapons[A->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
						}
						if (B->hull_strength < 0.0f) {
							Weapons[B->instance].lifeleft = 0.01f;
							Weapons[B->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
						}
					}
				} else {
					A->hull_strength -= bDamage;
					Weapons[B->instance].lifeleft = 0.01f;
					Weapons[B->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					if (A->hull_strength < 0.0f) {
						Weapons[A->instance].lifeleft = 0.01f;
						Weapons[A->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					}
				}
			} else if (wipB->weapon_hitpoints > 0) {
				B->hull_strength -= aDamage;
				Weapons[A->instance].lifeleft = 0.01f;
				Weapons[A->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
				if (B->hull_strength < 0.0f) {
					Weapons[B->instance].lifeleft = 0.01f;
					Weapons[B->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
				}
			}

			// single player and multiplayer masters evaluate the scoring and kill stuff
			if (!MULTIPLAYER_CLIENT) {

				//Save damage for bomb so we can do scoring once it's destroyed. -Halleck
				if (wipA->wi_flags & WIF_BOMB) {
					scoring_add_damage_to_weapon(A, B, wipB->damage);
					//Update stats. -Halleck
					scoring_eval_hit(A, B, 0);
				}
				if (wipB->wi_flags & WIF_BOMB) {
					scoring_add_damage_to_weapon(B, A, wipA->damage);
					//Update stats. -Halleck
					scoring_eval_hit(B, A, 0);
				}
			}

	#ifndef NDEBUG
			float dist = 0.0f;

			if (Weapons[A->instance].lifeleft == 0.01f) {
				dist = vm_vec_dist_quick(&A->pos, &wpA->homing_pos);
			}
			if (Weapons[B->instance].lifeleft == 0.01f) {
				dist = vm_vec_dist_quick(&B->pos, &wpB->homing_pos);
			}
	#endif
		}

		if(!(b_override && !a_override))
		{
			Script_system.SetHookObjects(4, "Weapon", A, "WeaponB", B, "Self",A, "Object", B);
			Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, A, wpA->weapon_info_index);
		}
		if((b_override && !a_override) || (!b_override && !a_override))
		{
			//Should be reversed
			Script_system.SetHookObjects(4, "Weapon", B, "WeaponB", A, "Self",B, "Object", A);
			Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, B, wpB->weapon_info_index);
		}

		Script_system.RemHookVars(4, "Weapon", "WeaponB", "Self","ObjectB");
		return 1;
	}

	return 0;
}

void collide_weapon_weapon_exec(obj_pair * pair, collision_exec_data *data)
{
	Script_system.SetHookObjects(4, "Weapon", pair->a, "WeaponB", pair->b, "Self", pair->a, "Object", pair->b);
	bool a_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pair->a);

	//Should be reversed
	Script_system.SetHookObjects(4, "Weapon", pair->b, "WeaponB", pair->a, "Self", pair->b, "Object", pair->a);
	bool b_override = Script_system.IsConditionOverride(CHA_COLLIDEWEAPON, pair->b);

	if (!a_override && !b_override) {
		float aDamage = data->weapon_weapon.wipA->damage;
		if (data->weapon_weapon.wipB->armor_type_idx >= 0)
			aDamage = Armor_types[data->weapon_weapon.wipB->armor_type_idx].GetDamage(aDamage, data->weapon_weapon.wipA->damage_type_idx, 1.0f);

		float bDamage = data->weapon_weapon.wipB->damage;
		if (data->weapon_weapon.wipA->armor_type_idx >= 0)
			bDamage = Armor_types[data->weapon_weapon.wipA->armor_type_idx].GetDamage(bDamage, data->weapon_weapon.wipB->damage_type_idx, 1.0f);

		if (data->weapon_weapon.wipA->weapon_hitpoints > 0) {
			if (data->weapon_weapon.wipB->weapon_hitpoints > 0) {		//	Two bombs collide, detonate both.
				if ((data->weapon_weapon.wipA->wi_flags & WIF_BOMB) && (data->weapon_weapon.wipB->wi_flags & WIF_BOMB)) {
					Weapons[pair->a->instance].lifeleft = 0.01f;
					Weapons[pair->b->instance].lifeleft = 0.01f;
					Weapons[pair->a->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					Weapons[pair->b->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
				} else {
					pair->a->hull_strength -= bDamage;
					pair->b->hull_strength -= aDamage;

					// safety to make sure either of the weapons die - allow 'bulkier' to keep going
					if ((pair->a->hull_strength > 0.0f) && (pair->b->hull_strength > 0.0f)) {
						if (data->weapon_weapon.wipA->weapon_hitpoints > data->weapon_weapon.wipB->weapon_hitpoints) {
							pair->b->hull_strength = -1.0f;
						} else {
							pair->a->hull_strength = -1.0f;
						}
					}

					if (pair->a->hull_strength < 0.0f) {
						Weapons[pair->a->instance].lifeleft = 0.01f;
						Weapons[pair->a->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					}
					if (pair->b->hull_strength < 0.0f) {
						Weapons[pair->b->instance].lifeleft = 0.01f;
						Weapons[pair->b->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
					}
				}
			} else {
				pair->a->hull_strength -= bDamage;
				Weapons[pair->b->instance].lifeleft = 0.01f;
				Weapons[pair->b->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
				if (pair->a->hull_strength < 0.0f) {
					Weapons[pair->a->instance].lifeleft = 0.01f;
					Weapons[pair->a->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
				}
			}
		} else if (data->weapon_weapon.wipB->weapon_hitpoints > 0) {
			pair->b->hull_strength -= aDamage;
			Weapons[pair->a->instance].lifeleft = 0.01f;
			Weapons[pair->a->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
			if (pair->b->hull_strength < 0.0f) {
				Weapons[pair->b->instance].lifeleft = 0.01f;
				Weapons[pair->b->instance].weapon_flags |= WF_DESTROYED_BY_WEAPON;
			}
		}

		// single player and multiplayer masters evaluate the scoring and kill stuff
		if (!MULTIPLAYER_CLIENT) {

			//Save damage for bomb so we can do scoring once it's destroyed. -Halleck
			if (data->weapon_weapon.wipA->wi_flags & WIF_BOMB) {
				scoring_add_damage_to_weapon(pair->a, pair->b, data->weapon_weapon.wipB->damage);
				//Update stats. -Halleck
				scoring_eval_hit(pair->a, pair->b, 0);
			}
			if (data->weapon_weapon.wipB->wi_flags & WIF_BOMB) {
				scoring_add_damage_to_weapon(pair->b, pair->a, data->weapon_weapon.wipA->damage);
				//Update stats. -Halleck
				scoring_eval_hit(pair->b, pair->a, 0);
			}
		}
	}

	if (!(b_override && !a_override)) {
		Script_system.SetHookObjects(4, "Weapon", pair->a, "WeaponB", pair->b, "Self", pair->a, "Object", pair->b);
		Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pair->a, Weapons[pair->a->instance].weapon_info_index);
	}
	if ((b_override && !a_override) || (!b_override && !a_override)) {
		//Should be reversed
		Script_system.SetHookObjects(4, "Weapon", pair->b, "WeaponB", pair->a, "Self", pair->b, "Object", pair->a);
		Script_system.RunCondition(CHA_COLLIDEWEAPON, '\0', NULL, pair->b, Weapons[pair->b->instance].weapon_info_index);
	}

	Script_system.RemHookVars(4, "Weapon", "WeaponB", "Self", "ObjectB");
}

/**
 * Checks weapon-weapon collisions.
 * @param pair obj_pair pointer to the two objects. pair->a and pair->b are weapons.
 * @return 1 if all future collisions between these can be ignored
 */
collision_result collide_weapon_weapon_eval(obj_pair *pair, collision_exec_data *data)
{
	float A_radius, B_radius;

	Assert(pair->a->type == OBJ_WEAPON);
	Assert(pair->b->type == OBJ_WEAPON);

	//	Don't allow ship to shoot down its own missile.
	if (pair->a->parent_sig == pair->b->parent_sig)
		return COLLISION_RESULT_NEVER;

	//	Only shoot down teammate's missile if not traveling in nearly same direction.
	if (Weapons[pair->a->instance].team == Weapons[pair->b->instance].team)
		if (vm_vec_dot(&pair->a->orient.vec.fvec, &pair->b->orient.vec.fvec) > 0.7f)
			return COLLISION_RESULT_NEVER;

	//	Ignore collisions involving a bomb if the bomb is not yet armed.
	weapon *wpA, *wpB;

	wpA = &Weapons[pair->a->instance];
	wpB = &Weapons[pair->b->instance];
	data->weapon_weapon.wipA = &Weapon_info[wpA->weapon_info_index];
	data->weapon_weapon.wipB = &Weapon_info[wpB->weapon_info_index];

	A_radius = pair->a->radius;
	B_radius = pair->b->radius;

	if (data->weapon_weapon.wipA->weapon_hitpoints > 0) {
		if (!(data->weapon_weapon.wipA->wi_flags2 & WIF2_HARD_TARGET_BOMB)) {
			A_radius *= 2;		// Makes bombs easier to hit
		}

		if (data->weapon_weapon.wipA->wi_flags & WIF_LOCKED_HOMING) {
			if ((data->weapon_weapon.wipA->max_lifetime - wpA->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level])
				return COLLISION_RESULT_NO_COLLISION;
		} else if ((data->weapon_weapon.wipA->lifetime - wpA->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level])
			return COLLISION_RESULT_NO_COLLISION;
	}

	if (data->weapon_weapon.wipB->weapon_hitpoints > 0) {
		if (!(data->weapon_weapon.wipB->wi_flags2 & WIF2_HARD_TARGET_BOMB)) {
			B_radius *= 2;		// Makes bombs easier to hit
		}
		if (data->weapon_weapon.wipB->wi_flags & WIF_LOCKED_HOMING) {
			if ((data->weapon_weapon.wipB->max_lifetime - wpB->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level])
				return COLLISION_RESULT_NO_COLLISION;
		} else if ((data->weapon_weapon.wipB->lifetime - wpB->lifeleft) < The_mission.ai_profile->delay_bomb_arm_timer[Game_skill_level])
			return COLLISION_RESULT_NO_COLLISION;
	}

	//	Rats, do collision detection.
	if (collide_subdivide(&pair->a->last_pos, &pair->a->pos, A_radius, &pair->b->last_pos, &pair->b->pos, B_radius)) {

		return COLLISION_RESULT_COLLISION;
	}

	return COLLISION_RESULT_NO_COLLISION;
}

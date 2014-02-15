#ifndef _MULTITHREAD_H
#define _MULTITHREAD_H

#include "object/object.h"
#include "object/objcollide.h"
#include "SDL.h"
#include "limits.h"
#include "weapon/weapon.h"
#include "weapon/beam.h"

#define MULTITHREADING_ENABLED
#define MAX_THREADS											256

#define THREAD_WAIT											-1
#define THREAD_EXIT											-2

#ifdef MULTITHREADING_ENABLED
#define OPENGL_LOCK											{SDL_LockMutex(render_mutex);}
#define OPENGL_UNLOCK										{SDL_UnlockMutex(render_mutex);}
#define G3_COUNT_LOCK										{SDL_LockMutex(g3_count_mutex);}
#define G3_COUNT_UNLOCK										{SDL_UnlockMutex(g3_count_mutex);}
#define HOOK_LOCK											{SDL_LockMutex(hook_mutex);}
#define HOOK_UNLOCK											{SDL_UnlockMutex(hook_mutex);}
#define BEAM_COLLISION_LOCK									{SDL_LockMutex(beam_collision_mutex);}
#define BEAM_COLLISION_UNLOCK								{SDL_UnlockMutex(beam_collision_mutex);}
#define BEAM_LIGHT_LOCK										{SDL_LockMutex(beam_light_mutex);}
#define BEAM_LIGHT_UNLOCK									{SDL_UnlockMutex(beam_light_mutex);}
#define SHIP_LOCK											{SDL_LockMutex(ship_mutex);}
#define SHIP_UNLOCK											{SDL_UnlockMutex(ship_mutex);}
#else
#define OPENGL_LOCK
#define OPENGL_UNLOCK
#define G3_COUNT_LOCK
#define G3_COUNT_UNLOCK
#define HOOK_LOCK
#define HOOK_UNLOCK
#define BEAM_COLLISION_LOCK
#define BEAM_COLLISION_UNLOCK
#define BEAM_LIGHT_LOCK
#define BEAM_LIGHT_UNLOCK
#define SHIP_LOCK
#define SHIP_UNLOCK
#endif

typedef enum
{
	COLLISION_RESULT_UNEVALUATED = -2,
	COLLISION_RESULT_NEVER = -1,
	COLLISION_RESULT_COLLISION = 0,
	COLLISION_RESULT_NO_COLLISION,
	COLLISION_RESULT_INVALID = INT_MAX
} collision_result;

typedef enum
{
	PROCESS_STATE_UNPROCESSED,
	PROCESS_STATE_BUSY,
	PROCESS_STATE_COLLIDED,
	PROCESS_STATE_EXECUTED,
	PROCESS_STATE_INVALID
} process_state;

typedef struct
{
	SDL_Thread *thread;
	SDL_mutex *mutex;
	SDL_cond *condition;
} thread_condition;

// Keeps track of pairs of objects for collision detection
typedef struct obj_pair	{
	object *a;
	object *b;
	int (*check_collision)( obj_pair * pair );
	int	next_check_time;	// a timestamp that when elapsed means to check for a collision
	struct obj_pair *next;
} obj_pair;

class collider_pair
{
public:
	object *a;
	object *b;
	int signature_a;
	int signature_b;
	int next_check_time;
	bool initialized;

	// we need to define a constructor because the hash map can
	// implicitly insert an object when we use the [] operator
	collider_pair()
		: a(NULL), b(NULL), signature_a(-1), signature_b(-1), next_check_time(-1), initialized(false)
	{}
};

// used for ship:ship and ship:debris
typedef struct {
	object	*heavy;
	object	*light;
	vec3d	heavy_collision_cm_pos;	// should be zero
	vec3d	light_collision_cm_pos;	// relative cm collision pos
	vec3d	r_heavy;						// relative to A
	vec3d	r_light;						// relative to B
	vec3d	hit_pos;					// relative hit position in A's rf (r_heavy)
	vec3d	collision_normal;		// normal outward from heavy
	float		hit_time;				// time normalized [0,1] when sphere hits model
	float		impulse;					// damage scales according to impulse
	vec3d	light_rel_vel;			// velocity of light relative to heavy before collison
	int		collide_rotate;		// if collision is detected purely from rotation
	int		submodel_num;			// submodel of heavy object that is hit
	int		edge_hit;				// if edge is hit, need to change collision normal
	int		submodel_rot_hit;		// if collision is against rotating submodel
	bool	is_landing;			//SUSHI: Maybe treat current collision as a landing
} collision_info_struct;

typedef struct
{
	weapon *wp;
	mc_info mc;
	int quadrant_num;
} ship_weapon_exec;

typedef struct
{
	beam *b;
	mc_info mc_entry;
	mc_info mc_exit;
	int quadrant_num;
	bool hull_exit_collision;
} beam_ship_exec;

typedef struct
{
	beam *b;
	mc_info test_collide;
} beam_misc_exec;

typedef struct
{
	bool player_involved;
	bool planet_collision;
	vec3d world_hit_pos;
	collision_info_struct ship_ship_hit_info;
} ship_ship_exec;

typedef struct
{
	weapon_info *wipA, *wipB;
} weapon_weapon_exec;

typedef struct
{
	vec3d hitpos;
	collision_info_struct misc_hit_info;
} misc_exec;

typedef union
{
	ship_weapon_exec ship_weapon;
	beam_ship_exec beam_ship;
	beam_misc_exec beam_misc;
	ship_ship_exec ship_ship;
	weapon_weapon_exec weapon_weapon;
	misc_exec misc;
} collision_exec_data;

typedef collision_result (*collision_eval_func)(obj_pair *, collision_exec_data *);
typedef void (*collision_exec_func)(obj_pair *, collision_exec_data *);
typedef int (*collision_fallback)( obj_pair *pair );

typedef struct
{
	obj_pair objs;
	int signature_a;
	int signature_b;
	unsigned char processed;
	bool in_use;
	collision_result result;
	collision_eval_func eval_func;
	collision_exec_data *exec_data;
	collision_exec_func exec_func;
} collision_data;

typedef struct
{
	collision_data *collision;
	//copy of object pointers for reading
//	object *a;
//	object *b;
} thread_vars;


extern SDL_mutex *render_mutex;
extern SDL_mutex *g3_count_mutex;
extern SDL_mutex *hook_mutex;
extern SDL_mutex *beam_collision_mutex;
extern SDL_mutex *beam_light_mutex;
extern SDL_mutex *ship_mutex;
extern bool threads_alive;
extern SCP_hash_map<unsigned int, collision_data> collision_cache;

//extern collision_data collision_cache[MAX_OBJECTS * MAX_OBJECTS];

void create_threads();
void destroy_threads();

void evaluate_collisions();
void execute_collisions();

/**
 * We do not expect this function to run inside a thread
 * @param object_1
 * @param object_2
 */
void collision_pair_add(object *object_1, object *object_2);

void collision_pair_clear();

void set_hit_struct_info(collision_info_struct *hit, mc_info *mc, int submodel_rot_hit);

void init_collision_info_struct(collision_info_struct *cis);

collision_result collide_ship_ship_eval(obj_pair * pair, collision_exec_data *data);
void collide_ship_ship_exec(obj_pair * pair, collision_exec_data *data);

collision_result collide_weapon_weapon_eval(obj_pair *pair, collision_exec_data *data);
void collide_weapon_weapon_exec(obj_pair * pair, collision_exec_data *data);

collision_result collide_ship_weapon_eval(obj_pair * pair, collision_exec_data *data);
void collide_ship_weapon_exec(obj_pair * pair, collision_exec_data *data);

collision_result collide_debris_ship_eval(obj_pair *pair, collision_exec_data *data);
void collide_debris_ship_exec(obj_pair *pair, collision_exec_data *data);

collision_result collide_asteroid_ship_eval(obj_pair *pair, collision_exec_data *data);
void collide_asteroid_ship_exec(obj_pair *pair, collision_exec_data *data);

collision_result collide_debris_weapon_eval(obj_pair *pair, collision_exec_data *data);
void collide_debris_weapon_exec(obj_pair *pair, collision_exec_data *data);

collision_result collide_asteroid_weapon_eval(obj_pair *pair, collision_exec_data *data);
void collide_asteroid_weapon_exec(obj_pair *pair, collision_exec_data *data);

collision_result beam_collide_ship_eval(obj_pair *pair, collision_exec_data *data);
void beam_collide_ship_exec(obj_pair *pair, collision_exec_data *data);

collision_result beam_collide_misc_eval(obj_pair *pair, collision_exec_data *data);
void beam_collide_asteroid_exec(obj_pair *pair, collision_exec_data *data);
void beam_collide_missile_exec(obj_pair *pair, collision_exec_data *data);
void beam_collide_debris_exec(obj_pair *pair, collision_exec_data *data);
#endif

#ifndef _MULTITHREAD_H
#define _MULTITHREAD_H

#include "object/object.h"
#include "object/objcollide.h"
#include "SDL.h"
#include "limits.h"
#include "weapon/weapon.h"

#define MULTITHREADING_ENABLED
#define MAX_THREADS											256

#define SAFETY_TIME											2

#define THREAD_WAIT											-1
#define THREAD_EXIT											-2

#ifdef MULTITHREADING_ENABLED
#define OPENGL_LOCK											{SDL_LockMutex(render_mutex);}
#define OPENGL_UNLOCK										{SDL_UnlockMutex(render_mutex);}
#define G3_COUNT_LOCK										{SDL_LockMutex(g3_count_mutex);}
#define G3_COUNT_UNLOCK										{SDL_UnlockMutex(g3_count_mutex);}
#define HOOK_LOCK											{SDL_LockMutex(hook_mutex);}
#define HOOK_UNLOCK											{SDL_UnlockMutex(hook_mutex);}
#else
#define OPENGL_LOCK
#define OPENGL_UNLOCK
#define G3_COUNT_LOCK
#define G3_COUNT_UNLOCK
#define HOOK_LOCK
#define HOOK_UNLOCK
#endif

typedef enum
{
	COLLISION_RESULT_NEVER = -1,
	COLLISION_RESULT_COLLISION = 0,
	COLLISION_RESULT_NO_COLLISION,
	COLLISION_RESULT_INVALID = INT_MAX
} collision_result;

typedef enum
{
	PROCESS_STATE_UNPROCESSED,
	PROCESS_STATE_BUSY,
	PROCESS_STATE_FINISHED,
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

typedef struct
{
	weapon *wp;
	mc_info *mc;
	int quadrant_num;
} ship_weapon_exec;

typedef struct
{
	collision_result result;
	union
	{
		ship_weapon_exec ship_weapon;
	};
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
	collision_eval_func eval_func;
	collision_exec_data exec_data;
	collision_exec_func exec_func;
} collision_data;

typedef struct
{
	collision_data *collision;
	//copy of object pointers for reading
	object *a;
	object *b;
} thread_vars;


extern SDL_mutex *render_mutex;
extern SDL_mutex *g3_count_mutex;
extern SDL_mutex *hook_mutex;
extern bool threads_alive;

void create_threads();
void destroy_threads();

void execute_collisions();

/**
 * We do not expect this function to run inside a thread
 * @param object_1
 * @param object_2
 */
void collision_pair_add(object *object_1, object *object_2);

void collision_pair_clear();


collision_result collide_ship_weapon_eval(obj_pair * pair, collision_exec_data *data);
void collide_ship_weapon_exec(obj_pair * pair, collision_exec_data *data);

#endif

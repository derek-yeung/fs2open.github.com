#ifndef _MULTITHREAD_H
#define _MULTITHREAD_H

#include "object/object.h"
#include "object/objcollide.h"
#include "SDL.h"
#include "limits.h"

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

typedef struct
{
	SDL_Thread *thread;
	SDL_mutex *mutex;
	SDL_cond *condition;
} thread_condition;

typedef struct
{
	obj_pair objs;
	int signature_a;
	int signature_b;
	unsigned char processed;
	//for future functionality
//	int (*operation_func)(obj_pair);
//	int collision_result;
//	int func_result;
} collision_data;

typedef struct
{
	collision_data *collision;
	//copy of object pointers for reading
	object *a;
	object *b;
} thread_vars;

typedef enum
{
	COLLISION_RESULT_NEVER = -1,
	COLLISION_RESULT_COLLISION = 0,
	COLLISION_RESULT_INVALID = INT_MAX
} collision_result;

typedef enum
{
	PROCESS_STATE_UNPROCESSED,
	PROCESS_STATE_BUSY,
	PROCESS_STATE_FINISHED,
	PROCESS_STATE_INVALID
} process_state;

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

#endif

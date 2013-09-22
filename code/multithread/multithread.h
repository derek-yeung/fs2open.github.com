#ifndef _MULTITHREAD_H
#define _MULTITHREAD_H

#define MULTITHREADING_ENABLED
//#define MULTITHREADING_ENABLED2
//#define MULTITHREADING_ENABLED3
#define MAX_THREADS                                         256

#define SAFETY_TIME                                         5

#define THREAD_WAIT                                         -1
#define THREAD_EXIT                                         -2

typedef struct
{
    pthread_t handle;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} thread_condition;

typedef struct
{
    object *object_1;
    object *object_2;
    unsigned char flags;
    void *operation_func;
} collision_pair;

typedef enum
{
//  THREAD_TYPE_OBJECT_MOVE,
//  THREAD_TYPE_DOCKING,
  THREAD_TYPE_COLLISION,
  THREAD_TYPE_INVALID
} thread_type;

typedef enum
{
  COLLISION_PAIR_FLAG_COLLIDED,
  COLLISION_PAIR_FLAG_RESULT,
  COLLISION_PAIR_FLAG_EXECUTED,
  COLLISION_PAIR_FLAG_INVALID
};


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

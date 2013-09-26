#ifndef _MULTITHREAD_H
#define _MULTITHREAD_H

#define MULTITHREADING_ENABLED
//#define MULTITHREADING_ENABLED2
//#define MULTITHREADING_ENABLED3
#define MAX_THREADS                                         256

#define SAFETY_TIME                                         5

#define THREAD_WAIT                                         -1
#define THREAD_EXIT                                         -2

#define BIT_GET(x, y)                                       (x & (1 << y))
#define BIT_SET(x, y)                                       (x |= (1 << y))
#define BIT_CLEAR(x, y)                                     (x &= !(1 << y))

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
    bool processed;
    bool collided;
    bool executed;
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
  COLLISION_PAIR_PROCESSED,
  COLLISION_PAIR_COLLIDED,
  COLLISION_PAIR_EXECUTED,
  COLLISION_PAIR_INVALID
} collision_pair_flags;

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

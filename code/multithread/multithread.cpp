#include <pthread.h>

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
#include "mission/missionparse.h" //For 2D Mode
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

pthread_attr_t attr;
pthread_mutex_t collision_master_mutex;
pthread_cond_t collision_master_condition;

SCP_vector <collision_pair> collision_list;
SCP_vector <int> thread_number;
SCP_vector <collision_pair> thread_collision_vars;
SCP_vector <thread_condition> conditions;

bool threads_alive = false;

unsigned int executions = 0;

timespec wait_time =
{
    2,
    0
};

void *supercollider_thread(void *obj_collision_vars_ptr);

void create_threads()
{
  int i;
  collision_pair setup_pair;

  threads_alive = true;
  setup_pair.object_1 = NULL;
  setup_pair.object_2 = NULL;
  setup_pair.processed = false;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_mutex_init(&collision_master_mutex, NULL);
  pthread_cond_init(&collision_master_condition, NULL);

  conditions.resize(Cmdline_num_threads);

  //ensure these numbers are filled before creating our threads
  for(i = 0; i < Cmdline_num_threads; i++)
  {
    thread_number.push_back(i);
  }

  for(i = 0; i < Cmdline_num_threads; i++)
  {
    mprintf(("multithread: Creating thread %d\n", i));
    thread_collision_vars.push_back(setup_pair);
    pthread_mutex_init(&(conditions[i].mutex), NULL);
    pthread_cond_init(&(conditions[i].condition), NULL);
    pthread_create(&(conditions[i].handle), &attr, supercollider_thread, &(thread_number[i]));
  }
}

void destroy_threads()
{
  int i;

  threads_alive = false;
  //kill our threads, they shouldn't be doing anything anyway
  for(i = 0; i < Cmdline_num_threads; i++)
  {
    mprintf(("multithread: destroying thread %d\n", i));
    pthread_cancel(conditions[i].handle);
    pthread_mutex_destroy(&(conditions[i].mutex));
    pthread_cond_destroy(&(conditions[i].condition));
  }
  pthread_mutex_destroy(&collision_master_mutex);
  pthread_cond_destroy(&collision_master_condition);

  pthread_attr_destroy(&attr);
}

void collision_pair_clear()
{
  collision_list.clear();
}

void collision_pair_add(object *object_1, object *object_2)
{
  collision_pair pair;

  memset(&pair, 0, sizeof(pair));
  pair.object_1 = object_1;
  pair.object_2 = object_2;

  collision_list.push_back(pair);
}

void execute_collisions()
{
  int i;
  SCP_vector <collision_pair>::iterator it;
  bool assigned_any, assigned_once = false;
  bool done = false;
  bool skip = false;

  time_t start_time = time(NULL);

  mprintf(("multithread: execution %d start\n", executions));

  while(done == false)
  {
    //go through our collision list
    mprintf(("multithread: execution line %d start main loop\n", __LINE__));
    if((time(NULL) - start_time) > SAFETY_TIME)
    {
      mprintf(("multithread: execution %d - STUCK\n", executions));
      Int3();
      for(i = 0; i < Cmdline_num_threads; i++)
      {
        mprintf(("multithread: destroying thread %d\n", i));
        pthread_cancel(conditions[i].handle);
        pthread_mutex_destroy(&(conditions[i].mutex));
        pthread_cond_destroy(&(conditions[i].condition));
      }
      game_shutdown();
      exit(-1);
    }
    mprintf(("multithread: execution line %d - passed time check\n", __LINE__));
    assigned_any = false;
    for(it = collision_list.begin(); it != collision_list.end(); it++)
    {
      mprintf(("multithread: execution line %d - start list loop\n", __LINE__));

      assigned_once = false;
      skip = false;
      if(it->processed == true)
      {
        mprintf(("multithread: execution line %d - skip (already processed)\n", __LINE__));
        continue;
      }
      //skip if the pairs are obviously wrong
      if((it->object_1 == NULL) || (it->object_2 == NULL) || (it->object_1 == it->object_2))
      {
        mprintf(("multithread: execution line %d - invalid objects\n", __LINE__));
        it->processed = true;
        continue;
      }

      //ensure the objects being checked aren't already being used
      //TODO make sure the checking doesn't keep skipping
      for(i = 0; i < Cmdline_num_threads; i++)
      {
        if(
            (it->object_1 == thread_collision_vars[i].object_1) ||
            (it->object_1 == thread_collision_vars[i].object_2) ||
            (it->object_2 == thread_collision_vars[i].object_1) ||
            (it->object_2 == thread_collision_vars[i].object_2)
            )
        {
          skip = true;
          break;
        }
      }

      if(skip == true)
      {
        mprintf(("multithread: execution line %d - skip (busy)\n", __LINE__));
        continue;
      }
      //check for a free thread to handle the collision
      for(i = 0; i < Cmdline_num_threads; i++)
      {
        if(pthread_mutex_trylock(&(conditions[i].mutex)) == 0)
        {
          mprintf(("multithread: execution line %d - processed by thread %d\n", __LINE__, i));
          thread_collision_vars[i].object_1 = it->object_1;
          thread_collision_vars[i].object_2 = it->object_2;
          thread_collision_vars[i].processed = false;
          it->processed = true;


          pthread_cond_signal(&(conditions[i].condition));
          pthread_mutex_unlock(&(conditions[i].mutex));
          assigned_once = true;
          break;
        }
      }

      mprintf(("multithread: execution line %d - middle\n", __LINE__));
      if(assigned_once == false)
      {
        mprintf(("multithread: execution %d - NONE_FREE\n", executions));
//        Int3();
        //if there weren't any free threads, wait until one is free - if we wait too long, skip
        if(pthread_mutex_timedlock(&collision_master_mutex, &wait_time) == 0)
        {
          pthread_cond_wait(&collision_master_condition, &collision_master_mutex);

          //then check again
          for(i = 0; i < Cmdline_num_threads; i++)
          {
            if(pthread_mutex_trylock(&(conditions[i].mutex)) == 0)
            {
              mprintf(("multithread: execution line %d - processed by thread %d\n", __LINE__, i));
              thread_collision_vars[i].object_1 = it->object_1;
              thread_collision_vars[i].object_2 = it->object_2;
              thread_collision_vars[i].processed = false;
              it->processed = true;

              pthread_cond_signal(&(conditions[i].condition));
              pthread_mutex_unlock(&(conditions[i].mutex));
              assigned_once = true;
              break;
            }
          }
          pthread_mutex_unlock(&collision_master_mutex);
        }
        else
        {
          //we deadlocked - quit the game
          mprintf(("multithread: execution %d - DEADLOCK\n", executions));
          Int3();
          for(i = 0; i < Cmdline_num_threads; i++)
          {
            mprintf(("multithread: destroying thread %d\n", i));
            pthread_cancel(conditions[i].handle);
            pthread_mutex_destroy(&(conditions[i].mutex));
            pthread_cond_destroy(&(conditions[i].condition));
          }
          game_shutdown();
          exit(-1);
        }
      }
      if(assigned_once == true)
      {
        assigned_any = true;
      }
    }
    mprintf(("multithread: execution line %d - end list loop\n", __LINE__));
    if(assigned_any == false)
    {
      mprintf(("multithread: execution %d - INVALID\n", executions));
      Int3();
      for(i = 0; i < Cmdline_num_threads; i++)
      {
        mprintf(("multithread: destroying thread %d\n", i));
        pthread_cancel(conditions[i].handle);
        pthread_mutex_destroy(&(conditions[i].mutex));
        pthread_cond_destroy(&(conditions[i].condition));
      }
    }
    mprintf(("multithread: execution line %d - final check\n", __LINE__));

    //make sure we processs everything on the list
    done = true;
    for(it = collision_list.begin(); it != collision_list.end(); it++)
    {
      if(it->processed == false)
      {
        mprintf(("multithread: execution line %d - looping back\n", __LINE__));
        done = false;
        break;
      }
    }
  }

  //make sure all the threads are done executing
  for(i = 0; i < Cmdline_num_threads; i++)
  {
    pthread_mutex_lock(&(conditions[i].mutex));
    pthread_mutex_unlock(&(conditions[i].mutex));
  }
  executions++;
}

void *supercollider_thread(void *num)
{
  int thread_num = *(int *)num;

  mprintf(("multithread: supercollider_thread %d started\n", thread_num));

  while(threads_alive)
  {
    pthread_mutex_lock(&(conditions[thread_num].mutex));
    pthread_cond_wait(&(conditions[thread_num].condition), &(conditions[thread_num].mutex));

    if((thread_collision_vars[thread_num].object_1 != NULL) && (thread_collision_vars[thread_num].object_2 != NULL))
    {
      obj_collide_pair(thread_collision_vars[thread_num].object_1, thread_collision_vars[thread_num].object_2);
      thread_collision_vars[thread_num].object_1 = NULL;
      thread_collision_vars[thread_num].object_2 = NULL;
    }
    thread_collision_vars[thread_num].processed = true;
    mprintf(("multithread: execution %d - thread %d done\n", executions, thread_num));

    pthread_mutex_unlock(&(conditions[thread_num].mutex));

    pthread_mutex_lock(&collision_master_mutex);
    pthread_cond_signal(&collision_master_condition);
    pthread_mutex_unlock(&collision_master_mutex);

    pthread_yield();
  }
  pthread_exit(NULL);
}

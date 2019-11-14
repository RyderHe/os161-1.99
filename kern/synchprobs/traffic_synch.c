#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *intersectionSem;

static struct lock *intersectionLock;
static struct cv *N_Origin;
static struct cv *S_Origin;
static struct cv *W_Origin;
static struct cv *E_Origin;
static int volatile numVehiclesInInter[4][4];  // number of vehicles from origin to destination
                                               // NN, NS, NW, NE
					       // SN, SS, SW, SE
					       // WN, WS, WW, WE
					       // EN, ES, EW, EE

// convert: int (0,1,2,3) <-> Direction (N, S, W, E)
Direction int_to_dir(int x);
int dir_to_int(Direction dir);

// check if the vehicle is trun right
bool checkTurnRight(Direction origin, Direction destination);

// check if the two vehicles are conflict
bool checkConflict(Direction o1, Direction d1, Direction o2, Direction d2);

// check if the vehicle could enter the intersection
bool couldEnter(Direction origin, Direction destination);



Direction int_to_dir(int x) {
  if (x == 0) {
    return north;
  } else if (x == 1) {
    return south;
  } else if (x == 2) {
    return west;
  } else { // 3
    return east;
  }
}

int dir_to_int(Direction dir) {
  if (dir == north) {
    return 0;
  } else if (dir == south) {
    return 1;
  } else if (dir == west) {
    return 2;
  } else { // east
    return 3;
  }
}

bool checkTurnRight(Direction origin, Direction destination) {
  if (origin == north && destination == west) {
    return true;
  } else if (origin == south && destination == east) {
    return true;
  } else if (origin == west && destination == south) {
    return true;
  } else if (origin == east && destination == north) {
    return true;
  } else { 
    return false;
  }
}

bool checkConflict(Direction o1, Direction d1, Direction o2, Direction d2) {
  if (o1 == o2) { // enter from same direction
    return true;
  } else if (o1 == d2 && d1 == o2) { // opposite directions
    return true;
  } else if (d1 != d2 && (checkTurnRight(o1, d1) || checkTurnRight(o2, d2))) { // diff destination
    return true;                                                        // at least one right turn
  } else {
    return false;
  }
}

bool couldEnter(Direction origin, Direction destination) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      Direction temp_origin = int_to_dir(i);
      Direction temp_destination = int_to_dir(j);
      if ((numVehiclesInInter[i][j] > 0) && 
          (checkConflict(origin, destination, temp_origin, temp_destination) == false)) { // check conflict
          return false;
      }
    }
  }
  return true;
} 



/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  /*
  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }
  return;*/
  

  // initialize
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      numVehiclesInInter[i][j] = 0;
    }
  }

  // create	
  intersectionLock = lock_create("intersection");
  N_Origin = cv_create("north");
  S_Origin = cv_create("south");
  W_Origin = cv_create("west");
  E_Origin = cv_create("east");

  // check
  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  if (N_Origin == NULL) {
    panic("could not create north cv");
  }
  if (S_Origin == NULL) {
    panic("could not create south cv");
  }
  if (W_Origin == NULL) {
    panic("could not create west cv");
  }
  if (E_Origin == NULL) {
    panic("could not create east cv");
  }
  
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  /*KASSERT(intersectionSem != NULL);
  sem_destroy(intersectionSem);*/
  

  KASSERT(intersectionLock != NULL);
  KASSERT(N_Origin != NULL);
  KASSERT(S_Origin != NULL);
  KASSERT(W_Origin != NULL);
  KASSERT(E_Origin != NULL);
	
  // clear
  lock_destroy(intersectionLock);
  cv_destroy(N_Origin);
  cv_destroy(S_Origin);
  cv_destroy(W_Origin);  
  cv_destroy(E_Origin);
  
  return;
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //(void)destination; /* avoid compiler complaint about unused parameter */
  /*KASSERT(intersectionSem != NULL);
  P(intersectionSem);*/


  KASSERT(intersectionLock != NULL);
  KASSERT(N_Origin != NULL);
  KASSERT(S_Origin != NULL);
  KASSERT(W_Origin != NULL);
  KASSERT(E_Origin != NULL);
   
  lock_acquire(intersectionLock);
  
  int temp_origin = dir_to_int(origin);
  int temp_destination = dir_to_int(destination);

  while (couldEnter(origin, destination) == false) { 
    // current vehicle could not enter the intersection dur to confliction
    // keep waiting on corresponding cv until it could enter intersection
    if (origin == north) {
      cv_wait(N_Origin, intersectionLock);
    } else if (origin == south) {
      cv_wait(S_Origin, intersectionLock);
    } else if (origin == west)  {
      cv_wait(W_Origin, intersectionLock);
    } else { // east
      cv_wait(E_Origin, intersectionLock);
    }
  }
  
  // current vehicle now enter the intersection
  numVehiclesInInter[temp_origin][temp_destination] += 1;

  lock_release(intersectionLock);
  
  return;
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  //(void)origin;  /* avoid compiler complaint about unused parameter */
  //void)destination; /* avoid compiler complaint about unused parameter */
  //KASSERT(intersectionSem != NULL);
  //V(intersectionSem);


  KASSERT(intersectionLock != NULL);
  KASSERT(N_Origin != NULL);
  KASSERT(S_Origin != NULL);
  KASSERT(W_Origin != NULL);
  KASSERT(E_Origin != NULL);
   
  lock_acquire(intersectionLock);
  
  int temp_origin = dir_to_int(origin);
  int temp_destination = dir_to_int(destination);
  
  // check if current vehicle needs to broadcast other vehicles
  int count = numVehiclesInInter[temp_origin][0] + numVehiclesInInter[temp_origin][1] +
              numVehiclesInInter[temp_origin][2] + numVehiclesInInter[temp_origin][3];
  if (count == 1) {
    cv_broadcast(N_Origin, intersectionLock);
    cv_broadcast(S_Origin, intersectionLock);
    cv_broadcast(W_Origin, intersectionLock);
    cv_broadcast(E_Origin, intersectionLock);
  }

  // current vehicle now exit the intersection
  numVehiclesInInter[temp_origin][temp_destination] -= 1;
  lock_release(intersectionLock);
  
  return;
}

# Technical Points
## **Reactor-Based** Event Handling Model
1. The main reactor patterns:
	1. `struct Conn` struct contains a socket `fd`, with 3 `bool`s to show intention to `read` `write` or `close`, these intentions are set by the application logic
	2. The main event loop/reactor using a map between socket `fd` and `pollfd`, registers the event on the `pollfd`s and then calls the `poll()` system calls registering the events in the OS
	3. The `poll()` system call returns the events that has just happened on the registered socket `fd`s, and depending on the event type, the main event loop decides whether to `handle_write` or `handle_read` or `handle_accept`
	4. In `handle_read` after the request has been parsed it will then call `try_one_request` which will later call `do_request` for certain functions in `do_request`it takes quite a long time and a `ThreadPool` is used to give it to the worker threads.
2. Achieved through `poll()`(for IO multiplexing and readiness notification) + non-blocking sockets `fd`s (for non-blocking IO) + thread pool (creates the worker threads) + `struct Conn` structs (contains buffers for non-blocking IO and shows intentions for read/write)

## Half-Sync/Half-Reactive Concurrency Model
1. The main thread calls `poll()` which does IO multiplexing and readiness notification, this is the single asynchronous thread. The worker threads execute the application code, these are the synchronous threads.
2. The event-handling model follows a reactor pattern, thus the Half-reactive part

## Thread Pool
1. Contains a queue of tasks, produced by the main thread, implemented as a queue of functions
2. Task queue protected by a `std::mutex` as the main thread will be writing to it and the workers threads will be competing for task on it
3. Worker threads waits with a condition variable `ThreadPool::isEmpty` where only when the thread pool is not empty anymore, can idle worker threads get the tasks
4. Created with a specified number of worker threads in the pool, this number cannot be changed and is specified by the application code

## Hash Table For Main Storage
1. Uses the FNV hash function to hash string keys
2. Uses separate chaining to lower collision rates and making it simpler to implement
3. Uses progressive rehashing where 2 sub hash tables of type `struct HTab` exists in the top-level hash table `struct HMap`, the newer one is twice the size of the older one. This avoids long latency due to the need to rehash a large number of keys
4. Uses intrusive `struct HNode` data structures: 
	1. The nodes only points to the next node and the hash value. 
	2. The actual data is stored by the container types `struct Entry` and `struct ZNode`
	3. Uses the `container_of` function to get the pointers to the containers
	4. Reduces the amount of manual memory management and implementation complexity
	5. Can be used in multiple higher level data structures.

## AVL Tree ZSet for Range and Rank Queries of Certain Keys
1. Uses a self-balancing AVL tree where it is self-balancing so that lookups takes worst case `log(n)`
2. Uses `score name` store, so that ranking/ range search/ lookup can be done using one or both of the attributes
3. Uses the `struct HNode` structs used to implement the hash table, which helps speed up lookups using the `name` field

## Heap Cache

## Timers

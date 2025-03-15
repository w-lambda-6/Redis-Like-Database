# Redis-Like-Database
### Technical Points
1. Concurrency  
   a. Non-blocking IO when dealing with reads and writes on sockets  
   b. The use of the poll()(and potentially epoll()) system call to achieve monitor events on sockets  
   c. Eventloops and structures to track read/write intentions to reduce thread creation

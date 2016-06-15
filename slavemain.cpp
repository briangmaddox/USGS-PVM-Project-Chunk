


#include <iostream>
#include "PvmProjectorSlave.h"

int main(int argc, char **argv)
{
  PvmProjectorSlave slave;
 
  try
  {
    return slave.connect();
  }
  catch(...)
  {
    return 0;
  }
}

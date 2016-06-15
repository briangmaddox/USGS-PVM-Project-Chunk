//Implementation file for the PVFSProjector
//By Chris Bilderback

#include "PVFSProjector.h"

//********************************************************************
PVFSProjector::PVFSProjector() : PvmProjector()
{}

//********************************************************************
PVFSProjector::~PVFSProjector()
{}

//********************************************************************
void PVFSProjector::setPartitionNumber(const unsigned int & inPart) throw()
{
  //resize the vectors
  mcounters.resize(inPart);
  mstop.resize(inPart);
}

//********************************************************************
unsigned int PVFSProjector::getPartitionNumber() const throw()
{
  return mcounters.size();
}

//********************************************************************
void PVFSProjector::project(BaseProgress * progress = NULL) 
  throw(ProjectorException)
{
  int mytid(0);                                  //my pvm id
  int errorcode(0);                              //pvm error code
  PmeshLib::ProjectionMesh * pmesh(0);           //projection mesh
  int counter(0);
  int scounter(0);

  try
  {
    if (!mcounters.size() || !childtid)           //must have a partition num  
    {
      PvmProjector::project(progress);
      return;
    }

    if (static_cast<unsigned int>(numofslaves) < mcounters.size()) 
    {
      PvmProjector::project(progress);
      return;
    }

    
    if (!fromprojection || !toprojection)        //check for projection
    {
      throw ProjectorException(PROJECTOR_ERROR_UNKOWN);
    }
    
    pmesh = setupForwardPmesh();                 //try setup the forward
                                                 //pmesh
    
    getExtents(pmesh);                           //get the extents
    
    
    if(cache)                                    //delete the cache
    {
      delete cache;
      cache = NULL;
    }
      
    setupOutput(outfile);                        //create the output file
        
    
    if (pmesh)                                   //delete uneeded mesh
    {
      delete pmesh;
      pmesh = NULL;
    }

    mytid = pvm_mytid();                         //enroll in pvm
    errorcode = pvm_spawn("slave", 0, 0,
                          NULL, numofslaves, childtid);
    if (errorcode == 0)
    {
      throw ProjectorException(PROJECTOR_ERROR_UNKOWN);
    }
    
    //figure out the mapping
    scounter = -1;
    for(counter = 0; counter < numofslaves; ++counter)
    {
      if (counter % numofslaves/mcounters.size() == 0)
        ++scounter;
      
      membership[childtid[counter]] = scounter;
    }

    //figure out the start stop stuff
    for(counter = 0; static_cast<unsigned int>(counter) 
          < mcounters.size(); ++counter)
    {
      mcounters[counter] = counter*(newheight/mcounters.size());
      mstop[counter] = (counter+1)*(newheight/mcounters.size());
    }
    
    //set the last partition to the right size
    mstop[mcounters.size()-1] = newheight;

    projectPVFS(progress);


  }
  catch(...)
  {
    if (pmesh)
      delete pmesh;
    throw ProjectorException(PROJECTOR_ERROR_UNKOWN);

  }
}

//*****************************************************************
void PVFSProjector::projectPVFS(BaseProgress * progress)
  throw(ProjectorException)
{
  int  bufferid(0), len(0), tag(0),              //pvm tags
    temptid(0);
  Stitcher * mystitch(0);                        //stitcher pointer
  long int beginofchunk(0), endofchunk(0);       //for passing to the slave
  long int chunkcounter(0);                      //for output
  long int ycounter(1);
  int chunkdif(maxchunk-minchunk);


  try
  {
                                                 //init the status progress
    if (progress)
    {
      std::strstream tempstream;
      
      tempstream << "Reprojecting " << newheight << " lines." << std::ends;
      tempstream.freeze(0);
      progress->init(tempstream.str(),
                     NULL,
                     "Done.",
                     newheight, 
                     29);
      progress->start();                         //start the progress
    }
  
    if (stitcher)                                //see if we want a stitcher  
    {
                                                 //creates the stitcher thread
      if (!(mystitch = new (std::nothrow) Stitcher(out)))
        
        throw std::bad_alloc();
    }
    
                               
    if (sequencemethod == 2)                     //init the random number
      srand48(time(NULL));
    
    while (chunkcounter < newheight)
    {
      bufferid = pvm_recv(-1, -1);                //blocking wait for messages
      pvm_bufinfo(bufferid, &len, &tag, &temptid);

      beginofchunk = mcounters[membership[temptid]]; //starting scanline
     
      //check termination
      if (beginofchunk < 0)
      {
        //terminate the slave
        chunkcounter += terminateSlave(tag, temptid, mystitch);
      }
      else
      {
        //check the sequence method
        switch(sequencemethod)
        {
        case 0:
          endofchunk = beginofchunk + maxchunk-1;
          break;
        case 1:
          ++ycounter;
          if (ycounter >= sequencesize)
            ycounter = 0;
          endofchunk = beginofchunk + sequence[ycounter]-1;
          break;
        case 2:
          endofchunk = beginofchunk + static_cast<int>(drand48()*chunkdif
                                                       + minchunk) -1;
          break;
        }
        
        //check to see if this is the last chunk in the partition
        if (endofchunk >= mstop[membership[temptid]]-1)
        {
          endofchunk = mstop[membership[temptid]]-1;
          //reset the counter
          mcounters[membership[temptid]] = -1;
        }
        else
        {
          //update the counter
          mcounters[membership[temptid]]+=(endofchunk-beginofchunk)+1;
        }

        switch(tag)
        {
        case SETUP_MSG:
          //pack the info in
          sendSlaveSetup();
          //add the first bit of work
          pvm_pklong(&(beginofchunk), 1, 1);
          pvm_pklong(&(endofchunk), 1, 1);
          //send the message
          pvm_send(temptid, SETUP_MSG);
          break;
        case WORK_MSG:
          //unpack the scanline
          if (stitcher)
          {
            chunkcounter += sendStitcher(mystitch);
          }
          else
            chunkcounter += unpackScanline();
          //pack the next work
          pvm_initsend(PvmDataDefault);
          pvm_pklong(&(beginofchunk), 1, 1);
          pvm_pklong(&endofchunk, 1, 1);     //last chunk
          pvm_send(temptid, WORK_MSG);
          break;
        case ERROR_MSG:
        default:
          throw ProjectorException(PROJECTOR_ERROR_BADINPUT);
        }
      }
      
      //update the output
      if (progress && !((chunkcounter) % 11))
        progress->update(chunkcounter);

    }

    if (progress)
      progress->done();
    
    if (stitcher)
    {
      mystitch->wait();
      //remove the stitcher
      delete mystitch;
    }
    
    writer.removeImage(0);                      //flush the output image

    out = NULL;
  }
  catch(...)
  {
    if (stitcher)
    {
      delete mystitch;                          //should stop the stitcher
      mystitch = NULL;
    }
    writer.removeImage(0);
  }
  
}

//*************************************************************
long int PVFSProjector::terminateSlave(const int & tag, const int & tid,
                             Stitcher * mystitch)
  throw(ProjectorException)
{
  if (tag == WORK_MSG)
  {
    if (stitcher)
    {
       pvm_initsend(PvmDataDefault);
       pvm_send(tid, EXIT_MSG);       //tell slave to exit
      return sendStitcher(mystitch);
    }
    else
    {
       pvm_initsend(PvmDataDefault);
       pvm_send(tid, EXIT_MSG);       //tell slave to exit
      return unpackScanline();
    }
  }
  

  if (tag == ERROR_MSG)
  {
    throw ProjectorException(PROJECTOR_ERROR_BADINPUT); 
    //should never happen
  }
  
  return 0;
  pvm_initsend(PvmDataDefault);
  pvm_send(tid, EXIT_MSG);       //tell slave to exit


}

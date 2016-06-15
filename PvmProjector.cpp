#ifndef PVMPROJECTOR_CPP_
#define PVMPROJECTOR_CPP_


#include "PvmProjector.h"
#include <stdlib.h>
#include <time.h>
#include <cmath>

//*******************************************************************
PvmProjector::PvmProjector() : Projector(),
                               numofslaves(0),
                               evenchunks(false),slavelocal(false), 
                               childtid(0), sequencemethod(0),
                               minchunk(0), maxchunk(0),
                               sequence(0), sequencesize(0),
                               slavelocalpath("./"), stitcher(false)
{}

//*******************************************************************
PvmProjector::~PvmProjector()
{
  delete [] childtid;
  delete [] sequence;
}

//*******************************************************************
void PvmProjector::setNumberOfSlaves(const int & innumslaves) throw()
{
  try
  {
    numofslaves = innumslaves;                //set the number of slaves
    
    delete [] childtid;                       //delete and null the childtid
    childtid = NULL;
    

     
    if (numofslaves != 0)                     //make sure not to create if 0
    {
      if (!(childtid = new (std::nothrow) int [numofslaves]))
        throw std::bad_alloc();

    }
  }
  catch(...)
  {
    delete [] childtid;                       //delete memory and return
    childtid = NULL;
    
  }
}

//**********************************************************************
int PvmProjector::getNumberOfSlaves() const throw()
{
  return numofslaves;
}


//**********************************************************************
void PvmProjector::setChunkSize(const int & inchunksize) throw()
{
  resetSequencing();

  if (inchunksize == 0)                //check to see if it is zero
    maxchunk = 1;
  else
    maxchunk = inchunksize;           //set the chunksize

  
}

//**********************************************************************
int PvmProjector::getChunkSize() const throw()
{
  return maxchunk;
}

//**********************************************************************
void PvmProjector::setEvenChunks(bool inevenchunks) throw()
{
  evenchunks = inevenchunks;
}

//**********************************************************************
bool PvmProjector::getEvenChunks() const throw()
{
  return evenchunks;
}

//***********************************************************************
void PvmProjector::setSlaveStoreLocal(const bool & inslavelocal) throw()
{
  slavelocal = inslavelocal;   //set the slave local
}

//***********************************************************************
bool PvmProjector::getSlaveStoreLocal() const throw()
{
  return slavelocal;
}

  
//*********************************************************************
void PvmProjector::project(BaseProgress * progress) 
    throw(ProjectorException)
{
  int mytid(0);                                  //my pvm id
  int errorcode(0);                              //pvm error code
  PmeshLib::ProjectionMesh * pmesh(0);           //projection mesh
 
  try
  {
    if (!numofslaves || !childtid)               //must have a slave pvmproject
    {
      Projector::project(progress);
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
    
    //branch on whether to have a slave store locally or not
    if (!slavelocal)
    {
      if (!projectnoslavelocal(progress))
          throw ProjectorException(PROJECTOR_ERROR_UNKOWN);
    }
    else
    {
      if (!projectslavelocal(progress))
        throw ProjectorException(PROJECTOR_ERROR_UNKOWN);
    }
  }
  catch(...)
  {
    if (pmesh)
      delete pmesh;
    throw ProjectorException(PROJECTOR_ERROR_UNKOWN);
  }

}
    

//*******************************************************************
void PvmProjector::setInputFile(std::string & ininfile) throw(ProjectorException)
{
  Projector::setInputFile(ininfile);
  inputfilename = ininfile;
}


//********************************************************************
bool PvmProjector::sendSlaveSetup() throw()
{
  char tempbuffer[100];
  int temp;
  try
  {
    //pack vital info for slave
    pvm_initsend(PvmDataDefault);
    
    strcpy(tempbuffer, inputfilename.c_str());
    //pack the input file name
    pvm_pkstr(tempbuffer);
    //pack the new image metrics
    pvm_pklong(&newheight, 1, 1);
    pvm_pklong(&newwidth, 1, 1);
    pvm_pkdouble(&newscale.x, 1, 1);
    pvm_pkdouble(&newscale.y, 1, 1);
    pvm_pkdouble(&outRect.left, 1, 1);
    pvm_pkdouble(&outRect.top, 1, 1);
    pvm_pkdouble(&outRect.bottom, 1, 1);
    pvm_pkdouble(&outRect.right, 1, 1);
   
    if (slavelocal)
      temp = 1;
    else
      temp = 0;
    //pack slave info
    pvm_pkint(&(temp), 1, 1);
    //pack the max chunksize
    pvm_pkint(&maxchunk, 1,1);

    strcpy(tempbuffer, slavelocalpath.c_str());
    pvm_pkstr(tempbuffer);

    //pack pmesh info
    pvm_pkint(&pmeshsize, 1, 1);
    pvm_pkint(&pmeshname, 1, 1);
    
    //pack the projection parameters
    pvm_pkint(reinterpret_cast<int *>(&Params.projtype),1,1);
    pvm_pkint(reinterpret_cast<int *>(&Params.datum), 1, 1);
    pvm_pkint(reinterpret_cast<int *>(&Params.unit), 1, 1);
    pvm_pkdouble(&Params.StdParallel1, 1, 1);
    pvm_pkdouble(&Params.StdParallel2, 1, 1);
    pvm_pkdouble(&Params.NatOriginLong, 1,1);
    pvm_pkdouble(&Params.NatOriginLat, 1, 1);
    pvm_pkdouble(&Params.FalseOriginLong, 1, 1);
    pvm_pkdouble(&Params.FalseOriginLat, 1, 1);
    pvm_pkdouble(&Params.FalseOriginEasting, 1, 1);
    pvm_pkdouble(&Params.FalseOriginNorthing, 1, 1);
    pvm_pkdouble(&Params.CenterLong, 1, 1);
    pvm_pkdouble(&Params.CenterLat, 1, 1);
    pvm_pkdouble(&Params.CenterEasting, 1, 1);
    pvm_pkdouble(&Params.CenterNorthing, 1, 1);
    pvm_pkdouble(&Params.ScaleAtNatOrigin, 1, 1);
    pvm_pkdouble(&Params.AzimuthAngle, 1, 1);
    pvm_pkdouble(&Params.StraightVertPoleLong, 1, 1);
    pvm_pkint(&Params.zone, 1, 1);
    pvm_pkdouble(&Params.FalseEasting, 1, 1);
    pvm_pkdouble(&Params.FalseNorthing, 1, 1);
    return true;
  }
  catch(...)
  {
    //don't do anything
    return false;
  }
}

//*******************************************************
bool PvmProjector::projectslavelocal(BaseProgress * progress)
  throw(ProjectorException)
{
  int  bufferid(0), len(0), tag(0),    //pvm tags
    temptid(0);
  long int ycounter(1), endofchunk(0), beginofchunk(0);
  long int countmax(0);      //this is the total number of scanlines sent
  long int chunkssent(0);    //this is the number of chunks sent
  int chunkdif(maxchunk-minchunk);
  long int * chunknode(0);   //represents a node in the chunk queue
  std::queue<long int *> chunkqueue;
  unsigned char * scanline(0);
  std::strstream tempstream;

  try
  {
     //init the status progress
    if (progress)
    {
      tempstream << "Reprojecting " << newheight << " lines." << std::ends;
      tempstream.freeze(0);
      progress->init(tempstream.str(),
                      NULL,
                      "Done.",
                      newheight, 
                      29);
      tempstream.seekp(0);
      progress->start();  //start the progress
    }

    //init the random number generator
    if (sequencemethod == 2)
      srand48(time(NULL));
    

    while (countmax < newheight)
    {
      beginofchunk = countmax;    //set the beginning line
      //build the message
      switch(sequencemethod)
      {
      case 0:
        endofchunk = countmax + maxchunk-1;
        break;
      case 1:
        ++ycounter;
        if (ycounter >= sequencesize)
          ycounter = 0;
        endofchunk = countmax + sequence[ycounter]-1;
        break;
      case 2:
        endofchunk = countmax + static_cast<int>(drand48()*chunkdif
                                                 + minchunk) -1;
        break;
      }
      
                                                            
      //check the newheight
      if (endofchunk >= newheight)
      {
        endofchunk = newheight-1;
      }

      //update the countmax
      countmax += (endofchunk - beginofchunk) + 1;
      
      //create the chunk node
      if (!(chunknode = new (std::nothrow) long int [3]))
        throw std::bad_alloc();
      
      //store chunk dimensions
      chunknode[0] = beginofchunk; 
      chunknode[1] = endofchunk;

      //update the number of chunks sent
      ++chunkssent;
              
      //update the output
      if (progress && !((beginofchunk) % 11))
        progress->update(beginofchunk);
      
      bufferid = pvm_recv(-1, -1);      //blocking wait for all messages
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      

      switch(tag)
      {
      case SETUP_MSG:
        //pack the info in
        sendSlaveSetup();
        //add the first bit of work
        pvm_pklong(&(beginofchunk), 1, 1);
        pvm_pklong(&(endofchunk), 1, 1);
        //store that this node has this chunk
        chunknode[2] = temptid;
        chunkqueue.push(chunknode);

        //send the message
        pvm_send(temptid, SETUP_MSG);
        break;
      case WORK_MSG:
        //pack the next work
        pvm_initsend(PvmDataDefault);
        pvm_pklong(&(beginofchunk), 1, 1);
        pvm_pklong(&endofchunk, 1, 1);     //last chunk
        
        chunknode[2] = temptid;
        chunkqueue.push(chunknode);
        pvm_send(temptid, WORK_MSG);
        break;
      case ERROR_MSG:
      default:
        //slave sent error
        throw ProjectorException(PROJECTOR_ERROR_BADINPUT);
      }
    }

    //first tell the slaves to projecting loop
    for (ycounter = 0; ycounter < numofslaves; ++ycounter)
    {
      bufferid = pvm_recv(-1, -1);      //blocking wait for all msgs
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      if (tag == ERROR_MSG)
      {
        throw ProjectorException(PROJECTOR_ERROR_BADINPUT);
      }

      pvm_initsend(PvmDataDefault);
      pvm_send(childtid[ycounter], EXIT_MSG);   //tell slave to exit

    }

    
    if (progress)
      progress->done();

    //create the scaline
    if (!(scanline = new unsigned char[newwidth*spp]))
      throw std::bad_alloc();
    
    if (progress)
    {
      tempstream << "Writing " << newheight << " lines." << std::ends;
      tempstream.freeze(0);
      progress->init(tempstream.str(),
                     NULL,
                     "Done.",
                     newheight, 
                     29);
      tempstream.seekp(0);
      progress->start();  //start the progress
    }

    //finish writting scanlines
    while (chunkqueue.size())
    {
      chunknode = chunkqueue.front();
      
      //get the number of scanlines in this chunk
      beginofchunk = chunknode[0];
      endofchunk = chunknode[1];

      //update the output
      if (progress && !((beginofchunk) % 11))
        progress->update(beginofchunk);

      //get the slave id
      temptid = chunknode[2];
      
      for (ycounter = beginofchunk; ycounter <= endofchunk; ++ycounter)
      {
        //ask for a scanline
        pvm_initsend(PvmDataDefault);
        pvm_send(temptid, WORK_MSG);
        
        bufferid = pvm_recv(-1, -1);      //blocking wait for all msgs
        pvm_bufinfo(bufferid, &len, &tag, &temptid);
        
        if (tag == ERROR_MSG)
        {
          throw ProjectorException(PROJECTOR_ERROR_BADINPUT);
        }
        
        if(pvm_upkbyte(reinterpret_cast<char*>(scanline),
                       newwidth*spp, 1) < 0)
          throw ProjectorException(PROJECTOR_ERROR_BADINPUT);
        //write it
        out->putRawScanline(ycounter, scanline); 
      }
      
      //break it out of the loop
      pvm_initsend(PvmDataDefault);
      pvm_send(temptid, WORK_MSG);
      

      chunkqueue.pop();         //remove the chunk
      delete []  chunknode;     //delete the chunk node
    }

    if (progress)
      progress->done();

    writer.removeImage(0);                      //flush the output image
    out = NULL;
    return true;
  }
  catch(...)
  {
    //see if there is anthing left in the queue
    while (chunkqueue.size())
    {
      chunknode = chunkqueue.front();
      chunkqueue.pop();
      delete [] chunknode;
    }
      

    return false;
  }

}

//*******************************************************
bool PvmProjector::projectnoslavelocal(BaseProgress * progress)
  throw(ProjectorException)
{
  int  bufferid(0), len(0), tag(0),    //pvm tags
    temptid(0);
  long int ycounter(1), endofchunk(0), beginofchunk(0);
  long int countmax(0);      //this is the total number of scanlines sent
  long int chunkssent(0);    //this is the number of chunks sent
  long int chunksgot(0);     //this is the number of chunks that we have got
  unsigned int chunkdif(maxchunk-minchunk);
  Stitcher * mystitch(0);    //this is the sticher pointer (if we use it)
    

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
      progress->start();  //start the progress
    }

    if (stitcher)  //see if we want a stitcher

    {
      //creates the stitcher thread
      if (!(mystitch = new (std::nothrow) Stitcher(out)))

        throw std::bad_alloc();
    }

    //init the random number generator
    if (sequencemethod == 2)
      srand48(time(NULL));
    

    while (countmax < newheight)
    {
      beginofchunk = countmax;    //set the beginning line
      //build the message
      switch(sequencemethod)
      {
      case 0:
        endofchunk = countmax + maxchunk -1;
        break;
      case 1:

        ++ycounter;
        if (ycounter >= sequencesize)
          ycounter = 0;
        endofchunk = countmax + sequence[ycounter]-1;
        break;
      case 2:
        endofchunk = countmax + static_cast<int>(drand48()*chunkdif
                                                 + minchunk) -1;
        break;
      }
      
                                                            
      //check the newheight
      if (endofchunk >= newheight)
      {
        endofchunk = newheight-1;
      }

      //update the countmax
      countmax += (endofchunk - beginofchunk) + 1;
      
      //update the number of chunks sent
      ++chunkssent;
              
      //update the output
      if (progress && !((beginofchunk) % 11))
        progress->update(beginofchunk);
      
      bufferid = pvm_recv(-1, -1);      //blocking wait for all messages
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      

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
        ++chunksgot; //got a chunk
        
        if (stitcher)
        {
          sendStitcher(mystitch);
        }
        else
          unpackScanline();
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

    //finish writting scanlines
    for (ycounter = chunksgot; ycounter < chunkssent; ++ycounter)
    {
      bufferid = pvm_recv(-1, -1);      //blocking wait for all msgs
      pvm_bufinfo(bufferid, &len, &tag, &temptid);
      
      if (tag == WORK_MSG)
      {
        if (stitcher)
        {
          sendStitcher(mystitch);
        }
        else
          unpackScanline();
      }
      else
      {
         throw ProjectorException(PROJECTOR_ERROR_BADINPUT); 
         //should never happen
      }
    }
    
    //slave termination
    for (ycounter = 0; ycounter < numofslaves; ++ycounter)
    {
      pvm_initsend(PvmDataDefault);
      pvm_send(childtid[ycounter], EXIT_MSG);   //tell slave to exit

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
    return true;
  }
  catch(...)
  {
    if (stitcher)
    {
      delete mystitch;                          //should stop the stitcher
      mystitch = NULL;
    }

    return false;
  }
}

//******************************************************
void PvmProjector::setStitcher(bool institcher) throw()
{
  stitcher = institcher;
}

//******************************************************
bool PvmProjector::getStitcher() const throw()
{
  return stitcher;
}


//***************************************************************
void PvmProjector::setSequence(const int * insequence,
                 const int & insequencesize) throw(std::bad_alloc)
{
  int counter(0);
  int max(0);                        //finds the max of the squence
  resetSequencing();                 //reset the sequencing
  
  sequencesize = insequencesize;     //set the size
  
  //create the sequence
  if(!(sequence = new (std::nothrow) int [sequencesize]))
    throw std::bad_alloc();
  
  //copy the sequence
  for (; counter < insequencesize; ++counter)
  {
    sequence[counter] = insequence[counter];
    if (sequence[counter] > max)
      max = sequence[counter];
  }

  maxchunk = max;   //set the maxchunksize
  //set the sequence method
  sequencemethod = 1;
}

  
//****************************************************************
void PvmProjector::setRandomSequence(const int & inminchunk,
                       const int & inmaxchunk) throw()
{
  resetSequencing(); 

  //set the chunksizes and sequence method
  minchunk = inminchunk;
  maxchunk = inmaxchunk;
  sequencemethod = 2;
}


  
//****************************************************************
void PvmProjector::resetSequencing() throw()
{
  delete [] sequence;
  sequence = 0;
  sequencesize = 0;
  minchunk = 0;
  maxchunk = 1;
  sequencemethod = 0;
}

//****************************************************************
void PvmProjector::
setSlaveLocalDir(const std::string & inslavelocalpath) throw()
{
  slavelocalpath = inslavelocalpath;
}

//**************************************************************
std::string PvmProjector::getSlaveStoreLocalDir() const throw()
{
  return slavelocalpath;
}

//*******************************************************
long int PvmProjector::unpackScanline() throw()
{
  unsigned char * tempscanline=NULL;
  long int scanlinenumber=0;
  long int endscanline = 0;
  long int counter; 
  try
  {
    //unpack the scanline numbers
    pvm_upklong(&scanlinenumber, 1,1 );
    pvm_upklong(&endscanline, 1, 1);
   
    
    //create enough space to hold the entire chunk
    if (!(tempscanline = new (std::nothrow) unsigned char 
          [(endscanline-scanlinenumber + 1)*newwidth*spp]))
      throw std::bad_alloc();

    //unpack all of the buffer
    pvm_upkbyte(reinterpret_cast<char*>(tempscanline),
                (endscanline-scanlinenumber + 1)*newwidth*spp, 1);


    for (counter = scanlinenumber; counter <= endscanline; ++counter)
    {
      //write it
      out->putRawScanline(counter, 
              &(tempscanline[newwidth*spp*(counter-scanlinenumber)]) ); 
    }

    delete [] tempscanline;
  
    return endscanline-scanlinenumber + 1;

  }
  catch(...)
  {
    return -1;
    //something bad happened 
    if (tempscanline)
      delete [] tempscanline;
  }
}

//*********************************************************************
long int PvmProjector::sendStitcher(Stitcher * mystitch) throw()
{
  unsigned char * tempscanline(0);
  long int scanlinenumber(0);
  long int endscanline(0);
  StitcherNode * temp(0);
  try
  {
    
    //unpack the scanline numbers
    pvm_upklong(&scanlinenumber, 1,1 );
    pvm_upklong(&endscanline, 1, 1);
   
    
    //create enough space to hold the entire chunk
    if (!(tempscanline = new (std::nothrow) unsigned char 
          [(endscanline-scanlinenumber + 1)*newwidth*spp]))
      throw std::bad_alloc();

    //unpack all of the buffer
    pvm_upkbyte(reinterpret_cast<char*>(tempscanline),
                (endscanline-scanlinenumber + 1)*newwidth*spp, 1);

    //create the stitcher node
    if (!(temp = new (std::nothrow) StitcherNode(scanlinenumber,
                                                 endscanline,
                                                 tempscanline)))
      throw std::bad_alloc();
    
    
    //add the chunk to the stitchers queue
    mystitch->add(temp);
    
    return endscanline-scanlinenumber + 1;
    
  }
  catch(...)
  {
    return -1;
    delete temp;
    delete [] tempscanline;
  }
}


#endif






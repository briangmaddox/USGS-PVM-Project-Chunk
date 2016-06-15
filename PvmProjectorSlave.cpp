#ifndef PVMPROJECTORSLAVE_CPP
#define PVMPROJECTORSLAVE_CPP


#include "PvmProjectorSlave.h"
#include <unistd.h>

//********************************************************
PvmProjectorSlave::PvmProjectorSlave() : Projector(),
                                         slavelocal(false),
                                         mastertid(0), mytid(0),
                                         maxchunk(1)
{
}

//********************************************************
PvmProjectorSlave::~PvmProjectorSlave()
{
}

//********************************************************
bool PvmProjectorSlave::connect() throw()
{
  double x, y;                             //temp projecting vars
  int _x, _y;                              //actual image xy
  long int currenty, endy,xcounter,
    ycounter;                              //current line and counter
  unsigned char * scanline = NULL;         //output scanline
  unsigned char * buffer = NULL;           //the buffer to send back
  const unsigned char * inscanline = NULL; //input scaline
  int sppcounter;                          // spp counter
  int bufferid, len, tag, temptid;         //pvm buffer info
  PmeshLib::ProjectionMesh * pmesh = NULL; //projection mesh
  double xscaleinv, yscaleinv;

 


  try
  {
   
    mastertid = pvm_parent();           //get the parent node
    mytid = pvm_mytid();                //get my tid

    

    if (mastertid < 0)
    {
      return false;
    }
    
    
    pvm_initsend(PvmDataDefault);       //init the send buffer
    pvm_send(mastertid, SETUP_MSG);     //send a setup request
  
    bufferid = pvm_recv(-1,-1);         //receive any
    pvm_bufinfo(bufferid, &len, &tag, &temptid); //get info
  
    if (tag != SETUP_MSG)               //check for right tag
      return false;
  
    unpackSetup();                      //unpack the setup info

    if (slavelocal)                     //check the slavelocal
    {
      return storelocal();            
    }

  
    if (pmesh)
    {
      delete pmesh;
      pmesh = NULL;
    }
     
    pmesh = setupReversePmesh();        //setup the reverse pmesh
     
    
    //create the buffer to be at least as big as the 
    //maximum chunksize
    if (!(buffer = new (std::nothrow) unsigned char 
          [(maxchunk)*newwidth*spp]))
      throw std::bad_alloc();
    
  
   
    
    //calcuate the inversers to do multiplaction instead of division
    xscaleinv = 1.0/oldscale.x;
    yscaleinv = 1.0/oldscale.y;


    //proccess messages
    while (tag != EXIT_MSG)
    {
       pvm_upklong(&currenty, 1, 1);       //unpack the scanlines
       pvm_upklong(&endy, 1, 1);
      
      //now just build the return message
      pvm_initsend(PvmDataDefault);
      pvm_pklong(&currenty, 1, 1);
      pvm_pklong(&endy, 1, 1);
             
      for (ycounter = currenty; ycounter <= endy; ++ycounter)
      {
        scanline = &(buffer[newwidth*spp*(ycounter-currenty)]);
        
        //reproject the line
        for (xcounter = 0; xcounter < newwidth; ++xcounter)
        {
          x = outRect.left + newscale.x * xcounter;
          y = outRect.top  - newscale.y * ycounter;
          
          //now get the 
          //reverse projected value
          if (pmesh)
	  {
	    pmesh->projectPoint(x, y);
	  }        
          else
          {
            toprojection->projectToGeo(x, y, y, x);
            fromprojection->projectFromGeo(y, x, x, y);
          }
          _x = static_cast<long int>((x - inRect.left)* 
                                     (xscaleinv) + 0.5);
          _y = static_cast<long int>((inRect.top - y) * 
                                     (yscaleinv) + 0.5);
          
          if ((_x >= oldwidth) || (_x < 0) || (_y >= oldheight) 
              || (_y < 0))
          {
            for (sppcounter = 0; sppcounter < spp; sppcounter++)
              scanline[xcounter*spp + sppcounter] = 0;
          }
          else
          {
            inscanline = cache->getRawScanline(_y);
            for (sppcounter = 0; sppcounter < spp; sppcounter++)
              scanline[xcounter*spp + sppcounter ] = 
                inscanline[_x*spp + sppcounter];
          }
       
         
	}

      }
     
      //pack this chunk into the buffer
      pvm_pkbyte(reinterpret_cast<char*>(buffer),
                 (endy-currenty + 1)*newwidth*spp, 1);
      
      

      //send the entire chunk back the the master
      pvm_send(mastertid, WORK_MSG);
      
      //reset the scanline
      scanline = NULL;

      bufferid = pvm_recv(-1,-1);         //receive any
      pvm_bufinfo(bufferid, &len, &tag, &temptid); //get info
    
      
    }
    
    
    //exit from pvm (this is needed to exit the program)
    //4/3/2001 Chris Bilderback
    pvm_exit();
    

    delete [] buffer;
    scanline = NULL;
    delete pmesh;
    delete toprojection;
    toprojection = NULL;
    pmesh = NULL;
    return true;
  }
  catch(...)
  {
     //set a error to the master
    pvm_initsend(PvmDataDefault);       //init the send buffer
    pvm_send(mastertid, ERROR_MSG);     //send an error message
    delete pmesh;
    delete toprojection;
    toprojection = NULL;
    pmesh = NULL;
    delete scanline;
    return false;
  }
}

//*********************************************************
bool PvmProjectorSlave::storelocal() throw()
{
  double x(0), y(0);                          //temp projecting vars
  int _x(0), _y(0);                           //actual image xy
  long int currenty(0), endy(0), xcounter(0),
    ycounter(0);                              //current line and counter
  unsigned char * scanline(0);                //output scanline
  const unsigned char * inscanline(0);        //input scaline
  int sppcounter(0);                          // spp counter
  int bufferid(0), len(0), tag(0), temptid(0);//pvm buffer info
  PmeshLib::ProjectionMesh * pmesh(0);        //projection mesh
  double xscaleinv(0), yscaleinv(0);
  std::ofstream out;                     
  std::ifstream in;                         
  std::string filename;                       //the current chunk file name
  ChunkFile * tmpChunk(0);                    //temp chunk
  int chunkcounter(0);                        //counts the chunks proccesed
  std::strstream tempstream;                  //for building the filname
  std::queue<ChunkFile*> filequeue;           //the chunk file queue


  try
  {
    
  
    if (pmesh)
    {
      delete pmesh;
      pmesh = NULL;
    }
     
    pmesh = setupReversePmesh();   //setup the reverse pmesh


     
    
    //create the scanline
    if (!(scanline = new (std::nothrow) unsigned char
          [newwidth*spp]))
      throw std::bad_alloc();
    
  
   
    
    //calcuate the inversers to do multiplaction instead of division
    xscaleinv = 1.0/oldscale.x;
    yscaleinv = 1.0/oldscale.y;

    //proccess messages
    while (tag != EXIT_MSG)
    {
       pvm_upklong(&currenty, 1, 1);       //unpack the scanlines
       pvm_upklong(&endy, 1, 1);
       
       //generate the output filename
       tempstream << mytid << chunkcounter << "chunk.tmp" << std::ends;
       filename = basepath;
       filename.append(tempstream.str());
       tempstream.freeze(0);
       tempstream.seekp(0);
       tempstream.clear();
       
       out.open(filename.c_str(), std::ios::out | std::ios::binary|
                                  std::ios::trunc);
 
       if(out.fail())
       {
         //change to throw later on
         //send an error message to the master
         pvm_initsend(PvmDataDefault);
         pvm_send(mastertid, ERROR_MSG);
         pvm_exit();
         return 0;
       }

       //create the file node
       if (!(tmpChunk = new (std::nothrow) ChunkFile(filename,
                                                     currenty,
                                                     endy)))
         throw std::bad_alloc();

      
       //reproject the scanline
       for (ycounter = currenty; ycounter <= endy; ++ycounter)
       {
         //reproject the line
         for (xcounter = 0; xcounter < newwidth; ++xcounter)
         {
           x = outRect.left + newscale.x * xcounter;
           y = outRect.top  - newscale.y * ycounter;
           
           //now get the 
           //reverse projected value
           if (pmesh)
           {
             pmesh->projectPoint(x, y);
           }        
           else
           {
             toprojection->projectToGeo(x, y, y, x);
             fromprojection->projectFromGeo(y, x, x, y);
           }
           _x = static_cast<long int>((x - inRect.left)* 
                                      (xscaleinv) + 0.5);
           _y = static_cast<long int>((inRect.top - y) * 
                                      (yscaleinv) + 0.5);
           
           if ((_x >= oldwidth) || (_x < 0) || (_y >= oldheight) 
               || (_y < 0))
           {
             for (sppcounter = 0; sppcounter < spp; sppcounter++)
               scanline[xcounter*spp + sppcounter] = 0;
           }
           else
           {
             inscanline = cache->getRawScanline(_y);
             for (sppcounter = 0; sppcounter < spp; sppcounter++)
               scanline[xcounter*spp + sppcounter ] = 
                 inscanline[_x*spp + sppcounter];
           }
           
           
         }
         
         //write this scanline to disk
         out.write(scanline, spp*newwidth);
       
       }
     
       //close the file
       out.close();
       //put the node in the queue
       filequeue.push(tmpChunk);
       //increment the chunkcounter
       chunkcounter++;
    
       //now ask the master for more work
       pvm_initsend(PvmDataDefault);
       pvm_send(mastertid, WORK_MSG);
       
             
       bufferid = pvm_recv(-1,-1);         //receive any
       pvm_bufinfo(bufferid, &len, &tag, &temptid); //get info
    }
    
    //now wait for master to ask for the chunks
    while (filequeue.size())
    {
      bufferid = pvm_recv(-1, -1);
      tmpChunk = filequeue.front();    //get the first file
      in.open(tmpChunk->path.c_str(),
              std::ios::in | std::ios::binary
             |std::ios::nocreate); //open the path
      
      if (in.fail())
      {
        //chang to throw later on
        pvm_initsend(PvmDataDefault);
        pvm_send(mastertid, ERROR_MSG);
        pvm_exit();
        return false;
      }

     

      for (ycounter = tmpChunk->starty; ycounter <= tmpChunk->endy; 
           ++ycounter)
      {
       
        //read the data in
        in.read(scanline, spp*newwidth);
      
        if (in.fail())
        {
          //chang to throw later on
          pvm_initsend(PvmDataDefault);
          pvm_send(mastertid, ERROR_MSG);
          pvm_exit();
          return false;
        }

       
        //put it in a message
        pvm_initsend(PvmDataDefault);
        pvm_pkbyte(reinterpret_cast<char*>(scanline),
                      newwidth*spp, 1);
        
        pvm_send(mastertid, WORK_MSG);

        bufferid = pvm_recv(-1, -1);  //wait for next scanline request
        
      }

      in.close();

      //delete the file
      unlink(tmpChunk->path.c_str());
      
      filequeue.pop();       //pop the chunk of the queue
      delete tmpChunk;       //delete the file chunk
      
    }
      

   
    //exit from pvm (this is needed to exit the program)
    //4/3/2001 Chris Bilderback
    pvm_exit();
    

    delete [] scanline;
    delete pmesh;
    delete toprojection;
    toprojection = NULL;
    pmesh = NULL;
    return true;
  }
  catch(...)
  {
    //send an error message to the master
    pvm_initsend(PvmDataDefault);
    pvm_send(mastertid, ERROR_MSG);
    pvm_exit();
    delete pmesh;
    delete toprojection;
    toprojection = NULL;
    pmesh = NULL;
    delete scanline;
    return false;
  }
}
  



//*********************************************************
void PvmProjectorSlave::unpackSetup() throw()
{
  char tempbuffer[100];
  std::string inputfilename;
  int temp;

  try
  {
    pvm_upkstr(tempbuffer);
    inputfilename = tempbuffer;
   
    pvm_upklong(&newheight, 1, 1);
    pvm_upklong(&newwidth, 1, 1);
    pvm_upkdouble(&newscale.x, 1, 1);
    pvm_upkdouble(&newscale.y, 1, 1);
    pvm_upkdouble(&outRect.left, 1, 1);
    pvm_upkdouble(&outRect.top, 1, 1);
    pvm_upkdouble(&outRect.bottom, 1, 1);
    pvm_upkdouble(&outRect.right, 1, 1);

     //pack slave info
    pvm_upkint(&temp, 1, 1);
    slavelocal = temp;
    
    pvm_upkint(reinterpret_cast<int*>(&maxchunk), 1,1);
    
    //unpack base path
    pvm_upkstr(tempbuffer);
    basepath = tempbuffer;
    
    //pack pmesh info
    pvm_upkint(&pmeshsize, 1, 1);
    pvm_upkint(&pmeshname, 1, 1);
    
    //pack the projection parameters
    pvm_upkint(reinterpret_cast<int *>(&Params.projtype),1,1);
    pvm_upkint(reinterpret_cast<int *>(&Params.datum), 1, 1);
    pvm_upkint(reinterpret_cast<int *>(&Params.unit), 1, 1);
    pvm_upkdouble(&Params.StdParallel1, 1, 1);
    pvm_upkdouble(&Params.StdParallel2, 1, 1);
    pvm_upkdouble(&Params.NatOriginLong, 1,1);
    pvm_upkdouble(&Params.NatOriginLat, 1, 1);
    pvm_upkdouble(&Params.FalseOriginLong, 1, 1);
    pvm_upkdouble(&Params.FalseOriginLat, 1, 1);
    pvm_upkdouble(&Params.FalseOriginEasting, 1, 1);
    pvm_upkdouble(&Params.FalseOriginNorthing, 1, 1);
    pvm_upkdouble(&Params.CenterLong, 1, 1);
    pvm_upkdouble(&Params.CenterLat, 1, 1);
    pvm_upkdouble(&Params.CenterEasting, 1, 1);
    pvm_upkdouble(&Params.CenterNorthing, 1, 1);
    pvm_upkdouble(&Params.ScaleAtNatOrigin, 1, 1);
    pvm_upkdouble(&Params.AzimuthAngle, 1, 1);
    pvm_upkdouble(&Params.StraightVertPoleLong, 1, 1);
    pvm_upkint(&Params.zone, 1, 1);
    pvm_upkdouble(&Params.FalseEasting, 1, 1);
    pvm_upkdouble(&Params.FalseNorthing, 1, 1);
   
    Projector::setInputFile(inputfilename);//setup cache, input image metrics, 
                                           //input projection
    
    toprojection = SetProjection(Params); //get the to projection
  }
  catch(...)
  {
    //set a error to the master
    pvm_initsend(PvmDataDefault);       //init the send buffer
    pvm_send(mastertid, ERROR_MSG);     //send an error message
    delete toprojection;
    toprojection = NULL;
  }
}

#endif





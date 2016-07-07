/**
  @file
  Flat Resolution

  Richard Barnes (rbarnes@umn.edu), 2012

  Contains code to generate an elevation mask which is guaranteed to drain
  a flat using a convergent flow pattern (unless it's a mesa)
*/
#ifndef _flat_resolution_included
#define _flat_resolution_included

#include "../common/interface.hpp"
#include "../common/grid_cell.hpp"
#include "../flowdirs/d8_flowdirs.hpp"
#include <deque>
#include <vector>
#include <queue>
#include <cmath>
#include <limits>

//234
//105
//876
//d8_masked_FlowDir
/**
  @brief  Helper function to d8_flow_flats()
  @author Richard Barnes (rbarnes@umn.edu)

  This determines a cell's flow direction, taking into account flat membership.
  It is a helper function to d8_flow_flats()

  @param[in]  &flat_mask      A mask from resolve_flats_barnes()
  @param[in]  &labels         The labels output from resolve_flats_barnes()
  @param[in]  x               x coordinate of cell
  @param[in]  y               y coordinate of cell

  @returns    The flow direction of the cell
*/
static int d8_masked_FlowDir(
  const Array2D<int32_t> &flat_mask,
  const Array2D<int32_t> &labels,
  const int x,
  const int y
){
  int minimum_elevation=flat_mask(x,y);
  int flowdir=NO_FLOW;

  //It is safe to do this without checking to see that (nx,ny) is within
  //the grid because we only call this function on interior cells
  for(int n=1;n<=8;n++){
    int nx=x+dx[n];
    int ny=y+dy[n];
    if( labels(nx,ny)!=labels(x,y))
      continue;
    if(  flat_mask(nx,ny)<minimum_elevation || (flat_mask(nx,ny)==minimum_elevation && flowdir>0 && flowdir%2==0 && n%2==1) ){
      minimum_elevation=flat_mask(nx,ny);
      flowdir=n;
    }
  }

  return flowdir;
}

//d8_flow_flats
/**
  @brief  Calculates flow directions in flats
  @author Richard Barnes (rbarnes@umn.edu)

  This determines flow directions within flats which have been resolved
  using resolve_flats_barnes().

  Uses the helper function d8_masked_FlowDir()

  @param[in]  &flat_mask      A mask from resolve_flats_barnes()
  @param[in]  &labels         The labels output from resolve_flats_barnes()
  @param[out] &flowdirs       Returns flat-resolved flow directions

  @pre
    1. **flat_mask** contains the number of increments to be applied to each
       cell to form a gradient which will drain the flat it is a part of.
    2. Any cell without a local gradient has a value of #NO_FLOW in
       **flowdirs**; all other cells have defined flow directions.
    3. If a cell is part of a flat, it has a value greater than zero in
       **labels** indicating which flat it is a member of; otherwise, it has a
       value of 0.

  @post
    1. Every cell whose flow direction could be resolved by this algorithm
       (all drainable flats) will have a defined flow direction in
       **flowdirs**. Any cells which could not be resolved (non-drainable
       flats) will still be marked #NO_FLOW.
*/
template<class U>
void d8_flow_flats(
  const Array2D<int32_t> &flat_mask,
  const Array2D<int32_t> &labels,
  Array2D<U> &flowdirs
){
  ProgressBar progress;

  std::cerr<<"%%Calculating D8 flow directions using flat mask..."<<std::endl;
  progress.start( flat_mask.viewWidth()*flat_mask.viewHeight() );
  #pragma omp parallel for
  for(int x=1;x<flat_mask.viewWidth()-1;x++){
    progress.update( x*flat_mask.viewHeight() );
    for(int y=1;y<flat_mask.viewHeight()-1;y++)
      if(flat_mask(x,y)==flat_mask.noData())
        continue;
      else if (flowdirs(x,y)==NO_FLOW)
        flowdirs(x,y)=d8_masked_FlowDir(flat_mask,labels,x,y);
  }
  std::cerr<<"Succeeded in "<<progress.stop()<<"s."<<std::endl;
}

//Procedure: BuildAwayGradient
/**
  @brief Build a gradient away from the high edges of the flats
  @author Richard Barnes (rbarnes@umn.edu)

  The queue of high-edge cells developed in find_flat_edges() is copied
  into the procedure. A breadth-first expansion labels cells by their
  distance away from terrain of higher elevation. The maximal distance
  encountered is noted.

  @param[in]  &elevations   A 2D array of cell elevations
  @param[in]  &flowdirs     A 2D array indicating each cell's flow direction
  @param[out] &flat_mask    A 2D array for storing flat_mask
  @param[in]  &edges        The high-edge FIFO queue from find_flat_edges()
  @param[out] &flat_height  Vector with length equal to max number of labels
  @param[in]  &labels       2D array storing labels developed in label_this()

  @pre
    1. Every cell in **labels** is marked either 0, indicating that the cell is
       not part of a flat, or a number greater than zero which identifies the
       flat to which the cell belongs.
    2. Any cell without a local gradient is marked #NO_FLOW in **flowdirs**.
    3. Every cell in **flat_mask** is initialized to 0.
    4. **edges** contains, in no particular order, all the high edge cells of
       the DEM (those flat cells adjacent to higher terrain) which are part of
       drainable flats.

  @post
    1. **flat_height** will have an entry for each label value of **labels**
       indicating the maximal number of increments to be applied to the flat
       identified by that label.
    2. **flat_mask** will contain the number of increments to be applied
       to each cell to form a gradient away from higher terrain; cells not in a
       flat will have a value of 0.
*/
template<class U>
static void BuildAwayGradient(
  const Array2D<U>       &flowdirs,
  Array2D<int32_t>       &flat_mask,
  std::deque<grid_cell>  edges,
  std::vector<int>       &flat_height,
  const Array2D<int32_t> &labels
){
  Timer timer;
  timer.start();

  int loops = 1;
  grid_cell iteration_marker(-1,-1);

  std::cerr<<"Performing Barnes flat resolution's away gradient..."<<std::flush;

  //Incrementation
  edges.push_back(iteration_marker);
  while(edges.size()!=1){  //Only iteration marker is left in the end
    int x = edges.front().x;
    int y = edges.front().y;
    edges.pop_front();

    if(x==-1){  //I'm an iteration marker
      loops++;
      edges.push_back(iteration_marker);
      continue;
    }

    if(flat_mask(x,y)>0) continue;  //I've already been incremented!

    //If I incremented, maybe my neighbours should too
    flat_mask(x,y)=loops;
    flat_height[labels(x,y)]=loops;
    for(int n=1;n<=8;n++){
      int nx = x+dx[n];
      int ny = y+dy[n];
      if(labels.in_grid(nx,ny)
          && labels(nx,ny)==labels(x,y)
          && flowdirs(nx,ny)==NO_FLOW)
        edges.push_back(grid_cell(nx,ny));
    }
  }

  timer.stop();
  std::cerr<<"succeeded in "<<timer.accumulated()<<"s."<<std::endl;
}



//Procedure: BuildTowardsCombinedGradient
/**
  @brief Builds gradient away from the low edges of flats, combines gradients
  @author Richard Barnes (rbarnes@umn.edu)

  The queue of low-edge cells developed in find_flat_edges() is copied
  into the procedure. A breadth-first expansion labels cells by their
  distance away from terrain of lower elevation. This is combined with
  the gradient from BuildAwayGradient() to give the final increments of
  each cell in forming the flat mask.

  @param[in]  &elevations   A 2D array of cell elevations
  @param[in]  &flowdirs     A 2D array indicating each cell's flow direction
  @param[in,out] &flat_mask A 2D array for storing flat_mask
  @param[in]  &edges        The low-edge FIFO queue from find_flat_edges()
  @param[in]  &flat_height  Vector with length equal to max number of labels
  @param[in]  &labels       2D array storing labels developed in label_this()

  @pre
    1. Every cell in **labels** is marked either 0, indicating that the cell
       is not part of a flat, or a number greater than zero which identifies
       the flat to which the cell belongs.
    2. Any cell without a local gradient is marked #NO_FLOW in **flowdirs**.
    3. Every cell in **flat_mask** has either a value of 0, indicating
       that the cell is not part of a flat, or a value greater than zero
       indicating the number of increments which must be added to it to form a
       gradient away from higher terrain.
    4. **flat_height** has an entry for each label value of **labels**
       indicating the maximal number of increments to be applied to the flat
       identified by that label in order to form the gradient away from higher
       terrain.
    5. **edges** contains, in no particular order, all the low edge cells of
       the DEM (those flat cells adjacent to lower terrain).

  @post
    1. **flat_mask** will contain the number of increments to be applied
       to each cell to form a superposition of the gradient away from higher
       terrain with the gradient towards lower terrain; cells not in a flat
       have a value of 0.
*/
template<class U>
static void BuildTowardsCombinedGradient(
  const Array2D<U>       &flowdirs,
  Array2D<int32_t>       &flat_mask,
  std::deque<grid_cell>  edges,
  std::vector<int>       &flat_height,
  const Array2D<int32_t> &labels
){
  Timer timer;
  timer.start();

  int loops = 1;
  grid_cell iteration_marker(-1,-1);


  std::cerr<<"Barnes flat resolution: toward and combined gradients..."<<std::flush;

  //Make previous flat_mask negative so that we can keep track of where we are
  #pragma omp parallel for collapse(2)
  for(int x=0;x<flat_mask.viewWidth();x++)
  for(int y=0;y<flat_mask.viewHeight();y++)
    flat_mask(x,y)*=-1;


  //Incrementation
  edges.push_back(iteration_marker);
  while(edges.size()!=1){  //Only iteration marker is left in the end
    int x = edges.front().x;
    int y = edges.front().y;
    edges.pop_front();

    if(x==-1){  //I'm an iteration marker
      loops++;
      edges.push_back(iteration_marker);
      continue;
    }

    if(flat_mask(x,y)>0) continue;  //I've already been incremented!

    //If I incremented, maybe my neighbours should too
    if(flat_mask(x,y)!=0)  //If !=0, it _will_ be less than 0.
      flat_mask(x,y)=(flat_height[labels(x,y)]+flat_mask(x,y))+2*loops;
    else
      flat_mask(x,y)=2*loops;

    for(int n=1;n<=8;n++){
      int nx = x+dx[n];
      int ny = y+dy[n];
      if(labels.in_grid(nx,ny)
          && labels(nx,ny)==labels(x,y)
          && flowdirs(nx,ny)==NO_FLOW)
        edges.push_back(grid_cell(nx,ny));
    }
  }

  timer.stop();
  std::cerr<<"succeeded in "<<timer.accumulated()<<"s!"<<std::endl;
}


//Procedure: label_this
/**
  @brief Labels all the cells of a flat with a common label.
  @author Richard Barnes (rbarnes@umn.edu)

  Performs a flood fill operation which labels all the cells of a flat
  with a common label. Each flat will have a unique label

  @param[in]  x           x-coordinate of flood fill seed
  @param[in]  y           y-coordinate of flood-fill seed
  @param[in]  label       Label to apply to the cells
  @param[out] &labels     2D array which will contain the labels
  @param[in]  &elevations 2D array of cell elevations

  @pre
    1. **elevations** contains the elevations of every cell or a value _NoData_
       for cells not part of the DEM.
    2. **labels** has the same dimensions as **elevations**.
    3. **(x0,y0)** belongs to the flat which is to be labeled.
    4. **label** is a unique label which has not been previously applied to a
       flat.
    5. **labels** is initialized to zero prior to the first call to this
       function.

  @post
    1. **(x0,y0)** and every cell reachable from it by passing over only cells
       of the same elevation as it (all the cells in the flat to which c
       belongs) will be marked as **label** in **labels**.
*/
template<class T>
static void label_this(
  int x0,
  int y0,
  const int label,
  Array2D<int32_t> &labels,
  const Array2D<T> &elevations
){
  std::queue<grid_cell> to_fill;
  to_fill.push(grid_cell(x0,y0));
  const T target_elevation = elevations(x0,y0);

  while(to_fill.size()>0){
    grid_cell c = to_fill.front();
    to_fill.pop();
    if(elevations(c.x,c.y)!=target_elevation)
      continue;
    if(labels(c.x,c.y)>0)
      continue;
    labels(c.x,c.y)=label;
    for(int n=1;n<=8;n++)
      if(labels.in_grid(c.x+dx[n],c.y+dy[n]))
        to_fill.push(grid_cell(c.x+dx[n],c.y+dy[n]));
  }
}

//Procedure: find_flat_edges
/**
  @brief Identifies cells adjacent to higher and lower terrain
  @author Richard Barnes (rbarnes@umn.edu)

  Cells adjacent to lower and higher terrain are identified and
  added to the appropriate queue

  @param[out] &low_edges  Queue for storing cells adjacent to lower terrain
  @param[out] &high_edges Queue for storing cells adjacent to higher terrain
  @param[in]  &flowdirs   2D array indicating flow direction for each cell
  @param[in]  &elevations 2D array of cell elevations

  @pre
    1. **elevations** contains the elevations of every cell or a value _NoData_
       for cells not part of the DEM.
    2. Any cell without a local gradient is marked #NO_FLOW in **flowdirs**.

  @post
    1. **high_edges** will contain, in no particular order, all the high edge
       cells of the DEM: those flat cells adjacent to higher terrain.
    2. **low_edges** will contain, in no particular order, all the low edge
       cells of the DEM: those flat cells adjacent to lower terrain.
*/
template <class T, class U>
static void find_flat_edges(
  std::deque<grid_cell> &low_edges,
  std::deque<grid_cell> &high_edges,
  const Array2D<U>      &flowdirs,
  const Array2D<T>      &elevations
){
  int cells_without_flow=0;
  ProgressBar progress;
  std::cerr<<"%%Searching for flats..."<<std::endl;
  progress.start( flowdirs.viewWidth()*flowdirs.viewHeight() );
  for(int x=0;x<flowdirs.viewWidth();x++){
    progress.update( x*flowdirs.viewHeight() );
    for(int y=0;y<flowdirs.viewHeight();y++){
      if(flowdirs(x,y)==flowdirs.noData())
        continue;
      if(flowdirs(x,y)==NO_FLOW)
        cells_without_flow++;
      for(int n=1;n<=8;n++){
        int nx = x+dx[n];
        int ny = y+dy[n];

        if(!flowdirs.in_grid(nx,ny)) continue;
        if(flowdirs(nx,ny)==flowdirs.noData()) continue;

        if(flowdirs(x,y)!=NO_FLOW && flowdirs(nx,ny)==NO_FLOW && elevations(nx,ny)==elevations(x,y)){
          low_edges.push_back(grid_cell(x,y));
          break;
        } else if(flowdirs(x,y)==NO_FLOW && elevations(x,y)<elevations(nx,ny)){
          high_edges.push_back(grid_cell(x,y));
          break;
        }
      }
    }
  }
  std::cerr<<"Succeeded in "<<progress.stop()<<"s."<<std::endl;
  std::cerr<<cells_without_flow<<" cells had no flow direction."<<std::endl;
}


//Procedure: resolve_flats_barnes
/**
  @brief  Performs the flat resolution by Barnes, Lehman, and Mulla.
  @author Richard Barnes (rbarnes@umn.edu)

  TODO

  @param[in]  &elevations 2D array of cell elevations
  @param[in]  &flowdirs   2D array indicating flow direction of each cell
  @param[in]  &flat_mask  2D array which will hold incremental elevation mask
  @param[in]  &labels     2D array indicating flat membership

  @pre
    1. **elevations** contains the elevations of every cell or the _NoData_
        value for cells not part of the DEM.
    2. Any cell without a local gradient is marked #NO_FLOW in **flowdirs**.

  @post
    1. **flat_mask** will have a value greater than or equal to zero for every
       cell, indicating its number of increments. These can be used be used
       in conjunction with **labels** to determine flow directions without
       altering the DEM, or to alter the DEM in subtle ways to direct flow.
    2. **labels** will have values greater than or equal to 1 for every cell
       which is in a flat. Each flat's cells will bear a label unique to that
       flat.
*/
template <class T, class U>
void resolve_flats_barnes(
  const Array2D<T> &elevations,
  const Array2D<U> &flowdirs,
  Array2D<int32_t> &flat_mask,
  Array2D<int32_t> &labels
){
  Timer timer;
  timer.start();

  std::deque<grid_cell> low_edges,high_edges;  //TODO: Need estimate of size

  std::cerr<<"\n###Barnes Flat Resolution"<<std::endl;

  std::cerr<<"Setting up labels matrix..."<<std::flush;
  labels.templateCopy(elevations);
  labels.resize(flowdirs);
  labels.init(0);
  std::cerr<<"succeeded."<<std::endl;

  std::cerr<<"Setting up flat resolution mask..."<<std::flush;
  flat_mask.templateCopy(elevations);
  flat_mask.resize(elevations);
  flat_mask.init(0);
  flat_mask.setNoData(-1);
  std::cerr<<"succeeded!"<<std::endl;

  find_flat_edges(low_edges, high_edges, flowdirs, elevations);

  if(low_edges.size()==0){
    if(high_edges.size()>0)
      std::cerr<<"There were flats, but none of them had outlets!"<<std::endl;
    else
      std::cerr<<"There were no flats!"<<std::endl;
    return;
  }

  std::cerr<<"Labeling flats..."<<std::flush;
  int group_number=1;
  for(auto i=low_edges.begin();i!=low_edges.end();++i)
    if(labels(i->x,i->y)==0)
      label_this(i->x, i->y, group_number++, labels, elevations);
  std::cerr<<"succeeded!"<<std::endl;

  std::cerr<<"Found "<<group_number<<" unique flats."<<std::endl;

  std::cerr<<"Removing flats without outlets from the queue..."<<std::flush;
  std::deque<grid_cell> temp;
  for(auto i=high_edges.begin();i!=high_edges.end();++i)
    if(labels(i->x,i->y)!=0)
      temp.push_back(*i);
  std::cerr<<"succeeded."<<std::endl;

  if(temp.size()<high_edges.size())  //TODO: Prompt for intervention?
    std::cerr<<"\033[91mNot all flats have outlets; the DEM contains sinks/pits/depressions!\033[39m"<<std::endl;
  high_edges=temp;
  temp.clear();

  std::cerr<<"The flat height vector will require approximately "
           <<(group_number*((long)sizeof(int))/1024/1024)
           <<"MB of RAM."<<std::endl;
        
  std::cerr<<"Creating flat height vector..."<<std::flush;
  std::vector<int> flat_height(group_number);
  std::cerr<<"succeeded!"<<std::endl;

  BuildAwayGradient(
    flowdirs, flat_mask, high_edges, flat_height, labels
  );
  BuildTowardsCombinedGradient(
    flowdirs, flat_mask, low_edges, flat_height, labels
  );

  std::cerr<<"Calculation time for Barnes Flat Resolution algorithm: "<<timer.stop()<<"s."<<std::endl;
}



//d8_flats_alter_dem
/**
  @brief  Alters the elevations of the DEM so that all flats drain
  @author Richard Barnes (rbarnes@umn.edu)

  This alters elevations within the DEM so that flats which have been
  resolved using resolve_flats_barnes() will drain.

  @param[in]     &flat_mask   A mask from resolve_flats_barnes()
  @param[in]     &labels      A grouping from resolve_flats_barnes()
  @param[in,out] &elevations  2D array of elevations

  @pre
    1. **flat_mask** contains the number of increments to be applied to each
       cell to form a gradient which will drain the flat it is a part of.
    2. If a cell is part of a flat, it has a value greater than zero in
       **labels** indicating which flat it is a member of; otherwise, it has a
       value of 0.

  @post
    1. Every cell whose part of a flat which could be drained will have its
       elevation altered in such a way as to guarantee that its flat does
       drain.
*/
template<class U>
void d8_flats_alter_dem(
  const Array2D<int32_t> &flat_mask,
  const Array2D<int32_t> &labels,
  Array2D<U> &elevations
){
  ProgressBar progress;

  std::cerr<<"%%Calculating D8 flow directions using flat mask..."<<std::endl;
  progress.start( flat_mask.viewWidth()*flat_mask.viewHeight() );
  #pragma omp parallel for
  for(int x=1;x<flat_mask.viewWidth()-1;x++){
    progress.update( x*flat_mask.viewHeight() );
    for(int y=1;y<flat_mask.viewHeight()-1;y++){
      if(labels(x,y)==0)
        continue;

      bool higher[9];
      for(int n=1;n<=8;++n)
        higher[n]=elevations(x,y)>elevations(x+dx[n],y+dy[n]);
      //TODO: nextafterf is the floating point version; should use an
      //overloaded version instead to be able to handle both double and float
      for(int i=0;i<flat_mask(x,y);++i)
        elevations(x,y)=nextafterf(elevations(x,y),std::numeric_limits<U>::infinity());
      for(int n=1;n<=8;++n){
        int nx=x+dx[n];
        int ny=y+dy[n];
        if(labels(nx,ny)==labels(x,y))
          continue;
        if(elevations(x,y)<elevations(nx,ny))
          continue;
        if(!higher[n])
          std::cerr<<"Attempting to raise ("<<x<<","<<y<<") resulted in an invalid alteration of the DEM!"<<std::endl;
      }
    }
  }
  std::cerr<<"Succeeded in "<<progress.stop()<<"s."<<std::endl;
}


template<class T, class U>
void barnes_flat_resolution_d8(Array2D<T> &elevations, Array2D<U> &flowdirs, bool alter){
  d8_flow_directions(elevations,flowdirs);

  Array2D<int32_t> flat_mask, labels;

  resolve_flats_barnes(elevations,flowdirs,flat_mask,labels);

  if(alter){  
    //NOTE: If this value appears anywhere an error's occurred
    flowdirs.init(155);
    d8_flats_alter_dem(flat_mask, labels, elevations);
    d8_flow_directions(elevations,flowdirs);
  } else {
    d8_flow_flats(flat_mask,labels,flowdirs);
  }

  flowdirs.templateCopy(elevations);
}

#endif
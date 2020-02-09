/*
 * basePipe hpp + cpp protoype and define a base class for building
 * pipeline functions to execute
 *
 */
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <random>
#include <chrono>
#include <string>
#include <numeric>
#include <iostream>
#include <functional> 
#include <vector>
#include "denStream.hpp"
#include "dbscan.hpp"
#include "utils.hpp"
/////// Based off algorithm outlined in Cao et al 06 "Density-Based Clustering over an Evolving Data Stream with Noise"/////

microCluster::microCluster(int t, int dim, double l){
  creationTime = t;
  lambda = l;
  center = std::vector<double>(dim, 0);
  avgSquares = std::vector<double>(dim, 0);
  weight = 0;
  editTime = 0;
}

void microCluster::insertPoint(std::vector<double> point, int timestamp){
  double newWeight = weight*pow(2, -lambda*(timestamp - editTime)) + 1;

  if(weight == 0){ //No points in cluster
    center = point;
    for(int i=0; i<point.size(); i++){
      avgSquares[i] = point[i]*point[i];
    }    
  } else{ //Add point to cluster
    for(int i=0; i<point.size(); i++){
      center[i] = center[i] + (point[i]-center[i])/newWeight;
      avgSquares[i] = avgSquares[i] + (point[i]*point[i]-avgSquares[i])/newWeight;
    }
  }

  editTime = timestamp;
  weight = newWeight;
}

std::vector<double> microCluster::getCenter(){
  return center;
}

int microCluster::getCreationTime(){
  return creationTime;
}

double microCluster::getWeight(int timestamp){
  return weight * pow(2, -lambda*(timestamp-editTime));
}

double microCluster::getRadius(int timestamp){
  double r2 = 0;
  for(int i=0; i<center.size(); i++){
    r2 += avgSquares[i] - center[i]*center[i];
  }

  return sqrt(r2);
}

double microCluster::mergeRadius(std::vector<double> point, int timestamp){
  double newWeight = weight*pow(2, -lambda*(timestamp - editTime)) + 1;
  double r2 = 0;

  for(int i=0; i<center.size(); i++){
    double newCenter = center[i] + (point[i]-center[i])/newWeight;
    double newAvgSquare = avgSquares[i] + (point[i]*point[i]-avgSquares[i])/newWeight;
    r2 += newAvgSquare - newCenter*newCenter;
  }

  return sqrt(r2);
}

// basePipe constructor
denStream::denStream(){
	procName = "DenStream";
    return;
}
//taking in preprocessor type


// runPipe -> Run the configured functions of this pipeline segment
pipePacket denStream::runPreprocessor(pipePacket inData){
  /////////constants//////////
  initPoints = 1000; // points to generate p clusters (large data sets, use 1000) //---------------
  minPoints = 20; //For DBSCAN //---------------------------------------------------------------
  lambda = 0.25; // decay factor
  mu = 10;   //weight of data points in cluster threshold
  beta = 0.2; // outlier threshold
  streamSpeed = 20; //number of points in one unit time

  Tp = ceil((1/lambda)*log((beta*mu)/((beta*mu)-1)));
  timestamp = 0; //Current time
  int numPerTime = 0; //Number of points processed in this unit time
  double xi_den = pow(2, -Tp*lambda)-1; //Denominator for outlier lower weight limit

  ///////initialize p micro clusters... DBSCAN first N points (N has to be less than size of input data to simulate stream)///////
  //this returns cluster labels corresponding to current points
  std::vector<int> clusterLabels = dbscan::cluster(inData.originalData, minPoints, epsilon, initPoints);
  int pClusters = *std::max_element(clusterLabels.begin(), clusterLabels.end()); //Number of clusters found

  timestamp = initPoints/streamSpeed;
  numPerTime = initPoints % streamSpeed;

  if(pClusters == -1) pClusters = 1;
  pMicroClusters = std::vector<microCluster>(pClusters, microCluster(timestamp, inData.originalData[0].size(), lambda));

  //Take labels of each point and insert into appropriate microCluster
  for(int i = 0; i<initPoints; i++){
    if(clusterLabels[i] != -1){ //Not noise
      pMicroClusters[clusterLabels[i]-1].insertPoint(inData.originalData[i], timestamp);
    }
  }

  for(int i = initPoints; i<inData.originalData.size(); i++){
    numPerTime++;
    if(numPerTime == streamSpeed){ //1 unit time has elapsed
      numPerTime = 0;
      timestamp++;
    }

    merging(inData.originalData, i, timestamp);

    if(timestamp % Tp == 0 && numPerTime == 0){
      auto itP = pMicroClusters.begin();
      while(itP != pMicroClusters.end()){
        if(itP->getWeight(timestamp) < beta*mu){ //Weight too low - no longer a pMicroCluster
          itP = pMicroClusters.erase(itP);
        } else{
          ++itP;
        }
      }

      auto itO = oMicroClusters.begin();
      while(itO != oMicroClusters.end()){
        int T0 = itO->getCreationTime();
        double xi_num = pow(2, -lambda*(timestamp - T0 + Tp)) - 1;
        double xi = xi_num/xi_den; //Outlier lower weight limit

        if(itO->getWeight(timestamp) < xi){ //Weight too low - no longer an oMicroCluster
          itO = oMicroClusters.erase(itO);
        } else{
          ++itO;
        }
      }
    }
  }

  std::vector<std::vector<double>> centers(pMicroClusters.size()); //Return p-micro-cluster centers
  for(int i=0; i<pMicroClusters.size(); i++){
    centers[i] = pMicroClusters[i].getCenter();
  }

  inData.originalData = centers;
	return inData;
}

//Index of nearest p-micro-cluster
int denStream::nearestPCluster(std::vector<double> point){
  utils ut;

  int min = 0;
  double minDist = ut.vectors_distance(pMicroClusters[0].getCenter(), point); 

  for(int i=1; i<pMicroClusters.size(); i++){
    double dist = ut.vectors_distance(pMicroClusters[i].getCenter(), point);
    if(dist < minDist){
      minDist = dist;
      min = i;
    }
  }

  return min;
}

//Index of nearest o-micro-cluster
int denStream::nearestOCluster(std::vector<double> point){
  utils ut;

  int min = 0;
  double minDist = ut.vectors_distance(oMicroClusters[0].getCenter(), point); 

  for(int i=1; i<oMicroClusters.size(); i++){
    double dist = ut.vectors_distance(oMicroClusters[i].getCenter(), point);
    if(dist < minDist){
      minDist = dist;
      min = i;
    }
  }

  return min;
}

//function merging (takes in point p, p micro clusters, o micro clusters )
void denStream::merging(std::vector<std::vector<double>> &data, int p, int timestamp){
  if(pMicroClusters.size() > 0){
    int nearestP = nearestPCluster(data[p]);
    if(pMicroClusters[nearestP].mergeRadius(data[p], timestamp) <= epsilon){ //Successfully add point to nearest p-micro-cluster
      pMicroClusters[nearestP].insertPoint(data[p], timestamp);
      return;
    }
  }

  if(oMicroClusters.size() > 0){
    int nearestO = nearestOCluster(data[p]);
    if(oMicroClusters[nearestO].mergeRadius(data[p], timestamp) <= epsilon){ //Successfully add point to nearest o-micro-cluster
      oMicroClusters[nearestO].insertPoint(data[p], timestamp);
      if(oMicroClusters[nearestO].getWeight(timestamp) > beta*mu){ //Move from o-micro-cluster to p-micro-cluster
        pMicroClusters.push_back(oMicroClusters[nearestO]);
        oMicroClusters.erase(oMicroClusters.begin()+nearestO);
      }
      return;
    }
  }

  microCluster x(timestamp, data[0].size(), lambda); //Create new o-micro-cluster
  x.insertPoint(data[p], timestamp);
  oMicroClusters.push_back(x);
}
  

// configPipe -> configure the function settings of this pipeline segment
bool denStream::configPreprocessor(std::map<std::string, std::string> configMap){
  std::string strDebug;
  
  auto pipe = configMap.find("debug");
  if(pipe != configMap.end()){
    debug = std::atoi(configMap["debug"].c_str());
    strDebug = configMap["debug"];
  }

  pipe = configMap.find("outputFile");
  if(pipe != configMap.end())
    outputFile = configMap["outputFile"].c_str();
  
  ut = utils(strDebug, outputFile);
  
  pipe = configMap.find("epsilon");
    if(pipe !=configMap.end())
      epsilon = std::stod(configMap["epsilon"].c_str());
    else epsilon = 16; //Same as DBSCAN

  ut.writeDebug("StreamKMeans","Configured with parameters { epsilon: " + std::to_string(epsilon) + ", debug: " + strDebug + ", outputFile: " + outputFile + " }");

	return true;
}